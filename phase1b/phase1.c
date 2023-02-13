/**
 * AUTHORS:    Kevin Nisterenko and Rey Sanayei
 * COURSE:     CSC 452, Spring 2023
 * INSTRUCTOR: Russell Lewis
 * ASSIGNMENT: Phase1b
 * DUE_DATE:   02/09/2023
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
#include <assert.h>

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
#define BLOCKED_ZAP 4   // means the process was blocked in a zap
#define FREE 9          // means the slot is free

// ---- typedefs
typedef struct Process Process;
typedef struct Queue Queue;

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

    Process* runNext;           // next pointer for the run queue

    int zappers;                // number of processes wanting to zap this
    Process* zappersHead;       // first zapper
    Process* zappersNext;       // subsequent zappers

    USLOSS_Context context;     // USLOSS context for the init and switches

    int start;                  // start time of the process
    int totalRuntime;           // total runtime of the process
};

/**
 * Struct for a Queue, specifically for the priority queues
 * for time sharing.
 */
struct Queue {
    Process* first;
    Process* last;
    int size; 
};

// ----- Function Prototypes
// phase1 funcs
void phase1_init(void);
void startProcesses(void);
int fork1(char *name, int(*func)(char *), char *arg, int stacksize, int priority);
int join(int *status);
void quit(int status);
void zap(int pid);
int isZapped(void);
void dumpProcesses(void);
int getpid(void);
void blockMe(int block_status);
int  unblockProc(int pid);
int  readCurStartTime(void);
void timeSlice(void);
int  readtime(void);
static void clockHandler(int dev, void *arg);
int  currentTime(void);

// DISPATCHER
void dispatcher(void);

// helpers
void kernelCheck(char* proc);
void trampoline();
void disableInterrupts();
void restoreInterrupts();
void cleanEntry(int idx);
int slotFinder();
void addToQueue (Process* proc);
void removeFromQueue(Process* proc);
void addZapper(Process* proc);
Process* removeZapper();
void blockProcess(int code);

// processes
int init(char* usloss);
int sentinel(char* usloss);
int testcase_mainProc(char* usloss);

// ----- Global data structures/vars
Process ProcessTable[MAXPROC]; // actual Process Table
Process* CurrProcess;          // current process/running process
int pidIncrementer;            // takes care of the pid
int procCount;                 // how many process currently in the table
Queue runQueue[LOW_PRIORITY];  // the run queues for the specific priorities

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

    // initialize the runQueues for each priority
    for (int i = 0; i < LOW_PRIORITY; i++) {
        runQueue[i].first = NULL;
        runQueue[i].last = NULL;
        runQueue[i].size = 0;
    }

    // set currProcess to the init, switch to it, start at init's pid
    pidIncrementer = 1;
    procCount = 0;
    CurrProcess = NULL;

    startProcesses();
}

/**
 * This function starts the running of processes, changes init from runnable to
 * running. Calls the dispatcher.
 */
