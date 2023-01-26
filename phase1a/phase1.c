#include <usloss.h>
#include <phase1.h> 
#include <stdlib.h>

#define LOWPRIORITY 7
#define HIGHPRIORITY 1 

// Process Struct Essentially
typedef struct Process{
    char name[MAXNAME];
    int PID;
    int parentPID;
    int priority;
    int status; 
    USLOSS_Context context;
} Process;

Process ProcessTable[MAXPROC];
Process* CurrProcess;
USLOSS_Context context;

int (*startFunc) (char*);

void phase1_init(void) {

}

void startProcesses(void) {

    // need to make a context
    USLOSS_ContextInit()
}
 
int fork1(char *name, int(*func)(char *), char *arg,
                  int stacksize, int priority) {
    
    // fork makes a process, so we call context init here as well
    USLOSS_ContextInit();
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

void trampoline() {

}