/**
 * AUTHORS:    Kevin Nisterenko and Rey Sanayei
 * COURSE:     CSC 452, Spring 2023
 * INSTRUCTOR: Russell Lewis
 * ASSIGNMENT: Phase1a
 * DUE_DATE:   02/02/2023
 * 
 * This project implements context management in a simulation of an operating
 * system kernel. It handles the Process Table, process creation, quitting, 
 * joining and some basic bootstrap for simulation of the kernel. 
 */

// ----- Includes
#include <usloss.h>
#include <phase1.h> 
#include <stdlib.h>
#include <string.h>

// ----- Constants
#define LOW_PRIORITY 7   
#define HIGH_PRIORITY 1

// Error codes
#define DEADLOCK_CODE 1 // means we hit a deadlock for the halt

// Processes states
#define RUNNABLE 0      // means the process is in a runnable state
#define RUNNING 1       // means the process is currently running
#define DEAD 2          // means the process quit
#define BLOCKED_JOIN 3  // means the process was blocked in a join
#define FREE 9          // means the slot is free

// ---- typedefs
typedef struct Process Process;

// ----- Structs

/**
 * Struct for a process, contains all the information on the process, including
 * it's main function pointer. Also contains all the necessary info (priority,
 * id, state, etc).
 */
struct Process {
    char name[MAXNAME];         // name of the process
    int PID;                    // process id
    int priority;               // process priority
    int slot;                   // slot in the process table
    int state;                  // process state, runnable, running etc
    int joinWait;               // join waiting
    void* stack;                // every process needs a stack allocated to it
    int stSize;                 // stackSize
    int (*processMain)(char* ); // a process also has a main function
    char args[MAXARG];
    int exitState;              // before process actually dies (zombie process),
                                // save its exit state for quit, cleaned up by
                                // parent on quit

    Process* parent;            // parent pointer 
    Process* firstChild;        // first child of the process, links to other
    Process* firstSibling;      // first sibling, linked list
    int numChildren;            // number of children that spanned from this

    USLOSS_Context context;     // USLOSS context for the init and switches
};

// ----- Function Prototypes
// phase1 funcs
void phase1_init(void);
void startProcesses(void);
int fork1(char *name, int(*func)(char *), char *arg, int stacksize, int priority);
int join(int *status);
void quit(int status, int switchToPid);
void dumpProcesses(void);
int getpid(void);

// phase 1a
void TEMP_switchTo(int newpid);

// helpers
void kernelCheck(char* proc);
void trampoline();
void disableInterrupts();
void restoreInterrupts();
void cleanEntry(int idx);
int slotFinder();

// processes
int init(char* usloss);
int sentinel(char* usloss);
int testcase_mainProc(char* usloss);

// ----- Global data structures/vars
Process ProcessTable[MAXPROC]; // actual Process Table
Process* CurrProcess;          // current process/running process
int pidIncrementer;            // takes care of the pid
int procCount;                 // how many process currently in the table

/**
 * Bootstarp for the process table and the init process, only populates 
 * the process table with empty processes, so that we can actually init them 
 * once we run them. 
 */
void phase1_init(void) {
    kernelCheck("phase1_init");

    // initializing the process table (global data structure)
    for (int i = 0; i < MAXPROC; i++) {
        cleanEntry(i); 
    }

    // set currProcess to the init, switch to it, start at init's pid
    pidIncrementer = 1;

    int slot = slotFinder();
    
    // fill in the information for setup of init process
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

    // make the init the current process so we can later run it
    CurrProcess = &ProcessTable[slot];

    pidIncrementer++;

    startProcesses();
}

/**
 * This function starts the running of processes, changes init from runnable to
 * running. Uses one context switch.
 */
void startProcesses(void) {
    CurrProcess->state = RUNNING;
    USLOSS_ContextSwitch(NULL, &CurrProcess->context);
}

/**
 * This function simulates a fork call to create children processes based on the
 * parent process (currProcess), it doesn't do anything like duplicating it, but 
 * does create a children process on the table if possible and sets it to running.
 * 
 * @param name, char pointer, name of the new process
 * @param func, function representing the main of the process
 * @param arg, char pointer representing the arguments to be passed to the
 * process' main
 * @param stacksize, integer representing the size of the stack allocated for
 * this process
 * @param priority, integer representing the priority of the child process
 * @return process id of the newly created process, or an error code if there 
 * was an error when creating the process
 */
