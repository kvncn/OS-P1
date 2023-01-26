#include <usloss.h>
#include <phase1.h> 
#include <stdlib.h>

#define LOWPRIORITY 7
#define HIGHPRIORITY 1 

// Process Struct Essentially
typedef struct PTEntry {
    char name[MAXNAME];
    int PID;
    int parentPID;
    int priority;
    int status; 
} PTEntry;

// Just a basic LL for the Queue of each priority
typedef struct PQueue {
    PTEntry* head;
    PTEntry* tail;
    int size; 
} PQueue;

PTEntry ProcessTable[MAXPROC];
PTEntry* currProcess;

void phase1_init(void) {

}

void startProcesses(void) {

}
 
int fork1(char *name, int(*func)(char *), char *arg,
                  int stacksize, int priority) {
    return 0;
}

int join(int *status) {
    return 0;
}

void quit(int status, int switchToPid) {

}

void TEMP_switchTo(int pid) {

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