#include <usloss.h>
#include <phase1.h> 
#include <stdlib.h>
#include <string.h>

// ----- Constants
#define LOW_PRIORITY 7
#define HIGH_PRIORITY 1
#define DEADLOCK_CODE 1
#define RUNNABLE 0
#define RUNNING 1
#define DEAD 2
#define BLOCKED_JOIN 3 
#define FREE 9 // means the slot is free
// add a few more for like runnable, dead, blocked by join etc

// #define SLOT_FINDER(pid) (pid % MAXPROC) not working :(

// ---- typedefs

typedef struct Process Process;

// ----- Structs
struct Process {
    char name[MAXNAME];
    int PID;
    int priority;

    // 0-9, block me has 10 
    // runnable, blocked for join, blocked in zap, 
    int state; 

    // every process needs a stack allocated to it in mem, so this is where we
    // have it
    void* stack;
    int stSize; // likely use the USLOSS min stack size when malloc but idk

    // a process also has a main function, and that main function takes
    // args (char*)
    int (*processMain)(char* );
    char* args;

    // before process actually dies (zombie process), save its exit state for 
    // quit, cleaned up by parent on quit
    int exitState;

    // parent pointer
    Process* parent;

    Process* firstChild;
    int numChildren;
    
    Process* firstSibling;

    USLOSS_Context context;
};

// ----- Function Prototypes

// phase1 funcs
void phase1_init(void);
void startProcesses(void);
int fork1(char *name, int(*func)(char *), char *arg, int stacksize, int priority);
int join(int *status);
void quit(int status, int switchToPid);
void dumpProcesses(void);

// phase 1a
void TEMP_switchTo(int newpid);

// helpers
void kernelCheck(char* proc);
void trampoline();
void print_process(Process proc);
void disableInterrupts();
void restoreInterrupts();
void cleanEntry(Process proc);
int slotFinder(int x);

// processes
int init(char* usloss);
int sentinel(char* usloss);
int testcase_mainProc(char* usloss);

// ----- Global data structures/vars
Process ProcessTable[MAXPROC];
Process* CurrProcess;
USLOSS_Context context;
int pidIncrementer; // do % with MAXPROC to get arrayPos
int procCount;

/**
 * 
 */
void phase1_init(void) {
    kernelCheck("phase1_init");
    Process empty; 
    for (int i = 0; i < MAXPROC; i++) {
        ProcessTable[i] = empty;
        cleanEntry(ProcessTable[i]); 
    }

    // set currProcess to the init, switch to it, start at init's pid
    pidIncrementer = 1;

    int slot = slotFinder(pidIncrementer);
    
    strcpy(ProcessTable[slot].name, "init");
    ProcessTable[slot].PID = pidIncrementer;
    ProcessTable[slot].args[0] = '\0';
    ProcessTable[slot].processMain = &init;
    ProcessTable[slot].stSize = USLOSS_MIN_STACK;
    ProcessTable[slot].stack = malloc(USLOSS_MIN_STACK);
    ProcessTable[slot].priority = 6;
    ProcessTable[slot].state = RUNNABLE;
    procCount++;

    // create context for init
    USLOSS_ContextInit(&ProcessTable[slot].context, ProcessTable[slot].stack, 
                       ProcessTable[slot].stSize, NULL, trampoline);

    CurrProcess = &ProcessTable[slot];

    pidIncrementer++;

    startProcesses();
}

/**
 * 
 */
void startProcesses(void) {

    // do we disable interrupts and enable them here?

    CurrProcess->state = RUNNING;
    USLOSS_ContextSwitch(NULL, &CurrProcess->context);
}
 
