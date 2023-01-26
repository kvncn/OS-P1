#include <usloss.h>
#include <phase1.h> 
#include <stdlib.h>

#define LOWPRIORITY 7
#define HIGHPRIORITY 1 

// Process Struct
typedef struct Process {
    char name[MAXNAME];
    int PID;
    int parentPID;
    int priority;
    int status; 

    // every process needs a stack allocated to it in mem, so this is where we
    // have it
    void* stack;
    int stSize; 


    USLOSS_Context context;
} Process;

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

Process ProcessTable[MAXPROC];
Process* CurrProcess;
USLOSS_Context context;

int (*startFunc) (char*);

void phase1_init(void) {
    // set currPrices to the init, switch to it
    TEMP_switchTo(0);
}

void startProcesses(void) {
    // need to make a context
    USLOSS_ContextInit(&ProcessTable[i].context, ProcessTable[i].stack, ProcessTable[i].stSize, NULL, trampoline)
}
 
int fork1(char *name, int(*func)(char *), char *arg, int stacksize, int priority) {
    
    // fork makes a process, so we call context init here as well
    USLOSS_ContextInit(&ProcessTable[i].context, ProcessTable[i].stack, ProcessTable[i].stSize, NULL, trampoline);
    return 0;
}

int join(int *status) {
    return 0;
}

void quit(int status, int switchToPid) {

}

/**
 * Prints all process to USLOSS console
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

// phase1a

void TEMP_switchTo(int newpid) {
    // no old process, esentially should only happen when we start, to 
    // switch to init
    if (newpid == -1) {
        USLOSS_ContextSwitch(NULL, &CurrProcess->context);
    }
    // assuming that pid corresponds to the index in the PTable
    Process* oldProc = CurrProcess;
    CurrProcess = ProcessTable[newpid];
    USLOSS_ContextSwitch(&oldProc->context, &CurrProcess->context);
}

// Special Processes

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

// helpers 

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

// wrapper func to call our other funcs, esp for process in contextInit
void trampoline() {

}