int fork1(char *name, int(*func)(char *), char *arg, int stacksize, int priority) {
    kernelCheck("fork1");	

    disableInterrupts();

    // problem with args or num processes, maybe out of priority
    if (name == NULL || strlen(name) > MAXNAME || func == NULL || 
        priority >= LOW_PRIORITY-1 || priority < HIGH_PRIORITY || 
        procCount == MAXPROC) {
        return -1;
    } 

    // problem with stack size
    if (stacksize < USLOSS_MIN_STACK) {
        return -2;
    }

    // increase the number of processes
    procCount++;

    // get the next slot depending on the pid 
    int slot = slotFinder();

    // if we had an invalid slot, we return and don't fork 
    if (slot == -1) {
        return -1;
    }

    strcpy(ProcessTable[slot].name, name);

    // if the arguments for the process were NULL we set it to null char,
    // otherwsie, copy the string
    if (arg == NULL) {
        ProcessTable[slot].args[0] = '\0';
    } else {
        strcpy(ProcessTable[slot].args, arg);
    }

    // fill in process info
    ProcessTable[slot].PID = pidIncrementer;
    ProcessTable[slot].processMain = func;
    ProcessTable[slot].stSize = stacksize;
    ProcessTable[slot].stack = malloc(stacksize);
    ProcessTable[slot].priority = priority;
    ProcessTable[slot].state = RUNNABLE;
    ProcessTable[slot].parent = CurrProcess;
    ProcessTable[slot].slot = slot;

    pidIncrementer++;

    // if this is the first child just set it, otherwise we can just get the
    // child reference and update the list pointer
    if (CurrProcess->firstChild == NULL) {
        CurrProcess->firstChild = &ProcessTable[slot];
    } else {
        // adding to head
        Process* cur = CurrProcess->firstChild;

        ProcessTable[slot].firstSibling = cur;
        CurrProcess->firstChild = &ProcessTable[slot];
    }
    CurrProcess->numChildren++;

    // initialize the context and then restore interrupts
    USLOSS_ContextInit(&ProcessTable[slot].context, ProcessTable[slot].stack, 
                       ProcessTable[slot].stSize, NULL, &trampoline);

    restoreInterrupts();

    return ProcessTable[slot].PID;
}

/**
 * This function alerts the parent process about the dead children it finds,
 * it gives information about the exit state and also the process id of the 
 * removed process. Also performs clean up of the Process Table after 
 * dead process removal.
 * 
 * @param status, integer pointer representing the status of the exited 
 * process, we set it after we find the dead child
 * @return integer representing the process id of the dead child that join
 * found
 */
int join(int *status) {
    kernelCheck("join");

    disableInterrupts();

    // check if we have any children
    if (CurrProcess->numChildren == 0) {
        return -2;
    }

    // remove dead child
    Process* child = CurrProcess->firstChild;

    Process* removed = NULL;

    // if head is the dead child, we just remove it, otherwise, find the
    // first dead child
    if (child->state == DEAD) {
        removed = child;
        CurrProcess->firstChild = child->firstSibling;
    // remove from rest, first dead child we find
    } else {
        // iterate over the list, until we find a dead child, remove it
        while (child->firstSibling->state != DEAD) {
            child = child->firstSibling;
        }
        removed = child->firstSibling;
        child->firstSibling = child->firstSibling->firstSibling;
    }


    // if no one dead, just halt the sim, quit join should find a dead child
    if (removed == NULL) {
        USLOSS_Halt(4);
    }

    // clean up the dead children after reading its exit status
    CurrProcess->numChildren--;

    *status = removed->exitState;

    int res = removed->PID;
    int slot = removed->slot;

    cleanEntry(slot);

    restoreInterrupts();

    return res;
}

/**
 * Terminates the execution of the current process, saves the exit status 
 * passed into it and switches out of it. 
 * 
 * @param status, integer representing exit status of the process after we 
 * kill it
 * @param switchToPid, integer representing the pid of the next process to 
 * run since we don't yet have a dispatcher
 */
void quit(int status, int switchToPid) {
    kernelCheck("quit");

    // never returns, goes inside other funcs, so 
    // no need to restore interrupts, leave this to the trampoline
    disableInterrupts();

    // if parent dies before all children, halt sim
    if (CurrProcess->numChildren > 0) {
        USLOSS_Console("ERROR: Process pid %d called quit() while it still had children.\n", CurrProcess->PID);
        USLOSS_Halt(3);
    }

    // make this process dead and store exit status
    CurrProcess->state = DEAD;
    CurrProcess->exitState = status;

    // since we quit, decrease count
    procCount--;
    
    // quit doesn't care about saving prev context, so we just 
    // get the next PID from the param, and call a context switch
    for (int i = 0; i < MAXPROC; i++) {
        // found PID in table, we are assuming it exists
        if (ProcessTable[i].PID == switchToPid) {
            CurrProcess = &ProcessTable[i];
            break;
        }
    }
    CurrProcess->state = RUNNING;
    USLOSS_ContextSwitch(NULL, &CurrProcess->context);
}