int fork1(char *name, int(*func)(char *), char *arg, int stacksize, int priority) {
    kernelCheck("fork1");	

    disableInterrupts();

    // problem with args or num processes
    if (name == NULL || strlen(name) > MAXNAME || func == NULL || 
        priority < LOW_PRIORITY || priority > HIGH_PRIORITY || 
        priority == 6 || procCount == MAXPROC) {
        return -1;
    } 

    // problem with stack size
    if (stacksize < USLOSS_MIN_STACK) {
        return -2;
    }


    procCount++;

    int slot = slotFinder(pidIncrementer);

    strcpy(ProcessTable[slot].name, name);

    if (arg == NULL) {
        ProcessTable[slot].args[0] = '\0';
    } else {
        strcpy(ProcessTable[slot].args, arg);
    }

    ProcessTable[slot].PID = pidIncrementer;
    ProcessTable[slot].processMain = func;
    ProcessTable[slot].stSize = stacksize;
    ProcessTable[slot].stack = malloc(stacksize);
    ProcessTable[slot].priority = priority;
    ProcessTable[slot].state = RUNNABLE;
    ProcessTable[slot].parent = CurrProcess;

    pidIncrementer++;

    USLOSS_ContextInit(&ProcessTable[slot].context, ProcessTable[slot].stack, 
                       ProcessTable[slot].stSize, NULL, &trampoline);
        
    // fork makes a process, so we call context init here as well
    // USLOSS_ContextInit(&ProcessTable[i].context, ProcessTable[i].stack, 
    //                    ProcessTable[i].stSize, NULL, trampoline);
    
    restoreInterrupts();
    return ProcessTable[slot].PID;
}

int join(int *status) {
    kernelCheck("join");

    disableInterrupts();

    // is one of the children already dead?
    // do we remove the child from process table? YES, here
    if (CurrProcess->numChildren == 0) {
        USLOSS_Console("NO CHILDREN");
        USLOSS_Halt(4);
        return -2;
    }

    // remove dead child
    Process* child = CurrProcess->firstChild;

    Process* removed = NULL;

    // if dead is head
    if (child->state == DEAD) {
        removed = child;
        CurrProcess->firstChild = child->firstSibling;
    // remove from rest, first dead child we find
    } else {
        for (int i = 0; i < CurrProcess->numChildren; i++) {
            if (child->firstSibling != NULL && child->firstSibling->state == DEAD) {
                removed = child->firstSibling;
                break;
            } 
            child = child->firstSibling;
        }
        child->firstSibling = child->firstSibling->firstSibling;
    }

    // no one dead
    if (removed == NULL) {
        return -2;
    }

    CurrProcess->numChildren--;

    *status = removed->exitState;

    int res = removed->PID;

    cleanEntry(*removed);
    // either have a failed to say it was dead/free
    // or zero it out to clean up this dead child's slot

    // join is a way to block parent until a child has called quit

    restoreInterrupts();

    return res;
}

// might be needing a lot of work ngl
void quit(int status, int switchToPid) {
    kernelCheck("quit");

    // never returns, goes inside other funcs, so 
    // no need to re enable
    disableInterrupts();

    // i am not runnable, possible rc, if a contextswitch happens
    // disable interrupts to deal with them

    // if parent dies before all children, halt sim
    if (CurrProcess->numChildren > 0) {
        USLOSS_Console("ERROR: still had children\n");
        USLOSS_Halt(3);
    }

    CurrProcess->parent->numChildren--;

    // if this is the first child, make the next child the first sibling
    if (CurrProcess->parent->firstChild->PID == CurrProcess->PID) {
        CurrProcess->parent->firstChild = CurrProcess->firstSibling;
    }

    CurrProcess->state = DEAD;
    CurrProcess->exitState = status;

    if (CurrProcess->parent == NULL) {
        cleanEntry(ProcessTable[slotFinder(CurrProcess->PID)]);
        // don't need to save state if no parent to wake up, jut get out
    } else {
        if (CurrProcess->parent->state == BLOCKED_JOIN) {
            CurrProcess->parent->state = RUNNABLE;
        }
    }

    procCount--;
    // do we need this??
    // if (CurrProcess->parent->state == BLOCKED_JOIN) {
    //     >> wake up
    // }

    // save exit state 

    // maybe wake up parent (currently in blocked state, into runnable)
    //     -> check if it is waiting/blocked etc..., only wake it up if
    //        it was blocked by join
    //        how to wake up parent? change proc->state

    TEMP_switchTo(switchToPid);

}

/**
 * Prints all processes to USLOSS console
 */
void dumpProcesses(void) {
    for (int i = 0; i < MAXPROC; i++) {
        print_process(ProcessTable[i]);
    }
}

/**
 * Helper for dumpProcesses, prints one specific process
 */