void startProcesses(void) {
    int slot = 1;
    
    // fill in the information for setup of init process
    strcpy(ProcessTable[slot].name, "init");
    ProcessTable[slot].PID = pidIncrementer;
    ProcessTable[slot].args[0] = '\0';
    ProcessTable[slot].processMain = &init;
    ProcessTable[slot].stSize = USLOSS_MIN_STACK;
    ProcessTable[slot].stack = malloc(USLOSS_MIN_STACK);
    ProcessTable[slot].priority = 6;
    ProcessTable[slot].state = RUNNABLE;
    ProcessTable[slot].slot = slot;
    procCount++;

    pidIncrementer++;

    USLOSS_IntVec[USLOSS_CLOCK_INT] = clockHandler;

    // create context for init
    USLOSS_ContextInit(&ProcessTable[slot].context, ProcessTable[slot].stack, 
                       ProcessTable[slot].stSize, NULL, trampoline);

    // add init to the runQueue
    addToQueue(&ProcessTable[slot]);
    dispatcher();
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

    // problem with args or num processes
    if (name == NULL || strlen(name) > MAXNAME || func == NULL || procCount == MAXPROC) {
        return -1;
    } 

    // check if priority is ok and not sentinel
    if (priority > LOW_PRIORITY || priority == 6) {
		return -1;
	} else if (priority == LOW_PRIORITY && (strcmp(name, "sentinel") != 0)) {
		return -1;
	} else if (priority < HIGH_PRIORITY) {
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
    
    mmu_init_proc(ProcessTable[slot].PID);

    // add process to run queue
    addToQueue(&ProcessTable[slot]);

    dispatcher();

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
    if (CurrProcess->numChildren == 0 && CurrProcess->joinWait == 0) {
        return -2;
    }

    if (CurrProcess->joinWait == 0) {
        blockProcess(BLOCKED_JOIN);
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

    CurrProcess->joinWait -= 1;

    *status = removed->exitState;

    int res = removed->PID;
    int slot = removed->slot;

    cleanEntry(slot);
    procCount--;

    restoreInterrupts();

    return res;
}

/**
 * Terminates the execution of the current process, saves the exit status 
 * passed into it and switches out of it. 
 * 
 * @param status, integer representing exit status of the process after we 
 * kill it
 */
void quit(int status) {
    kernelCheck("quit");

    // never returns, goes inside other funcs, so 
    // no need to restore interrupts, leave this to the trampoline
    disableInterrupts();

    //USLOSS_Console("CurrProcess Join wait %d\n", CurrProcess->joinWait);
    // if parent dies before all children, halt sim
    if (CurrProcess->numChildren > 0 || CurrProcess->joinWait > 1) {
        USLOSS_Console("ERROR: Process pid %d called quit() while it still had children.\n", CurrProcess->PID);
        USLOSS_Halt(3);
    }

    // make this process dead and store exit status
    CurrProcess->state = DEAD;
    CurrProcess->exitState = status;
   
    removeFromQueue(CurrProcess);

    // if there is no parent to notify, just don't
    if (CurrProcess->parent == NULL) {
        cleanEntry(CurrProcess->slot);
        procCount--;
    } else {
        CurrProcess->parent->joinWait += 1;
        // if our parent was blocked waiting, notify it and make it runnable
        // again
        if (CurrProcess->parent->state == BLOCKED_JOIN) {
            CurrProcess->parent->state = RUNNABLE;
            addToQueue(CurrProcess->parent);
        }
    }
    if (isZapped()) {
        while (CurrProcess->zappersHead != NULL) {
            Process* fromZap = removeZapper();
            fromZap->state = RUNNABLE; 
            addToQueue(fromZap);
        }
    }
    mmu_quit(CurrProcess->PID);
    CurrProcess = NULL;
    dispatcher();
}

/**
 * We terminate the process based on the passed PID, it doesn't unblock 
 * the process, and the zap can be pending and performed later. So it isn't
 * exactly terminating the process as much as requesting it to be terminated.
 * 
 * @param pic, integer representing the process id of the process we want
 * to zap
 */
void zap(int pid) {
    kernelCheck("zap");
    disableInterrupts();

    if (pid <= 0) {
        USLOSS_Console("ERROR: Attempt to zap() a PID which is <=0.  other_pid = %d\n", pid);
		USLOSS_Halt(1);
    }
    if (pid == 1) {
		USLOSS_Console("ERROR: Attempt to zap() init.\n");
		USLOSS_Halt(1);
	}
	if (pid == CurrProcess->PID) {
		USLOSS_Console("ERROR: Attempt to zap() itself.\n");
		USLOSS_Halt(1);
	}

    Process* toZap = &ProcessTable[pid % MAXPROC];

    if (toZap->state == DEAD) {
        USLOSS_Console("ERROR: Attempt to zap() a process that is already in the process of dying.\n");
		USLOSS_Halt(1);
    }

    if (toZap->state == FREE || toZap->PID != pid) {
        USLOSS_Console("ERROR: Attempt to zap() a non-existent process.\n");
		USLOSS_Halt(1);
    }

    addZapper(toZap);
    blockProcess(BLOCKED_ZAP);

    restoreInterrupts();
}

/**
 * Checks to see if the current process has been zapped by another
 * process. 
 * 
 * @return integer representing whether or not the process has been zapped
 */
int isZapped() {
    kernelCheck("isZapped");
    disableInterrupts();

    if (CurrProcess->zappers > 0) return 1;

    restoreInterrupts();
    return 0;
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
        else if (slot->state == BLOCKED_JOIN)
            USLOSS_Console("Blocked(waiting for child to quit)\n");
        else if (slot->state == BLOCKED_ZAP)
            USLOSS_Console("Blocked(waiting for zap target to quit)\n");
        else if (slot->state > 10)
            USLOSS_Console("Blocked(%d)\n", slot->state);
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

/**
 * This function is used to block the current process and the reason for the
 * blockage is passed so that the process can communicate that later on.
 * 
 * @param block_status, integer representing the new reason for blocking the 
 * current process
 */
void blockMe(int block_status) {
    kernelCheck("blockMe");
    disableInterrupts();

    if (block_status <= 10) {
        USLOSS_Console("blockMe status has to be greater than 10, instead got %d\n", block_status);
		USLOSS_Halt(1);
    }

    restoreInterrupts();

    return blockProcess(block_status);

}

/**
 * Function to unblock the passed process (passed as in the id), the process is
 * then placed back on the run queue. The dispatcher is called before the 
 * return to account for a greater priority process being awakened.
 * 
 * @param pid, integer representing the process to unblock
 * @return integer representing the result of the operation, 0 if successfulm 
 * -2 otherwise
 */
int unblockProc(int pid) {
    kernelCheck("blockMe");
    disableInterrupts();

    Process* toUnblock = &ProcessTable[pid % MAXPROC];

    // process to unblock is non existent
    if (toUnblock->state == 0 || toUnblock->PID != pid) {
        return -2;
    } else if (toUnblock->state <= 10) {
        return -2;
    }

    // to unblock it, simply add it back to runQueue
    addToQueue(toUnblock);
    // make it runnable 
    toUnblock->state = RUNNABLE;

    dispatcher();
    restoreInterrupts();
    return 0;
}

/**
 * This function returns the current process' start time (wall-clock time),
 * when it started its time slice.
 * 
 * @return integer representing the start time of the current process
 */
int readCurStartTime() {
    kernelCheck("readCurStartTime");
    return CurrProcess->start;
}

/**
 * This function compares the current time and the start time and calls the
 * dispatcher if necessary (ie, process ran for too long). 
 */
void timeSlice() {
    kernelCheck("timeSlice");
    disableInterrupts();

    if ((currentTime() - CurrProcess->start) >= 80000) {
        dispatcher();
    } else {
        restoreInterrupts();
    }

}

/**
 * This function returns the total time (not only of the current time slice), 
 * consumed by a process. 
 * 
 * @return integer representing the total time the current process has ran
 */
int readtime() {
    kernelCheck("readtime");
    return CurrProcess->totalRuntime;
}

/**
 * Interrupt handler for the USLOSS CLOCK device, it takes in the device 
 * and arguments and calls the phase2 clock handler and checks if the
 * dispatcher also needs to be called depending on the timeslice. Taken
 * from Russ' spec.
 * 
 * @param dev, integer representing USLOSS device 
 * @param arg, void pointer representing the arguments in the process
 */
static void clockHandler(int dev, void *arg) {
    kernelCheck("clockHandler");
    if (0) {
        USLOSS_Console("clockHandler(): PSR = %d\n", USLOSS_PsrGet());
        USLOSS_Console("clockHandler(): currentTime = %d\n", currentTime());
    }
    /* make sure to call this first, before timeSlice(), since we want to do
    * the Phase 2 related work even if process(es) are chewing up lots of
    * CPU.
    */
    phase2_clockHandler();

    // call the dispatcher if the time slice has expired
    timeSlice();

    /* when we return from the handler, USLOSS automatically re-enables
    * interrupts and disables kernel mode (unless we were previously in
    * kernel code). Or I think so. I haven’t double-checked yet. TODO
    */
}

/**
 * Function to read the wall-clock time from USLOSS and return it as an
 * int. Used what Russ provided in the spec. 
 * 
 * @return retval, integer representing current clock time
 */
int currentTime() {
    kernelCheck("currentTime");
    int retval;

    int usloss_rc = USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &retval);
    assert(usloss_rc == USLOSS_DEV_OK);

    return retval;
}


// ---- DISPATCHER

/**
 * Dispatcher for the kernel, decides when and how to make context switches, 
 * so that priorities and time sharing are respected. 
 */
void dispatcher() {
    kernelCheck("dispatcher");
    disableInterrupts();
    Process* newProc;

    // When the timeslice is up, we'll take the process back and put it in the RunQ
    if (CurrProcess != NULL && (currentTime() - CurrProcess->start) > 80000) {
        removeFromQueue(CurrProcess);
        addToQueue(CurrProcess);
    }

    for (int i = 0; i <= LOW_PRIORITY; i++) {
        if (runQueue[i].size != 0) {
            newProc = runQueue[i].first;
            break;
        }
    }

    if(newProc == NULL) {
        USLOSS_Console("Encountered a NULL process in the runQueue\n");
        USLOSS_Halt(1);
    }

    // if current process is running, switch it to runnable
    if (CurrProcess != NULL && CurrProcess->state == RUNNING) {
        CurrProcess->state = RUNNABLE;
    }

    if (CurrProcess != NULL) {
        CurrProcess->totalRuntime += (currentTime() - CurrProcess->start);
    }

    // swapping the chosen process in
    Process* oldProcess = CurrProcess;
    CurrProcess = newProc;

    // Update process states (CAUTION: Might need more)
    CurrProcess->state = RUNNING;

    CurrProcess->start = currentTime();

    // MMU Interaction?
    mmu_flush();

    if(oldProcess == NULL)
        USLOSS_ContextSwitch(NULL, &CurrProcess->context);
    else
        USLOSS_ContextSwitch(&oldProcess->context, &CurrProcess->context);

    restoreInterrupts();

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

    fork1("sentinel", &sentinel, NULL, USLOSS_MIN_STACK, LOW_PRIORITY);

    fork1("testcase_main", &testcase_mainProc, NULL, USLOSS_MIN_STACK, 3);
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
            USLOSS_Console("DEADLOCK DETECTED!  All of the processes have blocked, but I/O is not ongoing.\n");
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
    USLOSS_Halt(res);
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

    // exit the current process
    quit(res);
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
    ProcessTable[idx].zappers = 0;
    ProcessTable[idx].zappersHead = NULL;
    ProcessTable[idx].zappersNext = NULL;
    ProcessTable[idx].start = 0;
    ProcessTable[idx].totalRuntime = 0;
    ProcessTable[idx].runNext = NULL;
    ProcessTable[idx].joinWait = 0;

    procCount--;
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

/**
 * Adds a process to its specific runQueue.
 * 
 * @param proc, Process pointer for the process to add to the queue
 */
void addToQueue(Process* proc) {
    int slot = proc->priority - 1;
    
    // if the queue is empty, just make it the head, otherwise, we add it 
    // to the end (last)
    if (runQueue[slot].first == NULL) {
        runQueue[slot].first = proc;
    } else {
        runQueue[slot].last->runNext = proc;
    }

    // have to make it the tail anyways, since it is newly added
     runQueue[slot].last = proc;
     runQueue[slot].size++;
}
/**
 * Removes a process from its specific runQueue.
 * 
 * @param proc, Process pointer for the process to be removed 
 * from the queue
 */
void removeFromQueue(Process* proc) {
    Process* previous = runQueue[proc->priority-1].first;

    // if this was the only proc in runQueue
    if (previous->runNext == NULL) {
        runQueue[proc->priority-1].first = NULL;
        runQueue[proc->priority-1].last = NULL; 
    }

    // if we need to remove the head
    if (previous == proc) {
        runQueue[proc->priority-1].first = previous->runNext;

        // if the head is also the tail
        if (runQueue[proc->priority-1].last == proc) {
            runQueue[proc->priority-1].last = NULL;
        }
    } else {
        while (previous->runNext != proc) {
			previous = previous->runNext;
		}
		previous->runNext = proc->runNext;

		// check for tail 
		if (runQueue[proc->priority-1].last == proc) {
			runQueue[proc->priority-1].last = previous;
        }
    }
    runQueue[proc->priority-1].size--;
}

/**
 * Adds the currProcess' to the proc zappers.
 * 
 * @param proc, Process pointer indicating the 
 * process we want to add the zapper to  
 */
void addZapper(Process* proc){
    proc->zappers++;
    // since we incremented, this means we actually have
    // none
    if (proc->zappers == 1) {
        proc->zappersHead = CurrProcess;
        return;
    }

     // adding to head
    Process* cur = proc->zappersHead;
    CurrProcess->zappersNext = cur;
    proc->zappersHead = CurrProcess;
}

/**
 * Removed the first zapper. 
 * 
 * @return proc, Process pointer indicating the 
 * process we removed from Curr's zappers
 */
Process* removeZapper() {
    Process* result = CurrProcess->zappersHead;

    CurrProcess->zappersHead = CurrProcess->zappersHead->zappersNext;

    CurrProcess->zappers--; 

    return result;
}

/**
 * Blocks current process with the given code. 
 * 
 * @param code, blocking code so we know the process'
 * status
 * 
 * @return int, 0 if block was successful
 */
void blockProcess(int code) {
    CurrProcess->state = code;

    // remove from runQueue to block
    removeFromQueue(CurrProcess);

    dispatcher();

    return code;
}