/**
 * Prints all processes to USLOSS console, based on RUSS's implementation on 
 * discord. 
 */
void dumpProcesses(void) {
    USLOSS_Console(" PID  PPID  NAME              PRIORITY  STATE\n");

    // for every process, we want to print a human readable version of the
    // state
    for (int i=0; i < MAXPROC; i++) {
        Process *slot = &ProcessTable[i];
        if (slot->PID == 0)
            continue;

        int ppid = (slot->parent == NULL) ? 0 : slot->parent->PID;
        USLOSS_Console("%4d  %4d  %-17s %-10d", slot->PID, ppid, slot->name, slot->priority);

        if (slot->state == RUNNING)
            USLOSS_Console("Running\n");
        else if (slot->state == RUNNABLE)
            USLOSS_Console("Runnable\n");
        else
            USLOSS_Console("Terminated(%d)\n", slot->exitState);
    }
}

/**
 * Returns current process' id.
 * 
 * @return integer representing the process id of the current process
 */
int getpid() {
    kernelCheck("getpid");
    disableInterrupts();
    return CurrProcess->PID;
    restoreInterrupts();
}

// ----- Phase 1a

/**
 * This function works as a manual, brute dispatcher to switch between processes
 * since priorities are not implemented yet and the testcode does the switch
 * for us.
 */
void TEMP_switchTo(int newpid) {
    Process* oldProc = CurrProcess;
    for (int i = 0; i < MAXPROC; i++) {
        if (ProcessTable[i].PID == newpid) {
            CurrProcess = &ProcessTable[i];
            break;
        }
    }
    oldProc->state = RUNNABLE;
    CurrProcess->state = RUNNING;
    USLOSS_ContextSwitch(&oldProc->context, &CurrProcess->context);
}

// ----- Special Processes

/**
 * init process serves to bootstrap and kickstart our simulation, it runs 
 * at the second lowest priority and calls the service processes from 
 * other phases. Also here is where we create sentinel and testcase_main, also
 * context switch into testcase_main. We include an error checking join loop 
 * here as well. 
 * 
 * @param usloss, char pointer representing the arguments of this process' main
 * @return integer representing this process' main function return
 */
int init(char* usloss) {
    // calling the service funcs for different phases
    phase2_start_service_processes();
	phase3_start_service_processes();
	phase4_start_service_processes();
	phase5_start_service_processes();
    
    // creating sentinel manually since it is the lowest priority and thus
    // disallowed by fork, better than changing our fork implementation
    procCount++;
    CurrProcess->state = RUNNABLE;
    int slot = slotFinder();
    strcpy(ProcessTable[slot].name, "sentinel");
    
    ProcessTable[slot].args[0] = '\0';
    ProcessTable[slot].PID = pidIncrementer;
    ProcessTable[slot].processMain = &sentinel;
    ProcessTable[slot].stSize = USLOSS_MIN_STACK;
    ProcessTable[slot].stack = malloc(USLOSS_MIN_STACK);
    ProcessTable[slot].priority = LOW_PRIORITY;
    ProcessTable[slot].state = RUNNABLE;
    ProcessTable[slot].parent = CurrProcess;
    ProcessTable[slot].slot = slot;

    CurrProcess->numChildren++;
    CurrProcess->firstChild = &ProcessTable[slot];

    // still need to init the context even though we don't expect it to run
    USLOSS_ContextInit(&ProcessTable[slot].context, ProcessTable[slot].stack, 
                       ProcessTable[slot].stSize, NULL, &trampoline);
    
    pidIncrementer++;
    
    USLOSS_Console("Phase 1B TEMPORARY HACK: init() manually switching to testcase_main() after using fork1() to create it.\n");

    // calling fork for testcase_main
    fork1("testcase_main", &testcase_mainProc, NULL, USLOSS_MIN_STACK, 3);

    Process* old = CurrProcess;
    CurrProcess = &ProcessTable[3];

    CurrProcess->state = RUNNING;

    // start running testcase_main 
    USLOSS_ContextSwitch(&old->context, &CurrProcess->context);

    int res; 
    // here we have a while true loop to check for possible errors on join 
    // stemming from this process
    while (1) {
        res = join(&res);
        if (res == -2) 
            USLOSS_Console("ERROR: init() received -2 in infinite join() loop");
            break;
    }
    return 0;
}