void print_process(Process proc) {
    USLOSS_Console("-------Process %s-------\n", proc.name);
    USLOSS_Console("\t PID:\t%d\n", proc.PID);
    if (proc.parent != NULL) {
        USLOSS_Console("\t parentPID:\t%d\n", proc.parent->PID);
    }
    USLOSS_Console("\t priority:\t%d\n", proc.priority);
    USLOSS_Console("\t state ():\t%d\n", proc.state);
    USLOSS_Console("-----------------------\n");
}

// ----- Phase 1a

/**
 * This function works as a manual, brute dispatcher to switch between processes
 * since priorities are not implemented yet and the testcode does the switch
 * for us.
 */
void TEMP_switchTo(int newpid) {
    // assuming that pid corresponds to the index in the PTable
    Process* oldProc = CurrProcess;
    CurrProcess = &ProcessTable[newpid];
    USLOSS_ContextSwitch(&oldProc->context, &CurrProcess->context);
}

// ----- Special Processes

// init

int init(char* usloss) {
    // calling the service funcs for different phases
    phase2_start_service_processes();
	phase3_start_service_processes();
	phase4_start_service_processes();
	phase5_start_service_processes();
    
    // calling fork for sentinel
    fork1("sentinel", &sentinel, NULL, USLOSS_MIN_STACK, LOW_PRIORITY);
    
    // calling fork for testcase_main
    fork1("testcase_main", &testcase_mainProc, NULL, USLOSS_MIN_STACK, 3);
    
    int res; 
    
    while (1) {
        res = join(&res);
        if (res == -2) 
            USLOSS_Console("ERROR MESSAGE HERE, need to see testcases for msg");
            break;
    }
    
    return 0;
    
}

// sentinel 

int sentinel(char* usloss) {
    while (1) {
        if (phase2_check_io() == 0)
            USLOSS_Console("DEADLOCK!!!!! need to see testcase for specific msg");
            USLOSS_Halt(DEADLOCK_CODE);
        USLOSS_WaitInt();
    }
    return 0;
}

// testcase_main

int testcase_mainProc(char* usloss) {
    int res = testcase_main();
    
    if (res != 0) {
        USLOSS_Console("ERROR ON MAIN, need to see testcase for msg");
    }
    USLOSS_Halt(0);
    return 0;
}

// ----- Helpers 

/**
 * Checks whether or not the simulation is running in kernel mode and halts the
 * entire simulation if so. 
 */
void kernelCheck(char* proc) {
    // means we are running in user mode, so we halt simulation
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE == 0)) {
        USLOSS_Console("ERROR: halting blah blah blah need to check testcases");
        USLOSS_Halt(1);
    }
}

/**
 * This is used so that we can actually correctly call and never return from 
 * a process' main. 
 */
void trampoline() {
    // call the process' main func, with its own args
    int res = CurrProcess->processMain(CurrProcess->args);
    // quit on it, 1a will never return? so do we need to do it??? prob not for
    // 1a

    USLOSS_Halt(2);
    
    quit(res, CurrProcess->parent->PID);
}

/**
 * Disable all USLOSS interrupts
 */
void disableInterrupts() {
	unsigned int currPSR = USLOSS_PsrGet();
	int res = USLOSS_PsrSet(currPSR & ~USLOSS_PSR_CURRENT_INT);
}

/**
 * Restore all USLOSS interrupts
 */
void restoreInterrupts() {
	unsigned int currPSR = USLOSS_PsrGet();
	int res = USLOSS_PsrSet(currPSR | ~USLOSS_PSR_CURRENT_INT);
}

/**
 * Cleans a specific entry in the process table so we can 
 * initialize it/quit process
 */
void cleanEntry(Process proc) {
    proc.args[0] = ' \0' ;
    proc.PID = 0;
    proc.stack = NULL;
    proc.priority = 0;
    proc.state = FREE;
    proc.parent = NULL;
    proc.firstChild = NULL;
    proc.numChildren = 0;
    proc.firstSibling = NULL;
    proc.exitState = 0;
}

/**
 * Small helper to find the process index in the process table based
 * on its pid
 */
int slotFinder(int x) {
    return x % MAXPROC;
}
