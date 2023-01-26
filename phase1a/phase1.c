#include <usloss.h>
#include <phase1.h> 
#include <stdlib.h>

// ----- Constants
#define LOWPRIORITY 7
#define HIGHPRIORITY 1 
#define RUNNABLE 0
// add a few more for like runnable, dead, blocked by join etc

// ----- Structs
typedef struct Process {
    char name[MAXNAME];
    int PID;
    int parentPID;
    int priority;

    // 0-9, block me has 10 
    // runnable, blocked for join, blocked in zap, 
    int status; 

    // every process needs a stack allocated to it in mem, so this is where we
    // have it
    void* stack;
    int stSize; // likely use the USLOSS min stack size when malloc but idk

    // a process also has a main function, and that main function takes
    // args (char*)
    int (*processMain)(char* );
    char* args;

    // before process actually dies (zombie process), save its exit status for 
    // quit, cleaned up by parent on quit
    int exitStatus;

    USLOSS_Context context;
} Process;

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

// processes
int init(char* usloss);
int sentinel(char* usloss);
int testcase_mainProc(char* usloss);

// ----- Global data structures/vars
Process ProcessTable[MAXPROC];
Process* CurrProcess;
USLOSS_Context context;
int pidIncrementer; // do % with MAXPROC to get arrayPos

void phase1_init(void) {
    kernelCheck("phase1_init");
    // set currProcess to the init, switch to it, start at init's pid
    pidIncrementer = 1;
    TEMP_switchTo(pidIncrementer);
}

void startProcesses(void) {
    
    // need to make a context
    USLOSS_ContextInit(&ProcessTable[i].context, ProcessTable[i].stack, 
                       ProcessTable[i].stSize, NULL, trampoline);
}
 
int fork1(char *name, int(*func)(char *), char *arg, int stacksize, int priority) {
    kernelCheck("fork1");
    
    // fork makes a process, so we call context init here as well
    USLOSS_ContextInit(&ProcessTable[i].context, ProcessTable[i].stack, 
                       ProcessTable[i].stSize, NULL, trampoline);
    return 0;
}

int join(int *status) {
    kernelCheck("join");

    // is one of the children already dead?
    // do we remove the child from process table? YES, here

    // either have a filed to say it was dead/free
    // or zero it out to clean up this dead child's slot

    // join is a way to block parent until a child has called quit
    return 0;
}

void quit(int status, int switchToPid) {
    kernelCheck("quit");

    // i am not runnable, possible rc, if a contextswitch happens
    // disable interrupts to deal with them

    // if parent dies before all children, halt sim

    // save exit status 

    // maybe wake up parent (currently in blocked state, into runnable)
    //     -> check if it is waiting/blocked etc..., only wake it up if
    //        it was blocked by join
    //        how to wake up parent? change proc->status

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
    USLOSS_Console("\t parentPID:\t%d\n", proc.parentPID);
    USLOSS_Console("\t priority:\t%d\n", proc.priority);
    USLOSS_Console("\t status ():\t%d\n", proc.status);
    USLOSS_Console("-----------------------\n");
}

// ----- Phase 1a

/**
 * This function works as a manual, brute dispatcher to switch between processes
 * since priorities are not implemented yet and the testcode does the switch
 * for us.
 */
void TEMP_switchTo(int newpid) {
    // no old process, esentially should only happen when we start, to 
    // switch to init, might just make it not part of this func and just put
    // it on phase1_init
    if (newpid == 1) {
        USLOSS_ContextSwitch(NULL, &CurrProcess->context);
        return;
    }
    // assuming that pid corresponds to the index in the PTable
    Process* oldProc = CurrProcess;
    CurrProcess = &ProcessTable[newpid];
    USLOSS_ContextSwitch(&oldProc->context, &CurrProcess->context);
}

// ----- Special Processes

// init

int init(char* usloss) {
    return 0;
}

// sentinel 

int sentinel(char* usloss) {
    return 0;
}

// testcase_main

int testcase_mainProc(char* usloss) {
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
    quit(res, CurrProcess->parentPID);
}
