// SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin für Materialien und Energie GmbH
// SPDX-License-Identifier: MIT

/*! \file
 * \brief Feedback Support API 
 * */
#define __USE_FAST_VXWORKS_SYS_AUX_CLOCK__ defined vxWorks
#if __USE_FAST_VXWORKS_SYS_AUX_CLOCK__
#include <taskLib.h>
#include <logLib.h>
#include <sysLib.h>
#include <intLib.h>
#include <ioLib.h>
#include <sigLib.h>
#include <timers.h>
#else
#include <epicsMutex.h>
#include <epicsEvent.h>
#endif
#include <epicsMutex.h>
#include <epicsThread.h>
#include <dbScan.h>
#include <string.h>
#include <epicsStdioRedirect.h>
#include <errno.h>
#include <signal.h>
#include <semaphore.h>
#include <fcntl.h>                      /* For O_* constants */
#include <sys/stat.h>                   /* For mode constants */
#include <epicsExport.h>
#include "fbCore.h"

/*! Value returned if `sem_open' failed.  */
#ifndef SEM_FAILED
#define SEM_FAILED      ((sem_t *) 0)
#endif

#ifdef NODEBUG
#define Debug(L,FMT,ARGS...) ;
#else
#define Debug(L, FMT, ARGS...) { if(L <= fb_common_debug) \
	fprintf(stderr, FMT "\t- file : " __FILE__ ", line %d\n", ## ARGS, __LINE__); }
#endif

void fbLoop(void);
int fbInnerLoop(tfbdrvset * fbdrvset, tfbdrvpar * fbdrvpar);
void fbTriggerLoop(void);
void fbauxclkhandler(int arg);
void fbsignalhandler(int signal);
int fb_common_debug = 3;
static int fb_initialized = 0;
epicsExportAddress(int, fb_common_debug);
int fb_common_vxfbtask = 1;
epicsExportAddress(int, fb_common_vxfbtask);
#if __USE_FAST_VXWORKS_SYS_AUX_CLOCK__
int fbIntTaskId = 0;
SEM_ID fbSemBTimer;
#else
epicsEventId fbSignal;
#endif
int fbIntSigFlag = 0;
long fbLngCtrHandler = 0;
epicsExportAddress(int, fbLngCtrHandler);
long fbLngCtrCatcher = 0;
epicsExportAddress(int, fbLngCtrCatcher);
int fbIntSoftTriggerDelay = -1;
epicsExportAddress(int, fbIntSoftTriggerDelay);

/* defined in support module: */
tfbdrvset *feedbackPluginList[FB_MAX_MEMBERS] = { 0 };

tfbdrvset *pfbDrvDset = NULL;

void fbPrintSemaphoreError(int error)
{
    Debug(0, "Could not open semaphore %s", FB_SEM_NAME);
    if (error == EEXIST) {
        Debug(0, "Semaphore already exists: %s", FB_SEM_NAME);
    }
    if (error == EACCES) {
        Debug(0, "The semaphore exists, but the caller does not have permission to open it.: %s", FB_SEM_NAME);
    }
    if (error == EINVAL) {
        Debug(0, "value was greater than SEM_VALUE_MAX: %s", FB_SEM_NAME);
    }
    if (error == EMFILE) {
        Debug(0, "The process already has the maximum number of files and open: %s", FB_SEM_NAME);
    }
    if (error == ENAMETOOLONG) {
        Debug(0, "name was too long: %s", FB_SEM_NAME);
    }
    if (error == ENFILE) {
        Debug(0, "The system limit on the total number of open files has been reached: %s", FB_SEM_NAME);
    }
    if (error == ENOENT) {
        Debug(0, "Insufficient memory: %s", FB_SEM_NAME);
    }
    return;
}

/*****************************************************************************/
/*! 
 NAME
	fbInit()
 DESCRIPTION
	Initialize Feedback Task
 RETURNS
 	OK or ERROR
*/
int fbInit(short inpcard, short inpsignal, short outcard, short outsignal, short inode, short onode, int rate, int priority)
{
#if __USE_FAST_VXWORKS_SYS_AUX_CLOCK__
    STATUS taskalive;
    int options, connected;
#endif
    tfbdrvset *fbdrvset;
    tfbdrvpar *fbdrvpar;
    int status = 0;
    sem_t *sstatus;

    if (pfbDrvDset == NULL) {
        Debug(0, "Feedback nodes not set pfbDrvDset = %p", pfbDrvDset);
        return ERROR;
    }
    if (pfbDrvDset->parameter == NULL) {
        Debug(0, "Feedback parameters not set pfbDrvDset->parameter = %p", pfbDrvDset->parameter);
        return ERROR;
    }
    if (pfbDrvDset->parameter->status == OK) {
        Debug(0, "Feedback module running = %d", pfbDrvDset->parameter->status);
        return ERROR;
    }
    if (inode > FB_MAX_NODES || inode < 0) {
        Debug(0, "ERROR: inode = %d > FB_MAX_NODES", inode);
        return ERROR;
    }
    if (onode > FB_MAX_NODES || onode < 0) {
        Debug(0, "ERROR: onode = %d > FB_MAX_NODES", onode);
        return ERROR;
    }
    if (rate > FB_MAX_RATE || rate < FB_MIN_RATE) {
        Debug(0, "Feedback rate out of range (%d,%d), rate = %d", FB_MIN_RATE, FB_MAX_RATE, rate);
        return ERROR;
    }
    if (priority > 254 || priority < 0) {
        Debug(0, "Priority out of range: priority = %d", priority);
        return ERROR;
    }
    fbdrvset = pfbDrvDset;
    fbdrvpar = pfbDrvDset->parameter;
    if (fb_initialized == 0) {
        fb_initialized = 1;
        /* Disable the timer */
        fbIntSigFlag = 0;
        sstatus = sem_open(FB_SEM_NAME, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);
        if (sstatus == SEM_FAILED) {
            Debug(1, "Could not creat semaphore: %s %p", FB_SEM_NAME, sstatus);
            if (errno == EEXIST) {
                Debug(1, "Semaphore already exists: %p", sstatus);
            } else
                return ERROR;
        }
        // Slow triggering or without hardware clock
        epicsThreadCreate("tFbTrigger",
                          epicsThreadPriorityHigh, epicsThreadGetStackSize(epicsThreadStackSmall), (EPICSTHREADFUNC) fbTriggerLoop, NULL);
#if __USE_FAST_VXWORKS_SYS_AUX_CLOCK__
        /* Check if Feedback is already running */
        if ((taskalive = taskIdVerify(fbIntTaskId)) == OK) {
            logMsg("Task ID %d already active (Feedback Task)\n", fbIntTaskId, 0, 0, 0, 0, 0);
            return (ERROR);
        }
        sysAuxClkDisable();
        fbSemBTimer = semBCreate(SEM_Q_FIFO, SEM_EMPTY);
        if (fb_common_vxfbtask == 1)
            options = VX_FP_TASK;
        else
            options = 0x80;
        if ((fbIntTaskId = taskSpawn("tFB", priority, options, 20000,
                                     (FUNCPTR) fbLoop, (int)fbdrvset, (int)fbdrvpar, 0, 0, 0, 0, 0, 0, 0, 0)) == ERROR)
            logMsg("ERROR while task spawn tFB(%d)\n", fbIntTaskId, 0, 0, 0, 0, 0);
        else
            logMsg("Task ID %d spawned (tFB)\n", fbIntTaskId, 0, 0, 0, 0, 0);
        logMsg("Setting sysAuxClk rate to %d Hz.\n", rate, 0, 0, 0, 0, 0);
        taskDelay(100);
        if (sysAuxClkRateSet(rate) == ERROR) {
            logMsg("ERROR while sysAuxClkRateSet(%d)\n", rate, 0, 0, 0, 0, 0);
            return (ERROR);
        }
        if ((connected = sysAuxClkConnect((FUNCPTR) fbauxclkhandler, 0)) == ERROR) {
            logMsg("ERROR while sysAuxClkConnect\n", 0, 0, 0, 0, 0, 0);
            return (ERROR);
        }
#else                           /* do not __USE_FAST_VXWORKS_SYS_AUX_CLOCK__ */
        fbSignal = epicsEventMustCreate(epicsEventEmpty);
        epicsThreadCreate("tFeedback", epicsThreadPriorityHigh, epicsThreadGetStackSize(epicsThreadStackBig), (EPICSTHREADFUNC) fbLoop, NULL);
#endif                          /* if  __USE_FAST_VXWORKS_SYS_AUX_CLOCK__ */
    }
    fbdrvpar->inpcard = inpcard;
    fbdrvpar->inpsignal = inpsignal;
    fbdrvpar->outcard = outcard;
    fbdrvpar->outsignal = outsignal;
    fbdrvpar->inode = inode;
    fbdrvpar->onode = onode;
    fbdrvpar->rate = rate;
    fbdrvpar->priority = priority;
    fbdrvpar->status = FB_INIT;
    if (fbdrvset->configure) {
        Debug(2, "Calling fbdrvset->configure(fbdrvpar->status = %d)", fbdrvpar->status);
        if ((status = (*fbdrvset->configure) (fbdrvpar)))
            return status;
    }
    if (fbdrvset->init) {
        Debug(2, "Calling fbdrvset->init(fbdrvpar->status = %d)", fbdrvpar->status);
        status = (*fbdrvset->init) (fbdrvpar);
        return status;
    }

    return OK;
}

int fbDeactivate(void)
{
    int status = OK;

    if (pfbDrvDset == NULL) {
        Debug(0, "Driver not set %p", pfbDrvDset);
        return OK;
    }
    if (pfbDrvDset->parameter == NULL) {
        Debug(0, "Driver parameter list not set %p", pfbDrvDset->parameter);
        return ERROR;
    }
    if (pfbDrvDset->parameter->status == OK) {
        Debug(0, "Feedback task running: pfbDrvDset->parameter->status = %d", pfbDrvDset->parameter->status);
        return ERROR;
    }
    if (pfbDrvDset->deactivate)
        status = pfbDrvDset->deactivate();
    if (status != ERROR) {
        pfbDrvDset = NULL;
    } else {
        return ERROR;
    }
    return OK;
}

int fbForceDeactivate(double timeout_sec)
{
    int waited = 0;
    const int sleep_ms = 10;

    if (pfbDrvDset == NULL)
        return OK;

    if (pfbDrvDset->parameter == NULL)
        return ERROR;

    /* Stop loop */
    fbIntSigFlag = 1;

    /* Wake blocked loop */
    fbTrigger();

    /* Wait until fbLoop() leaves active state */
    while (pfbDrvDset->parameter->status == OK) {

        epicsThreadSleep(sleep_ms / 1000.0);
        waited += sleep_ms;

        if ((waited / 1000.0) >= timeout_sec) {
            Debug(0, "Timeout waiting for feedback loop shutdown: %s", (pfbDrvDset->name != NULL) ? pfbDrvDset->name : "<unnamed>");
            return ERROR;
        }
    }

    /* Optional plugin-specific cleanup */
    if (pfbDrvDset->deactivate)
        pfbDrvDset->deactivate();

    Debug(1, "Forced deactivation of plugin %s", (pfbDrvDset->name != NULL) ? pfbDrvDset->name : "<unnamed>");

    pfbDrvDset = NULL;

    return OK;
}

int fbActivate(int id)
{
    int status = OK;
    tfbdrvset *pfset;
    if (id >= FB_MAX_MEMBERS || id < 0) {
        Debug(0, "Unvalid fb plugin id %d", id);
        return ERROR;
    }
    pfset = feedbackPluginList[id];

    if (pfset == NULL) {
        Debug(0,
              "Feedback support still active. "
              "Deactivate plugin first: %s (%p)", (pfbDrvDset->name != NULL) ? pfbDrvDset->name : "<unnamed>", pfbDrvDset);

        return ERROR;
    }
    if (pfbDrvDset != NULL) {
        Debug(0, "%p", pfbDrvDset);
        return ERROR;
    }
    if (pfset->activate)
        status = pfset->activate();
    if (status != ERROR) {
        pfbDrvDset = feedbackPluginList[id];
    } else {
        Debug(0, "ERROR: Cannot activate: pfset->activate() status = %d", status);
    }
    return OK;
}

/**
 * @brief fbTriggerLoop
 * Trigger running feedback task periodically. Delay is defined by epicsThreadSleepQuantum() * fbLngSoftTriggerDelay
 */
void fbTriggerLoop(void)
{
    int status = OK;
    double quantum = 2.0;

    for (; /*ever */ ;) {
        if (fbIntSoftTriggerDelay < 0) {
            Debug(5, "Wait for fbIntSoftTriggerDelay = %d >= 0", fbIntSoftTriggerDelay);
            epicsThreadSleep(0.5);
        } else {
            quantum = epicsThreadSleepQuantum();
            Debug(10, "fbTrigger() status = %d, fbIntSoftTriggerDelay=%d * quantum=%f", status, fbIntSoftTriggerDelay, quantum);
            epicsThreadSleep(quantum * (double)fbIntSoftTriggerDelay);
            status = fbTrigger();
        }
    }
    return;
}

/*****************************************************************************/
/* 
 NAME
	fbLoop()
 DESCRIPTION
	Infinite Loop, This task will never return!!!
*/
void fbLoop(void)
{
    /* Task Control */
    struct sigaction endTask;
    int status = OK;
    int current_rate = -1;
    sem_t *sem_des;
    int sval;                           /* Debug *sem_t */
    tfbdrvset *fbdrvset = pfbDrvDset;
    tfbdrvpar *fbdrvpar = pfbDrvDset->parameter;
#if __USE_FAST_VXWORKS_SYS_AUX_CLOCK__
    int priority = 150;
#endif

    sem_des = sem_open(FB_SEM_NAME, 0, 0, 0);
    if (sem_des == SEM_FAILED) {
        Debug(0, "Could not open semaphore %s", FB_SEM_NAME);
        fbPrintSemaphoreError(errno);
    }
    status = sem_getvalue(sem_des, &sval);
    Debug(2, "Open Semaphore (" FB_SEM_NAME "): \n\t\tsval = %d, \n\t\tstatus = %d", sval, status);
    endTask.sa_handler = fbsignalhandler;
    sigemptyset(&endTask.sa_mask);
    endTask.sa_flags = FB_NO_OPTIONS;
    if (sigaction(SIGABRT, &endTask, NULL) == ERROR) {
        Debug(0, "Could not install signal handler %p", fbsignalhandler);
        return;
    }
    status = sem_getvalue(sem_des, &sval);
    Debug(1, "Wait for fbOpen()\nsem_wait (" FB_SEM_NAME "): sval = %d, status = %d", sval, status);
    status = sem_wait(sem_des);
    status = sem_getvalue(sem_des, &sval);
    Debug(2, "Semaphore (" FB_SEM_NAME ") taken: \n\t\tsval = %d, \n\t\tstatus = %d", sval, status);
        /*-----------------------------------------------------------------------------------*/
    while (1 == 1) {
#if __USE_FAST_VXWORKS_SYS_AUX_CLOCK__
        /* Set new parameters */
        fbIntTaskId = taskIdSelf();
        status = taskPriorityGet(fbIntTaskId, &priority);
        if (priority != fbdrvpar->priority) {
            Debug(1, "Changing feedback priority to %d", fbdrvpar->priority);
            status = taskPrioritySet(fbIntTaskId, fbdrvpar->priority);
        }
        Debug(1, "Set Rate and enable sysAuxClk: rate: %d\n", current_rate);
        current_rate = sysAuxClkRateGet();
        if (fbdrvpar->rate != current_rate)
            status = sysAuxClkRateSet(fbdrvpar->rate);
#endif
        /* Enable clock or open otherwise */
        if (fbdrvset->open) {
            if ((*fbdrvset->open) (fbdrvpar) != OK)
                Debug(0, "ERROR in *fbdrvset->open(), rate=%d", current_rate);
        }
/** \todo: is this really necessary?
        else
			sysAuxClkEnable();
*/
        Debug(1, "Entering inner loop, status = %d", status);
        fbdrvpar->status = OK;
        /* Entering Inner Feedback Loop */
        status = fbInnerLoop(fbdrvset, fbdrvpar);
        fbdrvpar->status = ERROR;
#if __USE_FAST_VXWORKS_SYS_AUX_CLOCK__
        sysAuxClkDisable();
#endif
        Debug(1, "Exit inner Loop: sysAuxClk disabled, wait for semaphore, status = %d.", status);
        fbIntSigFlag = 0;
        /* Stop here until resume */
        if (fbdrvset->close) {
            status = (*fbdrvset->close) (fbdrvpar);
            if (status != OK)
                Debug(0, "ERROR in *fbdrvset->close() = %d", status);
        }
        /* Semaphore ------------------- < wait here > ------------------------------------------- */
        status = sem_getvalue(sem_des, &sval);
        Debug(2, "sem_wait (" FB_SEM_NAME "): \n\t\tsval = %d, \n\t\tstatus = %d", sval, status);
        status = sem_wait(sem_des);
        status = sem_getvalue(sem_des, &sval);
        Debug(2, "Semaphore (" FB_SEM_NAME ") taken: \n\t\tsval = %d, \n\t\tstatus = %d", sval, status);
        if (pfbDrvDset != NULL)
            fbdrvset = pfbDrvDset;
        if (pfbDrvDset != NULL)
            fbdrvpar = pfbDrvDset->parameter;
    }                                   /*infinite loop */
}                                       /* void fbLoop() */

/*****************************************************************************/
/* 
 NAME
	fbInnerLoop()
 DESCRIPTION
	Inner Feedback Loop
 RETURNS
 	OK or ERROR
*/
int fbInnerLoop(tfbdrvset * fbdrvset, tfbdrvpar * fbdrvpar)
{
    int status = 0;
    INTSUPFUN *InputNodeFunction;
    INTSUPFUN *OutputNodeFunction;
    INTSUPFUN inFunc;
    INTSUPFUN outFunc;

    if (fbdrvset == NULL) {
        return ERROR;
    }

    if (fbdrvpar == NULL) {
        return ERROR;
    }

    if (fbdrvpar->inode < 0 || fbdrvpar->inode > FB_MAX_NODES) {
        Debug(0, "fbInnerLoop: invalid inode=%d", fbdrvpar->inode);
        return ERROR;
    }

    if (fbdrvpar->onode < 0 || fbdrvpar->onode > FB_MAX_NODES) {
        Debug(0, "fbInnerLoop: invalid onode=%d", fbdrvpar->onode);
        return ERROR;
    }

    InputNodeFunction = (INTSUPFUN *) & (fbdrvset->input_node1);
    OutputNodeFunction = (INTSUPFUN *) & (fbdrvset->output_node1);

    inFunc = InputNodeFunction[fbdrvpar->inode];
    outFunc = OutputNodeFunction[fbdrvpar->onode];

    Debug(2, "fbInnerLoop: inode=%d onode=%d inFunc=%p outFunc=%p", fbdrvpar->inode, fbdrvpar->onode, inFunc, outFunc);

    if (inFunc == NULL) {
        Debug(0, "fbInnerLoop: NULL input callback inode=%d", fbdrvpar->inode);
        return ERROR;
    }

    if (outFunc == NULL) {
        Debug(0, "fbInnerLoop: NULL output callback onode=%d", fbdrvpar->onode);
        return ERROR;
    }

#if __USE_FAST_VXWORKS_SYS_AUX_CLOCK__
    semTake(fbSemBTimer, WAIT_FOREVER);
#else
    epicsEventMustWait(fbSignal);
#endif

    fbLngCtrCatcher = 0;

    while (status != ERROR && fbIntSigFlag == 0) {
#if __USE_FAST_VXWORKS_SYS_AUX_CLOCK__
        semTake(fbSemBTimer, WAIT_FOREVER);
#else
        epicsEventMustWait(fbSignal);
#endif

        fbLngCtrHandler++;

        status = inFunc(fbdrvpar);

        if (status == OK)
            status = outFunc(fbdrvpar);
    }

    Debug(1, "Suspending feedback task. " "fbLngCtrHandler=%ld status=%d", fbLngCtrHandler, status);

    return status;
}

/*****************************************************************************/
/* 
 NAME
	fbauxclkhandler()
 DESCRIPTION
	Interupt service routine for sysAuxClk.
*/
void fbauxclkhandler(int arg)
{                                       /* sysAuxClk interrupt handler code */
#if __USE_FAST_VXWORKS_SYS_AUX_CLOCK__
    semGive(fbSemBTimer);
#else
    epicsEventSignal(fbSignal);
#endif
}

/*****************************************************************************/
/*! 
 NAME
	fbsignalhandler()
 DESCRIPTION
	Interupt service routine for sysAuxClk.
*/
void fbsignalhandler(int signal)
{
#if __USE_FAST_VXWORKS_SYS_AUX_CLOCK__
    logMsg("fbsignalhandler: Abort signal received!\n", 0, 0, 0, 0, 0, 0);
#else
    Debug(0, "fbsignalhandler: Abort signal received! %d", signal)
#endif
        fbIntSigFlag = 1;
}

/*****************************************************************************/
/*! 
 NAME
	fbpr()
 DESCRIPTION
	Debugging Messages.
*/
int fbpr(void)
{
    sem_t *sem_des;
    int sval;
    int status;
    int i;

    tfbdrvset *fbdrvset;
    tfbdrvpar *fbdrvpar;

    printf("***********************************************************\n");
    printf("\t\tfbCore Module\n");
    printf("Built on " __DATE__ " at " __TIME__ "\n");
    printf("Changeset Id: %s\n", HGVERSION);
    printf("and date: %s\n", FB_TIMESTAMP);
    printf("***********************************************************\n");
    printf("Supported feeback rates from %d Hz to %d Hz\n", FB_MIN_RATE, FB_MAX_RATE);
    fbdrvset = pfbDrvDset;
    if (fbdrvset == NULL) {
        printf("No active feedback support module.\n");
        return OK;
    }
    fbdrvpar = pfbDrvDset->parameter;
    if (fbdrvpar == NULL) {
        printf("No active feedback support module parameter list.\n");
        return OK;
    }
    printf("Registered feedback devices:\n");
    for (i = 0; i < FB_MAX_MEMBERS; i++) {
        if (feedbackPluginList[i] != NULL) {
            if (feedbackPluginList[i]->name != NULL) {
                printf("\t%d: %s\n", i, feedbackPluginList[i]->name);
            }
        }
    }
    printf("Current Feedback Device: %s\n", fbdrvset->name);
    printf("Current Device Report:\n");
    if (fb_initialized == 0) {
        printf("Module not initialized.\n");
        return ERROR;
    }
    if (fbdrvset == NULL) {
        printf("Function set not available.\n");
        return ERROR;
    }
    if (fbdrvpar == NULL) {
        printf("Parameter set not available.\n");
        return ERROR;
    }
    if (fbdrvset->report) {
        status = (*fbdrvset->report) (fbdrvpar);
        if (status != OK) {
            Debug(0, "ERROR in *fbdrvset->report() = %p\n", fbdrvset->report);
        }
    }
    printf("********************\n");
    printf("Feedback Task:\n");
#if __USE_FAST_VXWORKS_SYS_AUX_CLOCK__
    printf("\tfbIntTaskId = %d\n", fbIntTaskId);
#endif
    printf("\tfbIntSigFlag = %d\n", fbIntSigFlag);
    printf("\tfbLngCtrHandler = %ld\n", fbLngCtrHandler);
    printf("\tfbLngCtrCatcher = %ld\n", fbLngCtrCatcher);
    printf("\tfbIntSoftTriggerDelay = %d\n", fbIntSoftTriggerDelay);
    printf("\tfb_common_debug = %d\n", fb_common_debug);
    printf("\tfb_initialized = %d\n", fb_initialized);
    sem_des = sem_open(FB_SEM_NAME, 0, 0, 0);
    status = sem_getvalue(sem_des, &sval);
    printf("Semaphore FB_SEM_NAME: \n\t\tsval = %d, \n\t\tstatus = %d\n", sval, status);
    status = sem_close(sem_des);
    return status;
}

/*****************************************************************************/
/*! 
 NAME
	fbClose()
 DESCRIPTION
	Stop Feedback Loop Task.
*/
int fbClose(void)
{
    /* XXX task crashed hier: 
       if (kill(fbIntTaskId, SIGABRT) == ERROR)
       {
       logMsg("Task ID invalid.\n",0,0,0,0,0,0);
       return (ERROR);
       }
     */
    /* XXX  workaround: */
    fbIntSigFlag = 1;
    return (OK);
}

/*****************************************************************************/
/*! 
 NAME
	fbOpen()
 DESCRIPTION
	Start Feedback.
*/
int fbOpen(void)
{
    int sval;
    int status;
    sem_t *sem_des;
    if (pfbDrvDset == NULL)
        return (ERROR);
    if (pfbDrvDset->parameter == NULL)
        return (ERROR);

    sem_des = sem_open(FB_SEM_NAME, 0, 0, 0);
    if (sem_des == SEM_FAILED) {
        Debug(0, "Could not open semaphore %s", FB_SEM_NAME);
        fbPrintSemaphoreError(errno);
    }
    status = sem_getvalue(sem_des, &sval);
    Debug(2, "Semaphore : \n\t\tsval = %d, \n\t\tstatus = %d\n", sval, status);
    status = sem_post(sem_des);
    Debug(2, "Start Feedback Task, status = %d", status);
    status = sem_close(sem_des);
    return (status);
}

/*****************************************************************************/
/*! 
 NAME
	fbConfigure()
 DESCRIPTION
	Configure Feedback Task and call fbdrvset->configure() before and after parametrisation 
*/
int fbConfigure(short inpcard, short inpsignal, short outcard, short outsignal, short inode, short onode, int rate, int priority)
{
    int status = ERROR;
    tfbdrvset *fbdrvset;
    tfbdrvpar *pfbdrvpar;
    tfbdrvpar fbdrvpar;

    if (pfbDrvDset == NULL)
        return (ERROR);
    if (pfbDrvDset->parameter == NULL)
        return (ERROR);
    if (inode > FB_MAX_NODES || inode < 0)
        return (ERROR);
    if (onode > FB_MAX_NODES || onode < 0)
        return (ERROR);
    if (rate > FB_MAX_RATE || rate < FB_MIN_RATE)
        return (ERROR);
    if (priority > 254 || priority < 0)
        return (ERROR);
    /* Function Set */
    fbdrvset = pfbDrvDset;
    if (fbdrvset->configure) {
        /* Parameters that have to be verified */
        pfbdrvpar = &fbdrvpar;
        pfbdrvpar->inpcard = inpcard;
        pfbdrvpar->inpsignal = inpsignal;
        pfbdrvpar->outcard = outcard;
        pfbdrvpar->outsignal = outsignal;
        pfbdrvpar->inode = inode;
        pfbdrvpar->onode = onode;
        pfbdrvpar->rate = rate;
        pfbdrvpar->priority = priority;
        pfbdrvpar->status = FB_PRECONFIGURE;    /* call configure(status = before) */
        if ((status = (*fbdrvset->configure) (pfbdrvpar)))
            return (status);
        inode = pfbdrvpar->inode;
        onode = pfbdrvpar->onode;
    }
    /* Set global parameters */
    pfbdrvpar = pfbDrvDset->parameter;
    pfbdrvpar->inpcard = inpcard;
    pfbdrvpar->inpsignal = inpsignal;
    pfbdrvpar->outcard = outcard;
    pfbdrvpar->outsignal = outsignal;
    pfbdrvpar->inode = inode;
    pfbdrvpar->onode = onode;
    pfbdrvpar->rate = rate;
    pfbdrvpar->priority = priority;
    pfbdrvpar->status = FB_CONFIGURE;   /* call configure(status = after) */
    if (fbdrvset->configure) {
        if ((status |= (*fbdrvset->configure) (pfbdrvpar)))
            return (status);
    }
    return (status);
}

/*****************************************************************************/
/*! 
 NAME
	fbGetNodes()
 DESCRIPTION
	Change nodes and do not call fbdrvset->configure()
*/
int fbGetNodes(short *inode, short *onode)
{
    INTSUPFUN *InputNodeFunction;
    INTSUPFUN *OutputNodeFunction;
    tfbdrvset *pfbdrvset;
    tfbdrvpar *pfbdrvpar;

    if (pfbDrvDset == NULL)
        return (ERROR);
    if (pfbDrvDset->parameter == NULL)
        return (ERROR);
    pfbdrvset = pfbDrvDset;
    pfbdrvpar = pfbDrvDset->parameter;
    InputNodeFunction = (INTSUPFUN *) & (pfbdrvset->input_node1);
    OutputNodeFunction = (INTSUPFUN *) & (pfbdrvset->output_node1);
    *inode = pfbdrvpar->inode;
    *onode = pfbdrvpar->onode;
    if (*inode > FB_MAX_NODES || *inode < 0)
        return (ERROR);
    if (*onode > FB_MAX_NODES || *onode < 0)
        return (ERROR);
    if (!InputNodeFunction[*inode])
        return (ERROR);
    if (!OutputNodeFunction[*onode])
        return (ERROR);
    return (OK);
}

int fbGetNodeName(int index, char *name, int length)
{

    if (name == NULL)
        return ERROR;
    if (pfbDrvDset == NULL)
        return ERROR;
    if (index < 0 || index > 15)
        return ERROR;
    if (pfbDrvDset->nodenames[index] == NULL) {
        name[0] = '\0';
        return OK;
    }
    strncpy(name, pfbDrvDset->nodenames[index], length);
    name[length] = '\0';
    return OK;
}

/*****************************************************************************/
/*! 
 NAME
	fbSetNodes()
 DESCRIPTION
	Change feedback nodes but do NOT call fbdrvset->configure()
*/
int fbSetNodes(short inode, short onode)
{
    int status = OK;
    tfbdrvset *fbdrvset;
    tfbdrvpar *pfbdrvpar;
    tfbdrvpar fbdrvpar;

    if (pfbDrvDset == NULL)
        return (ERROR);
    if (pfbDrvDset->parameter == NULL)
        return (ERROR);
    if (inode > FB_MAX_NODES || inode < 0)
        return (ERROR);
    if (onode > FB_MAX_NODES || onode < 0)
        return (ERROR);
    /* Function Set */
    fbdrvset = pfbDrvDset;
    fbdrvpar = *pfbDrvDset->parameter;
    if (fbdrvset->configure) {
        /* Parameters that have to be verified */
        Debug(5, "Verify Nodes: inode = %d\t onode = %d\t", inode, onode);
        pfbdrvpar = &fbdrvpar;
        pfbdrvpar->inode = inode;
        pfbdrvpar->onode = onode;
        pfbdrvpar->status = FB_PRECONFIGURE;    /* call configure(status = before) */
        if ((status = (*fbdrvset->configure) (pfbdrvpar)))
            return (status);
        inode = pfbdrvpar->inode;
        onode = pfbdrvpar->onode;
    }
    /* Set global parameters */
    Debug(2, "Setting Nodes: inode = %d\t onode = %d\t", inode, onode);
    pfbdrvpar = pfbDrvDset->parameter;
    pfbdrvpar->inode = inode;
    pfbdrvpar->onode = onode;
    pfbdrvpar->status = FB_CONFIGURE;   /* call configure(status = after) */
    if (fbdrvset->configure) {
        if ((status = (*fbdrvset->configure) (pfbdrvpar)))
            return (status);
    }
    return (status);
}

/*****************************************************************************/
/*! 
 NAME
	fbHome()
 DESCRIPTION
	Set pfbdrvpar->flags = flags plus call *pfbdrvset->home() if exists.
*/
int fbHome(unsigned int flags)
{
    int status;
    tfbdrvpar *pfbdrvpar;
    tfbdrvset *pfbdrvset;
    pfbdrvset = pfbDrvDset;
    pfbdrvpar = pfbDrvDset->parameter;
    pfbdrvpar->flags = flags;

    if (pfbDrvDset == NULL)
        return (ERROR);
    if (pfbDrvDset->parameter == NULL)
        return (ERROR);
    if (pfbdrvset->home) {
        if ((status = (*pfbdrvset->home) (pfbdrvpar)))
            return (status);
    }
    return (OK);
}

/*****************************************************************************/
/*! 
 NAME
	fbGetFlags()
 DESCRIPTION
	Set pfbdrvpar->flags = flags plus call *pfbdrvset->home() if exists.
*/
int fbGetFlags(unsigned int *flags)
{
    if (pfbDrvDset == NULL)
        return ERROR;
    if (pfbDrvDset->parameter == NULL)
        return ERROR;
    *flags = pfbDrvDset->parameter->flags;
    return OK;
}

/*****************************************************************************/
/*! 
 NAME
	fbSetFlags()
 DESCRIPTION
	Set pfbdrvpar->flags = flags plus call *pfbdrvset->home() if exists.
*/
int fbSetFlags(unsigned int flags)
{
    tfbdrvpar *pfbdrvpar;

    if (pfbDrvDset == NULL)
        return ERROR;
    pfbdrvpar = pfbDrvDset->parameter;
    if (pfbdrvpar == NULL)
        return (ERROR);
    pfbdrvpar->flags = flags;
    return (OK);
}

/*****************************************************************************/
/*! 
 NAME
	fbGetStatus()
 DESCRIPTION
	-1 = DISABLED, 0 = ENABLED
*/
int fbGetStatus(int *status)
{
    if (pfbDrvDset == NULL)
        return ERROR;
    if (pfbDrvDset->parameter == NULL)
        return (ERROR);
    *status = pfbDrvDset->parameter->status;
    return (OK);
}

/*****************************************************************************/
/*! 
 *
 */
int fbGetRate(int *rate)
{
    if (pfbDrvDset == NULL)
        return ERROR;
    if (pfbDrvDset->parameter == NULL)
        return (ERROR);
    *rate = pfbDrvDset->parameter->rate;
    return (OK);
}

/*****************************************************************************/
/*! 
 *
 */
int fbSetRate(int rate)
{
    tfbdrvpar *pfbdrvpar;

    if (pfbDrvDset == NULL)
        return ERROR;
    pfbdrvpar = pfbDrvDset->parameter;
    if (pfbdrvpar == NULL) {
        return (ERROR);
    }
    if (rate > FB_MAX_RATE || rate < FB_MIN_RATE) {
        Debug(2, "Rate out of range! %d < RATE < %d", FB_MIN_RATE, FB_MAX_RATE);
        return (ERROR);
    }
    pfbdrvpar->rate = rate;
    return (OK);
}

int fbSetSoftTriggerRate(double rate)
{
    double t;
    int qs;

    if (rate == 0.0) {
        Debug(0, "ERROR: rate = %f", rate);
        fbIntSoftTriggerDelay = -1;
        return ERROR;
    }
    t = 1.0 / rate;
    qs = (int)(t / epicsThreadSleepQuantum());
    if (qs > 0) {
        fbIntSoftTriggerDelay = qs;
    } else {
        fbIntSoftTriggerDelay = 1;
    }
    return OK;
}

int fbGetSoftTriggerRate(double *rate)
{
    double t;

    t = fbIntSoftTriggerDelay * epicsThreadSleepQuantum();
    *rate = 1.0 / t;
    return OK;
}

/*****************************************************************************/
/*! 
 *
 */
int fbGetPriority(int *priority)
{
    if (pfbDrvDset == NULL)
        return ERROR;
    if (pfbDrvDset->parameter == NULL)
        return (ERROR);
    *priority = pfbDrvDset->parameter->priority;
    return (OK);
}

/*****************************************************************************/
/*! 
 *
 */
int fbSetPriority(int priority)
{
    tfbdrvpar *pfbdrvpar;

    if (pfbDrvDset == NULL)
        return ERROR;
    pfbdrvpar = pfbDrvDset->parameter;
    if (pfbdrvpar == NULL)
        return (ERROR);
#if __USE_FAST_VXWORKS_SYS_AUX_CLOCK__
    if (priority > 254 || priority < 0) {
        Debug(2, "Priority out of range! %d < PRIORITY < %d", 254, 0);
        return ERROR;
    }
    pfbdrvpar->priority = priority;
    return OK;
#else
    return ERROR;
#endif                          /* if __USE_FAST_VXWORKS_SYS_AUX_CLOCK__ */
}

/*****************************************************************************/
/*! 
 NAME
	fbTrigger()
 DESCRIPTION
	Interupt service routine for sysAuxClk.
    The feedback support module can prevent fbTrigger() to give signals by <br>
    pfbDrvDset->parameter.status to FB_NOT_READY
*/
int fbTrigger(void)
{                                       /* like sysAuxClk interrupt handler code */
    tfbdrvpar *pfbdrvpar;

    if (pfbDrvDset == NULL)
        return ERROR;
    pfbdrvpar = pfbDrvDset->parameter;
    if (pfbdrvpar == NULL)
        return (ERROR);
    if (pfbdrvpar->status != FB_NOT_READY) {
#if __USE_FAST_VXWORKS_SYS_AUX_CLOCK__
        semGive(fbSemBTimer);
#else
        epicsEventSignal(fbSignal);
#endif
        return OK;
    } else
        return ERROR;
}