/**
 * The sentinel process serves as a deadlock check and it should only run if
 * nothing else can, ie, it is the lowest priority process. This is Russ' 
 * implementation from the spec.
 * 
 * @param usloss, char pointer representing the arguments of this process' main
 * @return integer representing this process' main function return
 */
int sentinel(char* usloss) {
    while (1) {
        if (phase2_check_io() == 0)
            USLOSS_Console("DEADLOCK!!!!! need to see testcase for specific msg");
            USLOSS_Halt(DEADLOCK_CODE);
        USLOSS_WaitInt();
    }
    return 0;
}

/**
 * This function serves as a way to call the testcase's main function. If it 
 * ever returns, we output the error, however, on phase1a this is normal 
 * termination and expected, so we exit with normal termination of zero and
 * halt the simulation.
 * 
 * @param usloss, char pointer representing the arguments of this process' main
 * @return integer representing this process' main function return
 */
int testcase_mainProc(char* usloss) {
    int res = testcase_main();
    
    if (res != 0) {
        USLOSS_Console("ERROR ON MAIN, need to see testcase for msg");
    }
    USLOSS_Console("Phase 1B TEMPORARY HACK: testcase_main() returned, simulation will now halt.\n");
    USLOSS_Halt(0);
    return 0;
}

// ----- Helpers 

/**
 * Checks whether or not the simulation is running in kernel mode and halts the
 * entire simulation if so. 
 * 
 * @param proc, char pointer representing the process' name so we can output 
 * a good error message as to what process tried running in user mode
 */
void kernelCheck(char* proc) {
    // means we are running in user mode, so we halt simulation
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
		USLOSS_Console("ERROR: Someone attempted to call %s while in user mode!\n", proc);
		USLOSS_Halt(1);
	}
}

/**
 * This is used so that we can actually correctly call and never return from 
 * a process' main. It is a wrapper for USLOSS.
 */
void trampoline() {
    restoreInterrupts();
    CurrProcess->state = RUNNING;
    // call the process' main func, with its own args
    int res = CurrProcess->processMain(CurrProcess->args);
    
    // we don;t ever want to return, so if we actually get here we halt the
    // simulation with an error
    USLOSS_Halt(2);

    // exit the current process
    quit(res, CurrProcess->parent->PID);
}

/**
 * Disable all USLOSS interrupts
 */
void disableInterrupts() {
	int res = USLOSS_PsrSet(USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_INT);
}

/**
 * Restore all USLOSS interrupts
 */
void restoreInterrupts() {
	int res = USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);
}

/**
 * Cleans a specific entry in the process table so we can 
 * initialize it/quit the process. 
 * 
 * @param idx, index of the process we want to clean up in the
 * process table
 */
void cleanEntry(int idx) {
    ProcessTable[idx].name[0] = '\0';
    ProcessTable[idx].args[0] = '\0';
    ProcessTable[idx].PID = 0;
    ProcessTable[idx].stack = NULL;
    ProcessTable[idx].priority = 0;
    ProcessTable[idx].state = FREE;
    ProcessTable[idx].parent = NULL;
    ProcessTable[idx].firstChild = NULL;
    ProcessTable[idx].numChildren = 0;
    ProcessTable[idx].firstSibling = NULL;
    ProcessTable[idx].exitState = 0;
    ProcessTable[idx].slot = 0;
    ProcessTable[idx].joinWait = 0;
}

/**
 * Small helper to find the process index in the process table based
 * on its pidIncrementer, ie, always finds the next slot in the table. 
 * 
 * @return integer representing the next slot in the process table
 */
int slotFinder() {
    // cant really do it if we are full of processes
	if (procCount >= MAXPROC) 
		return -1;
	
	// try to find a good spot
	int procNum = 0;
    // here we have the actual index based on pid incrementer % MAXPROC
	while (ProcessTable[pidIncrementer % MAXPROC].state != FREE) {
        // if we end up going over the allowed number of process we return -1
		if (procNum < MAXPROC) {
            procNum++;
		    pidIncrementer++;
        } else {
            return -1;
        }
	}

	// PID updated
	return pidIncrementer % MAXPROC;
}
