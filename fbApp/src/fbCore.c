// SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin für Materialien und Energie GmbH
// SPDX-License-Identifier: MIT

/*! \file
 * \brief Feedback Support API
 * */
#if defined(__linux__) || defined(linux)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#endif

#if defined(vxWorks)
#define FB_OS_VXWORKS 1
#else
#define FB_OS_VXWORKS 0
#endif

#if !FB_OS_VXWORKS && (defined(__linux__) || defined(linux))
#define FB_OS_LINUX 1
#else
#define FB_OS_LINUX 0
#endif

#if FB_OS_VXWORKS
#include <taskLib.h>
#include <logLib.h>
#include <sysLib.h>
#include <intLib.h>
#include <ioLib.h>
#include <sigLib.h>
#include <timers.h>
#else
#include <epicsEvent.h>
#endif
#if FB_OS_LINUX
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#endif
#include <epicsMutex.h>
#include <epicsThread.h>
#include <dbScan.h>
#include <string.h>
#include <epicsStdioRedirect.h>
#include <errno.h>
#include <signal.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <epicsExport.h>
#include "fbCore.h"

/*! Value returned if `sem_open' failed. */
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
int fbInnerLoop(tfbdrvset *fbdrvset, tfbdrvpar *fbdrvpar);
void fbTriggerLoop(void);
void fbauxclkhandler(int arg);
void fbsignalhandler(int signal);

static int fbWaitForNextCycle(tfbdrvpar *fbdrvpar);
static int fbPrepareTriggerSource(tfbdrvpar *fbdrvpar);
static void fbStopTriggerSource(void);
#if FB_OS_LINUX
static int fbLinuxTimerStart(int rate);
static int fbLinuxTimerSetRate(int rate);
static int fbLinuxTimerWait(void);
static int fbLinuxTimerRequestStop(void);
static void fbLinuxTimerClose(void);
#endif
static void fbSignalFeedbackCycle(void);
static void fbTriggerConfigLockInit(void);
static void fbGetTriggerConfiguration(int *mode, double *softRate);

int fb_common_debug = 0;
static int fb_initialized = 0;
epicsExportAddress(int, fb_common_debug);
int fb_common_vxfbtask = 1;
epicsExportAddress(int, fb_common_vxfbtask);
#if FB_OS_VXWORKS
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

static epicsMutexId fbTriggerConfigLock = NULL;
static volatile int fbCurrentTriggerMode = FB_TRIGGER_MODE_HARDWARE;
static volatile int fbLoopRequested = 0;
static volatile int fbLoopRunning = 0;
static double fbSoftTriggerRequestedRate = 0.0;
static double fbSoftTriggerEffectiveRate = 0.0;
static unsigned long fbSoftTriggerOverruns = 0ul;
static unsigned long fbHardwareTriggerOverruns = 0ul;

#if FB_OS_LINUX
typedef struct fbLinuxTimerState {
    int timerFd;
    int stopFd;
    int rate;
} fbLinuxTimerState;

typedef struct fbLinuxSoftClockState {
    struct timespec next;
    double rate;
    int initialized;
} fbLinuxSoftClockState;

static fbLinuxTimerState fbLinuxTimer = { -1, -1, 0 };
static fbLinuxSoftClockState fbLinuxSoftClock;
#endif

/* defined in support module: */
tfbdrvset *feedbackPluginList[FB_MAX_MEMBERS] = { 0 };
tfbdrvset *pfbDrvDset = NULL;

static void fbTriggerConfigLockInit(void)
{
    if (fbTriggerConfigLock == NULL)
        fbTriggerConfigLock = epicsMutexMustCreate();
}

static void fbGetTriggerConfiguration(int *mode, double *softRate)
{
    fbTriggerConfigLockInit();
    epicsMutexLock(fbTriggerConfigLock);
    if (mode != NULL)
        *mode = fbCurrentTriggerMode;
    if (softRate != NULL)
        *softRate = fbSoftTriggerEffectiveRate;
    epicsMutexUnlock(fbTriggerConfigLock);
}

const char *fbGetTriggerModeName(int mode)
{
    switch (mode) {
    case FB_TRIGGER_MODE_HARDWARE:
        return "Hardware";
    case FB_TRIGGER_MODE_SOFTWARE:
        return "Software";
    case FB_TRIGGER_MODE_MANUAL:
        return "Manual";
    default:
        return "Unknown";
    }
}

#if FB_OS_LINUX
/*!
 * First compares the seconds, if needed compare the nanoseconds.
 */
static int fbTimespecCompare(const struct timespec *left, const struct timespec *right)
{
    if (left->tv_sec < right->tv_sec)
        return -1;
    if (left->tv_sec > right->tv_sec)
        return 1;
    if (left->tv_nsec < right->tv_nsec)
        return -1;
    if (left->tv_nsec > right->tv_nsec)
        return 1;
    return 0;
}

static void fbTimespecAddPeriod(struct timespec *value, double rate)
{
    double period;
    long seconds;
    long nanoseconds;

    period = 1.0 / rate;
    seconds = (long) period;
    nanoseconds = (long) ((period - (double) seconds) * 1000000000.0 + 0.5);

    value->tv_sec += seconds;
    value->tv_nsec += nanoseconds;
    /* On slow rates normalize from huge ns to sec */
    while (value->tv_nsec >= 1000000000L) {
        value->tv_nsec -= 1000000000L;
        value->tv_sec++;
    }
}

/*!
 * Convert rate to period
 */
static int fbLinuxRateToTimespec(double rate, struct timespec *period)
{
    double secondsValue;
    long nanoseconds;

    if (period == NULL || rate <= 0.0)
        return ERROR;

    secondsValue = 1.0 / rate;
    period->tv_sec = (time_t) secondsValue;
    nanoseconds = (long)((secondsValue - (double) period->tv_sec) * 1000000000.0 + 0.5);

    if (nanoseconds >= 1000000000L) {
        period->tv_sec++;
        nanoseconds -= 1000000000L;
    }
    if (period->tv_sec == 0 && nanoseconds < 1L)
        nanoseconds = 1L;

    period->tv_nsec = nanoseconds;
    return OK;
}

/*
 * Configure timer
 */
static int fbLinuxTimerArmLocked(int rate)
{
    struct itimerspec timerSpec;
    struct timespec now;
    struct timespec period;

    if (fbLinuxTimer.timerFd < 0) {
        Debug(0, "Linux hardware timer cannot be armed: invalid timerfd=%d", fbLinuxTimer.timerFd);
        return ERROR;
    }
    if (rate < FB_MIN_RATE || rate > FB_MAX_RATE) {
        Debug(0, "Linux hardware timer rate out of range: rate=%d valid=%d..%d Hz", rate, FB_MIN_RATE, FB_MAX_RATE);
        return ERROR;
    }

    if (fbLinuxRateToTimespec((double)rate, &period) != OK) {
        Debug(0, "Linux hardware timer cannot convert rate=%d Hz to period", rate);
        return ERROR;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        Debug(0, "Linux hardware timer clock_gettime(CLOCK_MONOTONIC) failed: errno=%d (%s)", errno, strerror(errno));
        return ERROR;
    }

    memset(&timerSpec, 0, sizeof(timerSpec));
    timerSpec.it_interval = period;
    timerSpec.it_value = now;
    timerSpec.it_value.tv_sec += period.tv_sec;
    timerSpec.it_value.tv_nsec += period.tv_nsec;
    while (timerSpec.it_value.tv_nsec >= 1000000000L) {
        timerSpec.it_value.tv_nsec -= 1000000000L;
        timerSpec.it_value.tv_sec++;
    }

    if (timerfd_settime(fbLinuxTimer.timerFd,
                        TFD_TIMER_ABSTIME,
                        &timerSpec,
                        NULL) != 0) {
        Debug(0, "Linux hardware timer timerfd_settime failed: timerfd=%d rate=%d errno=%d (%s)", fbLinuxTimer.timerFd, rate, errno, strerror(errno));
        return ERROR;
    }

    fbLinuxTimer.rate = rate;
    Debug(2, "Linux hardware timer armed: timerfd=%d rate=%d Hz period=%ld.%09ld s first=%ld.%09ld",
          fbLinuxTimer.timerFd, rate, (long)period.tv_sec, period.tv_nsec, (long)timerSpec.it_value.tv_sec, timerSpec.it_value.tv_nsec);
    return OK;
}

/*!
 * Close and release resources
 */
static void fbLinuxTimerCloseLocked(void)
{
    int closeStatus;

    if (fbLinuxTimer.timerFd >= 0 || fbLinuxTimer.stopFd >= 0) {
        Debug(2, "Closing Linux hardware timer backend: timerfd=%d stopfd=%d rate=%d Hz",
              fbLinuxTimer.timerFd, fbLinuxTimer.stopFd, fbLinuxTimer.rate);
    }

    if (fbLinuxTimer.timerFd >= 0) {
        closeStatus = close(fbLinuxTimer.timerFd);
        if (closeStatus != 0) {
            Debug(0, "Closing Linux hardware timerfd=%d failed: errno=%d (%s)",
                  fbLinuxTimer.timerFd, errno, strerror(errno));
        }
        fbLinuxTimer.timerFd = -1;
    }
    if (fbLinuxTimer.stopFd >= 0) {
        closeStatus = close(fbLinuxTimer.stopFd);
        if (closeStatus != 0) {
            Debug(0, "Closing Linux hardware stop eventfd=%d failed: errno=%d (%s)",
                  fbLinuxTimer.stopFd, errno, strerror(errno));
        }
        fbLinuxTimer.stopFd = -1;
    }
    fbLinuxTimer.rate = 0;
}

/*!
 * Create, initialize, and arm Linux timerfd hardware trigger
 */
static int fbLinuxTimerStart(int rate)
{
    int timerFd;
    int stopFd;
    int status;

    Debug(2, "Starting Linux hardware timer backend at %d Hz", rate);

    timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (timerFd < 0) {
        Debug(0,
              "Linux hardware timer timerfd_create failed: errno=%d (%s)",
              errno, strerror(errno));
        return ERROR;
    }

    stopFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (stopFd < 0) {
        Debug(0, "Linux hardware timer eventfd creation failed: errno=%d (%s)", errno, strerror(errno));
        close(timerFd);
        return ERROR;
    }
    Debug(3, "Linux hardware timer descriptors created: timerfd=%d stopfd=%d", timerFd, stopFd);

    fbTriggerConfigLockInit();
    epicsMutexLock(fbTriggerConfigLock);
    fbLinuxTimerCloseLocked();
    fbLinuxTimer.timerFd = timerFd;
    fbLinuxTimer.stopFd = stopFd;
    fbHardwareTriggerOverruns = 0ul;
    status = fbLinuxTimerArmLocked(rate);
    if (status != OK)
        fbLinuxTimerCloseLocked();
    epicsMutexUnlock(fbTriggerConfigLock);

    if (status == OK) {
        Debug(1, "Linux hardware timer backend active at %d Hz", rate);
    } else {
        Debug(0, "Linux hardware timer backend failed to start at %d Hz", rate);
    }

    return status;
}

static int fbLinuxTimerSetRate(int rate)
{
    int status;
#ifndef NODEBUG
    int oldRate;
#endif

    fbTriggerConfigLockInit();
    epicsMutexLock(fbTriggerConfigLock);
#ifndef NODEBUG
    oldRate = fbLinuxTimer.rate;
#endif
    status = fbLinuxTimerArmLocked(rate);
    if (status == OK)
        fbHardwareTriggerOverruns = 0ul;
    epicsMutexUnlock(fbTriggerConfigLock);

    if (status == OK) {
        Debug(1,
              "Linux hardware timer rate changed: old=%d Hz new=%d Hz; overrun counter reset",
              oldRate, rate);
    } else {
        Debug(0,
              "Linux hardware timer rate change failed: old=%d Hz requested=%d Hz",
              oldRate, rate);
    }
    return status;
}

static int fbLinuxTimerWait(void)
{
    struct pollfd fds[2];
    uint64_t expirations;
    uint64_t stopValue;
    ssize_t count;
    int timerFd;
    int stopFd;
    int status;
#ifndef NODEBUG
    unsigned long totalOverruns;
#endif

    fbTriggerConfigLockInit();
    epicsMutexLock(fbTriggerConfigLock);
    timerFd = fbLinuxTimer.timerFd;
    stopFd = fbLinuxTimer.stopFd;
    epicsMutexUnlock(fbTriggerConfigLock);

    if (timerFd < 0 || stopFd < 0) {
        Debug(0,
              "Linux hardware timer wait called with invalid descriptors: timerfd=%d stopfd=%d",
              timerFd, stopFd);
        return ERROR;
    }

    memset(fds, 0, sizeof(fds));
    // eventfd  (stop requests)
    fds[0].fd = timerFd;
    fds[0].events = POLLIN;
    // timerfd  (hardware timer ticks)
    fds[1].fd = stopFd;
    fds[1].events = POLLIN;

    do {
        status = poll(fds, 2, -1);
    } while (status < 0 && errno == EINTR && fbIntSigFlag == 0);

    if (fbIntSigFlag != 0) {
        Debug(2, "%s", "Linux hardware timer wait interrupted by shutdown flag");
        return ERROR;
    }
    if (status < 0) {
        Debug(0, "Linux hardware timer poll failed: timerfd=%d stopfd=%d errno=%d (%s)", timerFd, stopFd, errno, strerror(errno));
        return ERROR;
    }
    /* handle stop request */
    if ((fds[1].revents & POLLIN) != 0) {
        do {
            count = read(stopFd, &stopValue, sizeof(stopValue));
        } while (count < 0 && errno == EINTR);
        if (count < 0) {
            Debug(0, "Linux hardware timer stop event read failed: stopfd=%d count=%ld errno=%d (%s)", stopFd, (long)count, errno, strerror(errno));
        } else if (count != (ssize_t)sizeof(stopValue)) {
            Debug(0, "Linux hardware timer stop event short read: stopfd=%d count=%ld expected=%lu", stopFd, (long)count, (unsigned long)sizeof(stopValue));
        } else {
            Debug(2, "Linux hardware timer stop event received: stopfd=%d value=%lu", stopFd, (unsigned long)stopValue);
        }
        return ERROR;
    }
    // check 
    if ((fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        Debug(0, "Linux hardware timer stop eventfd poll error: stopfd=%d revents=0x%x", stopFd, (unsigned int)fds[1].revents);
        return ERROR;
    }
    // check 
    if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        Debug(0, "Linux hardware timerfd poll error: timerfd=%d revents=0x%x", timerFd, (unsigned int)fds[0].revents);
        return ERROR;
    }
    // done
    if ((fds[0].revents & POLLIN) == 0) {
        Debug(0, "Linux hardware timer poll returned without timer event: timerfd=%d revents=0x%x stopRevents=0x%x",
              timerFd, (unsigned int)fds[0].revents, (unsigned int)fds[1].revents);
        return ERROR;
    }
    
    // POSIX pattern to restart after signal interrupt (count = -1)
    do {
        count = read(timerFd, &expirations, sizeof(expirations));
    } while (count < 0 && errno == EINTR && fbIntSigFlag == 0);

    if (fbIntSigFlag != 0) {
        Debug(2, "%s", "Linux hardware timer expiration ignored during shutdown");
        return ERROR;
    }
    if (count < 0) {
        Debug(0, "Linux hardware timerfd read failed: timerfd=%d count=%ld errno=%d (%s)", timerFd, (long)count, errno, strerror(errno));
        return ERROR;
    }
    // double check if result fits 
    if (count != (ssize_t)sizeof(expirations)) {
        Debug(0, "Linux hardware timerfd short read: timerfd=%d count=%ld expected=%lu", timerFd, (long)count, (unsigned long)sizeof(expirations));
        return ERROR;
    }
    if (expirations == 0u) {
        Debug(0, "Linux hardware timerfd returned zero expirations: timerfd=%d", timerFd);
        return ERROR;
    }

    if (expirations > 1u) {
        fbTriggerConfigLockInit();
        epicsMutexLock(fbTriggerConfigLock);
        fbHardwareTriggerOverruns += (unsigned long)(expirations - 1u);
#ifndef NODEBUG
        totalOverruns = fbHardwareTriggerOverruns;
#endif
        epicsMutexUnlock(fbTriggerConfigLock);
        Debug(1, "Linux hardware timer overrun: expirations=%lu missed=%lu total=%lu", (unsigned long)expirations, (unsigned long)(expirations - 1u), totalOverruns);
    }

    return OK;
}

static int fbLinuxTimerRequestStop(void)
{
    uint64_t value;
    ssize_t count;
    int stopFd;

    value = 1u;
    fbTriggerConfigLockInit();
    epicsMutexLock(fbTriggerConfigLock);
    stopFd = fbLinuxTimer.stopFd;
    epicsMutexUnlock(fbTriggerConfigLock);

    if (stopFd < 0) {
        Debug(1, "%s",
              "Linux hardware timer stop requested while backend is inactive");
        return ERROR;
    }

    do {
        count = write(stopFd, &value, sizeof(value));
    } while (count < 0 && errno == EINTR);

    if (count == (ssize_t)sizeof(value)) {
        Debug(2, "Linux hardware timer stop requested: stopfd=%d", stopFd);
        return OK;
    }
    if (count < 0 && errno == EAGAIN) {
        Debug(3, "Linux hardware timer stop event already pending: stopfd=%d", stopFd);
        return OK;
    }

    if (count < 0) {
        Debug(0, "Linux hardware timer stop request failed: stopfd=%d errno=%d (%s)", stopFd, errno, strerror(errno));
    } else {
        Debug(0, "Linux hardware timer stop request whatever unexpected: stopfd=%d count=%ld expected=%lu",
              stopFd, (long)count, (unsigned long)sizeof(value));
    }
    return ERROR;
}

static void fbLinuxTimerClose(void)
{
    fbTriggerConfigLockInit();
    epicsMutexLock(fbTriggerConfigLock);
    fbLinuxTimerCloseLocked();
    epicsMutexUnlock(fbTriggerConfigLock);
}

static int fbLinuxAbsoluteWait(double rate)
{
    struct timespec now;
    int status;

    if (rate <= 0.0)
        return ERROR;

    if (!fbLinuxSoftClock.initialized || fbLinuxSoftClock.rate != rate) {
        if (clock_gettime(CLOCK_MONOTONIC, &fbLinuxSoftClock.next) != 0)
            return ERROR;
        fbLinuxSoftClock.rate = rate;
        fbLinuxSoftClock.initialized = 1;
        fbTimespecAddPeriod(&fbLinuxSoftClock.next, rate);
    }

    do {
        status = clock_nanosleep(CLOCK_MONOTONIC,
                                 TIMER_ABSTIME,
                                 &fbLinuxSoftClock.next,
                                 NULL);
    } while (status == EINTR && fbIntSigFlag == 0);

    if (status != 0 || fbIntSigFlag != 0)
        return ERROR;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return ERROR;

    fbTimespecAddPeriod(&fbLinuxSoftClock.next, rate);
    while (fbTimespecCompare(&fbLinuxSoftClock.next, &now) <= 0) {
        fbTimespecAddPeriod(&fbLinuxSoftClock.next, rate);
        fbTriggerConfigLockInit();
        epicsMutexLock(fbTriggerConfigLock);
        fbSoftTriggerOverruns++;
        epicsMutexUnlock(fbTriggerConfigLock);
    }

    return OK;
}
#endif

static void fbSignalFeedbackCycle(void)
{
#if FB_OS_VXWORKS
    semGive(fbSemBTimer);
#else
    epicsEventSignal(fbSignal);
#endif
}

static int fbWaitForNextCycle(tfbdrvpar *fbdrvpar)
{
#if FB_OS_LINUX
    int mode;
    double softRate;
#endif

    if (fbdrvpar == NULL)
        return ERROR;

#if FB_OS_VXWORKS
    if (semTake(fbSemBTimer, WAIT_FOREVER) != OK)
        return ERROR;
    return OK;
#elif FB_OS_LINUX
    fbGetTriggerConfiguration(&mode, &softRate);
    if (mode == FB_TRIGGER_MODE_HARDWARE)
        return fbLinuxTimerWait();
    if (mode == FB_TRIGGER_MODE_SOFTWARE)
        return fbLinuxAbsoluteWait(softRate);
    epicsEventMustWait(fbSignal);
    return OK;
#else
    epicsEventMustWait(fbSignal);
    return OK;
#endif
}

static int fbPrepareTriggerSource(tfbdrvpar *fbdrvpar)
{
    int mode;
    double softRate;

    if (fbdrvpar == NULL)
        return ERROR;

    fbGetTriggerConfiguration(&mode, &softRate);

#if FB_OS_VXWORKS
    sysAuxClkDisable();
    while (semTake(fbSemBTimer, NO_WAIT) == OK) {
        /* Drain a stale binary-semaphore token before starting. */
    }

    if (mode == FB_TRIGGER_MODE_HARDWARE) {
        if (sysAuxClkRateSet(fbdrvpar->rate) == ERROR)
            return ERROR;
        sysAuxClkEnable();
    } else if (mode == FB_TRIGGER_MODE_SOFTWARE) {
        if (fbIntSoftTriggerDelay < 1 || softRate <= 0.0)
            return ERROR;
    }
#else
#if FB_OS_LINUX
    fbLinuxSoftClock.initialized = 0;
    if (mode == FB_TRIGGER_MODE_HARDWARE)
        return fbLinuxTimerStart(fbdrvpar->rate);
#endif
    if (mode == FB_TRIGGER_MODE_SOFTWARE && softRate <= 0.0)
        return ERROR;
#endif

    return OK;
}

static void fbStopTriggerSource(void)
{
#if FB_OS_VXWORKS
    sysAuxClkDisable();
#elif FB_OS_LINUX
    fbLinuxTimerClose();
    fbLinuxSoftClock.initialized = 0;
#endif
}

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
#if FB_OS_VXWORKS
    STATUS taskalive;
    int options;
    int connected;
#endif
    tfbdrvset *fbdrvset;
    tfbdrvpar *fbdrvpar;
    int status;
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
    status = OK;

    if (fb_initialized == 0) {
        fbTriggerConfigLockInit();
        fbIntSigFlag = 0;

        sstatus = sem_open(FB_SEM_NAME, O_CREAT, S_IRUSR | S_IWUSR, 0);
        if (sstatus == SEM_FAILED) {
            fbPrintSemaphoreError(errno);
            return ERROR;
        }
        sem_close(sstatus);

#if FB_OS_VXWORKS
        if ((taskalive = taskIdVerify(fbIntTaskId)) == OK) {
            logMsg("Task ID %d already active (Feedback Task)\n", fbIntTaskId, 0, 0, 0, 0, 0);
            return ERROR;
        }

        sysAuxClkDisable();
        fbSemBTimer = semBCreate(SEM_Q_FIFO, SEM_EMPTY);
        if (fbSemBTimer == NULL) {
            logMsg("Unable to create feedback cycle semaphore\n", 0, 0, 0, 0, 0, 0);
            return ERROR;
        }

        epicsThreadCreate("tFbTrigger",
                          epicsThreadPriorityHigh,
                          epicsThreadGetStackSize(epicsThreadStackSmall),
                          (EPICSTHREADFUNC)fbTriggerLoop,
                          NULL);

        if (fb_common_vxfbtask == 1)
            options = VX_FP_TASK;
        else
            options = 0x80;

        fbIntTaskId = taskSpawn("tFB", priority, options, 20000,
                                (FUNCPTR)fbLoop,
                                (int)fbdrvset, (int)fbdrvpar,
                                0, 0, 0, 0, 0, 0, 0, 0);
        if (fbIntTaskId == ERROR) {
            logMsg("ERROR while task spawn tFB(%d)\n", fbIntTaskId, 0, 0, 0, 0, 0);
            return ERROR;
        }
        logMsg("Task ID %d spawned (tFB)\n", fbIntTaskId, 0, 0, 0, 0, 0);

        taskDelay(100);
        if (sysAuxClkRateSet(rate) == ERROR) {
            logMsg("ERROR while sysAuxClkRateSet(%d)\n", rate, 0, 0, 0, 0, 0);
            return ERROR;
        }
        connected = sysAuxClkConnect((FUNCPTR)fbauxclkhandler, 0);
        if (connected == ERROR) {
            logMsg("ERROR while sysAuxClkConnect\n", 0, 0, 0, 0, 0, 0);
            return ERROR;
        }
#else
        fbSignal = epicsEventMustCreate(epicsEventEmpty);
        epicsThreadCreate("tFeedback",
                          epicsThreadPriorityHigh,
                          epicsThreadGetStackSize(epicsThreadStackBig),
                          (EPICSTHREADFUNC)fbLoop,
                          NULL);
#if !FB_OS_LINUX
        epicsThreadCreate("tFbTrigger",
                          epicsThreadPriorityHigh,
                          epicsThreadGetStackSize(epicsThreadStackSmall),
                          (EPICSTHREADFUNC)fbTriggerLoop,
                          NULL);
#endif
#endif
        fb_initialized = 1;
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
        status = (*fbdrvset->configure)(fbdrvpar);
        if (status != OK)
            return status;
    }
    if (fbdrvset->init) {
        Debug(2, "Calling fbdrvset->init(fbdrvpar->status = %d)", fbdrvpar->status);
        status = (*fbdrvset->init)(fbdrvpar);
    }

    return status;
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
    if (fbLoopRequested != 0) {
        Debug(0, "%s", "Feedback task is open or starting");
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

    /* Stop and wake the loop where the selected backend permits it. */
    fbClose();

    /* Wait until fbLoop() leaves active state */
    while (fbLoopRequested != 0) {

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
 * @brief Software trigger producer used by VxWorks and fallback platforms.
 *
 * Linux software triggering is implemented directly in the feedback thread
 * with CLOCK_MONOTONIC absolute deadlines, so no producer thread is used.
 */
void fbTriggerLoop(void)
{
#if FB_OS_LINUX
    return;
#else
    int mode;
    double softRate;
    double delay;

    for (;;) {
        fbGetTriggerConfiguration(&mode, &softRate);
        if (mode != FB_TRIGGER_MODE_SOFTWARE ||
            softRate <= 0.0 ||
            fbIntSoftTriggerDelay < 1) {
            epicsThreadSleep(0.1);
            continue;
        }

        delay = epicsThreadSleepQuantum() *
                (double)fbIntSoftTriggerDelay;
        epicsThreadSleep(delay);

        fbGetTriggerConfiguration(&mode, &softRate);
        if (mode == FB_TRIGGER_MODE_SOFTWARE &&
            fbLoopRunning != 0 &&
            pfbDrvDset != NULL &&
            pfbDrvDset->parameter != NULL &&
            pfbDrvDset->parameter->status == OK &&
            fbIntSigFlag == 0) {
            fbSignalFeedbackCycle();
        }
    }
#endif
}

/*****************************************************************************/
/*
 NAME
    fbLoop()
 DESCRIPTION
    Feedback task lifecycle loop.
*/
void fbLoop(void)
{
    struct sigaction endTask;
    int status;
    int sval;
    int currentRate;
    int closeStatus;
    sem_t *sem_des;
    tfbdrvset *fbdrvset;
    tfbdrvpar *fbdrvpar;
#if FB_OS_VXWORKS
    int priority;
#endif

    sem_des = sem_open(FB_SEM_NAME, 0, 0, 0);
    if (sem_des == SEM_FAILED) {
        fbPrintSemaphoreError(errno);
        return;
    }

    endTask.sa_handler = fbsignalhandler;
    sigemptyset(&endTask.sa_mask);
    endTask.sa_flags = FB_NO_OPTIONS;
    if (sigaction(SIGABRT, &endTask, NULL) == ERROR) {
        Debug(0, "Could not install signal handler %p", fbsignalhandler);
        sem_close(sem_des);
        return;
    }

    for (;;) {
        sem_getvalue(sem_des, &sval);
        Debug(2, "Wait for fbOpen(): semaphore value=%d", sval);
        if (sem_wait(sem_des) != 0) {
            Debug(0, "sem_wait(" FB_SEM_NAME ") failed: errno=%d", errno);
            continue;
        }

        if (fbIntSigFlag != 0) {
            fbLoopRequested = 0;
            continue;
        }

        fbdrvset = pfbDrvDset;
        if (fbdrvset == NULL || fbdrvset->parameter == NULL) {
            Debug(0, "%s", "No active feedback plugin after fbOpen()");
            fbLoopRequested = 0;
            continue;
        }
        fbdrvpar = fbdrvset->parameter;
        status = OK;
        currentRate = fbdrvpar->rate;

#if FB_OS_VXWORKS
        fbIntTaskId = taskIdSelf();
        priority = 0;
        if (taskPriorityGet(fbIntTaskId, &priority) == OK &&
            priority != fbdrvpar->priority) {
            Debug(1, "Changing feedback priority to %d", fbdrvpar->priority);
            taskPrioritySet(fbIntTaskId, fbdrvpar->priority);
        }
#endif

        if (fbdrvset->open != NULL) {
            status = (*fbdrvset->open)(fbdrvpar);
            if (status != OK)
                Debug(0, "ERROR in *fbdrvset->open(), rate=%d", currentRate);
        }

        if (status == OK && fbIntSigFlag == 0) {
            fbLoopRunning = 1;
            fbdrvpar->status = OK;
            status = fbPrepareTriggerSource(fbdrvpar);
            if (status == OK) {
                Debug(1, "Entering inner loop, trigger mode=%s",
                      fbGetTriggerModeName(fbCurrentTriggerMode));
                status = fbInnerLoop(fbdrvset, fbdrvpar);
            }
        }

        fbStopTriggerSource();
        fbdrvpar->status = ERROR;

        if (fbdrvset->close != NULL) {
            closeStatus = (*fbdrvset->close)(fbdrvpar);
            if (closeStatus != OK)
                Debug(0, "ERROR in *fbdrvset->close() = %d", closeStatus);
        }

        fbLoopRunning = 0;
        fbLoopRequested = 0;
        Debug(1, "Feedback loop stopped, status=%d", status);
    }
}

/*****************************************************************************/
/*
 NAME
    fbInnerLoop()
 DESCRIPTION
    Execute one input and output callback for each selected trigger.
 RETURNS
    OK or ERROR
*/
int fbInnerLoop(tfbdrvset *fbdrvset, tfbdrvpar *fbdrvpar)
{
    int status;
    int waitStatus;
    INTSUPFUN *InputNodeFunction;
    INTSUPFUN *OutputNodeFunction;
    INTSUPFUN inFunc;
    INTSUPFUN outFunc;

    status = OK;

    if (fbdrvset == NULL || fbdrvpar == NULL)
        return ERROR;

    if (fbdrvpar->inode < 0 || fbdrvpar->inode > FB_MAX_NODES) {
        Debug(0, "fbInnerLoop: invalid inode=%d", fbdrvpar->inode);
        return ERROR;
    }
    if (fbdrvpar->onode < 0 || fbdrvpar->onode > FB_MAX_NODES) {
        Debug(0, "fbInnerLoop: invalid onode=%d", fbdrvpar->onode);
        return ERROR;
    }

    InputNodeFunction = (INTSUPFUN *)&fbdrvset->input_node1;
    OutputNodeFunction = (INTSUPFUN *)&fbdrvset->output_node1;
    inFunc = InputNodeFunction[fbdrvpar->inode];
    outFunc = OutputNodeFunction[fbdrvpar->onode];

    if (inFunc == NULL) {
        Debug(0, "fbInnerLoop: NULL input callback inode=%d", fbdrvpar->inode);
        return ERROR;
    }
    if (outFunc == NULL) {
        Debug(0, "fbInnerLoop: NULL output callback onode=%d", fbdrvpar->onode);
        return ERROR;
    }

    fbLngCtrCatcher = 0;

    while (status != ERROR && fbIntSigFlag == 0) {
        waitStatus = fbWaitForNextCycle(fbdrvpar);
        if (waitStatus != OK) {
            if (fbIntSigFlag != 0)
                break;
            status = ERROR;
            break;
        }
        if (fbIntSigFlag != 0)
            break;

        fbLngCtrHandler++;

        status = inFunc(fbdrvpar);
        if (status == OK)
            status = outFunc(fbdrvpar);
    }

    Debug(1, "Suspending feedback task. fbLngCtrHandler=%ld status=%d",
          fbLngCtrHandler, status);
    return status;
}

/*****************************************************************************/
/* VxWorks sysAuxClk interrupt handler. */
void fbauxclkhandler(int arg)
{
    (void)arg;
    fbSignalFeedbackCycle();
}

/*****************************************************************************/
/*! 
 NAME
	fbsignalhandler()
 DESCRIPTION
	Stop request signal handler.
*/
void fbsignalhandler(int signal)
{
#if FB_OS_VXWORKS
    (void)signal;
    logMsg("fbsignalhandler: Abort signal received!\n", 0, 0, 0, 0, 0, 0);
#else
    Debug(0, "fbsignalhandler: Abort signal received! %d", signal);
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
    int mode;
    double requestedRate;
    double effectiveRate;
    unsigned long softOverruns;
    unsigned long hardwareOverruns;
#if FB_OS_LINUX
    int linuxTimerActive;
    int linuxTimerRate;
#endif
    tfbdrvset *fbdrvset;
    tfbdrvpar *fbdrvpar;

    printf("***********************************************************\n");
    printf("\t\tfbCore Module\n");
    printf("Built on " __DATE__ " at " __TIME__ "\n");
    printf("Changeset Id: %s\n", HGVERSION);
    printf("and date: %s\n", FB_TIMESTAMP);
    printf("***********************************************************\n");
    printf("Supported feedback rates from %d Hz to %d Hz\n", FB_MIN_RATE, FB_MAX_RATE);

    fbdrvset = pfbDrvDset;
    if (fbdrvset == NULL) {
        printf("No active feedback support module.\n");
        return OK;
    }
    fbdrvpar = fbdrvset->parameter;
    if (fbdrvpar == NULL) {
        printf("No active feedback support module parameter list.\n");
        return OK;
    }

    printf("Registered feedback devices:\n");
    for (i = 0; i < FB_MAX_MEMBERS; i++) {
        if (feedbackPluginList[i] != NULL &&
            feedbackPluginList[i]->name != NULL)
            printf("\t%d: %s\n", i, feedbackPluginList[i]->name);
    }

    printf("Current Feedback Device: %s\n", fbdrvset->name);
    printf("Current Device Report:\n");
    if (fb_initialized == 0) {
        printf("Module not initialized.\n");
        return ERROR;
    }
    if (fbdrvset->report != NULL) {
        status = (*fbdrvset->report)(fbdrvpar);
        if (status != OK)
            Debug(0, "ERROR in *fbdrvset->report() = %p", fbdrvset->report);
    }

    fbTriggerConfigLockInit();
    epicsMutexLock(fbTriggerConfigLock);
    mode = fbCurrentTriggerMode;
    requestedRate = fbSoftTriggerRequestedRate;
    effectiveRate = fbSoftTriggerEffectiveRate;
    softOverruns = fbSoftTriggerOverruns;
    hardwareOverruns = fbHardwareTriggerOverruns;
#if FB_OS_LINUX
    linuxTimerActive = (fbLinuxTimer.timerFd >= 0);
    linuxTimerRate = fbLinuxTimer.rate;
#endif
    epicsMutexUnlock(fbTriggerConfigLock);

    printf("********************\n");
    printf("Feedback Task:\n");
#if FB_OS_VXWORKS
    printf("\tfbIntTaskId = %d\n", fbIntTaskId);
#endif
    printf("\ttrigger mode = %d (%s)\n", mode, fbGetTriggerModeName(mode));
    printf("\thardware rate = %d Hz\n", fbdrvpar->rate);
    printf("\tsoft requested rate = %.6f Hz\n", requestedRate);
    printf("\tsoft effective rate = %.6f Hz\n", effectiveRate);
    printf("\tsoft trigger overruns = %lu\n", softOverruns);
#if FB_OS_LINUX
    printf("\tLinux Hardware backend = timerfd(CLOCK_MONOTONIC)\n");
    printf("\tLinux timerfd active = %s\n",
           linuxTimerActive ? "yes" : "no");
    printf("\tLinux timerfd rate = %d Hz\n", linuxTimerRate);
    printf("\tLinux timerfd overruns = %lu\n", hardwareOverruns);
#else
    (void)hardwareOverruns;
#endif
    printf("\tfbIntSoftTriggerDelay = %d\n", fbIntSoftTriggerDelay);
    printf("\tfbIntSigFlag = %d\n", fbIntSigFlag);
    printf("\tfbLngCtrHandler = %ld\n", fbLngCtrHandler);
    printf("\tfbLngCtrCatcher = %ld\n", fbLngCtrCatcher);
    printf("\tfb_common_debug = %d\n", fb_common_debug);
    printf("\tfb_initialized = %d\n", fb_initialized);

    sem_des = sem_open(FB_SEM_NAME, 0, 0, 0);
    if (sem_des == SEM_FAILED) {
        fbPrintSemaphoreError(errno);
        return ERROR;
    }
    status = sem_getvalue(sem_des, &sval);
    printf("Semaphore FB_SEM_NAME: value=%d status=%d\n", sval, status);
    sem_close(sem_des);
    return status;
}

/*****************************************************************************/
/* Request the active feedback loop to stop. */
int fbClose(void)
{
    int mode;

    if (pfbDrvDset == NULL || pfbDrvDset->parameter == NULL)
        return ERROR;
    if (fbLoopRequested == 0)
        return OK;

    fbIntSigFlag = 1;
    if (fbLoopRunning == 0)
        return OK;

    fbGetTriggerConfiguration(&mode, NULL);
#if FB_OS_LINUX
    if (mode == FB_TRIGGER_MODE_HARDWARE) {
        if (fbLinuxTimerRequestStop() != OK)
            Debug(0, "%s", "Cannot wake Linux timerfd backend");
    } else if (mode == FB_TRIGGER_MODE_MANUAL) {
        fbSignalFeedbackCycle();
    }
#else
    (void)mode;
    if (fb_initialized != 0)
        fbSignalFeedbackCycle();
#endif
    return OK;
}

/*****************************************************************************/
/* Start or resume the feedback loop. */
int fbOpen(void)
{
    int status;
    int mode;
    double softRate;
    sem_t *sem_des;

    if (pfbDrvDset == NULL || pfbDrvDset->parameter == NULL)
        return ERROR;
    if (fb_initialized == 0)
        return ERROR;
    if (fbLoopRequested != 0)
        return ERROR;

    fbGetTriggerConfiguration(&mode, &softRate);
    if (mode == FB_TRIGGER_MODE_SOFTWARE && softRate <= 0.0) {
        Debug(0, "%s", "Software trigger mode requires a positive SOFTTRIGGERRATE");
        return ERROR;
    }

    fbIntSigFlag = 0;
    fbLoopRequested = 1;
    sem_des = sem_open(FB_SEM_NAME, 0, 0, 0);
    if (sem_des == SEM_FAILED) {
        fbLoopRequested = 0;
        fbPrintSemaphoreError(errno);
        return ERROR;
    }
    status = sem_post(sem_des);
    sem_close(sem_des);
    Debug(2, "Start Feedback Task, status = %d", status);
    if (status != 0)
        fbLoopRequested = 0;
    return status == 0 ? OK : ERROR;
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
    if (fbSetRate(rate) != OK)
        return ERROR;
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
    int mode;

    if (pfbDrvDset == NULL)
        return ERROR;
    pfbdrvpar = pfbDrvDset->parameter;
    if (pfbdrvpar == NULL)
        return ERROR;
    if (rate > FB_MAX_RATE || rate < FB_MIN_RATE) {
        Debug(2, "Rate out of range! %d <= RATE <= %d", FB_MIN_RATE, FB_MAX_RATE);
        return ERROR;
    }

    fbGetTriggerConfiguration(&mode, NULL);
#if FB_OS_VXWORKS
    if (mode == FB_TRIGGER_MODE_HARDWARE &&
        fbLoopRunning != 0) {
        if (sysAuxClkRateSet(rate) == ERROR)
            return ERROR;
    }
#elif FB_OS_LINUX
    if (mode == FB_TRIGGER_MODE_HARDWARE &&
        fbLoopRunning != 0) {
        if (fbLinuxTimerSetRate(rate) != OK)
            return ERROR;
    }
#else
    (void)mode;
#endif
    pfbdrvpar->rate = rate;
    return OK;
}

int fbSetSoftTriggerRate(double rate)
{
    double quantum;
    double period;
    double effectiveRate;
    int delayQuanta;

    if (rate <= 0.0 || rate > (double)FB_MAX_RATE) {
        Debug(0, "Software trigger rate out of range: %f", rate);
        return ERROR;
    }

    quantum = epicsThreadSleepQuantum();
    if (quantum <= 0.0)
        return ERROR;

    period = 1.0 / rate;
    delayQuanta = (int)(period / quantum + 0.5);
    if (delayQuanta < 1)
        delayQuanta = 1;
    effectiveRate = 1.0 / (quantum * (double)delayQuanta);

    fbTriggerConfigLockInit();
    epicsMutexLock(fbTriggerConfigLock);
    fbSoftTriggerRequestedRate = rate;
#if FB_OS_LINUX
    fbSoftTriggerEffectiveRate = rate;
    (void)effectiveRate;
    Debug(0, "rate: %f", fbSoftTriggerEffectiveRate);
#else
    fbSoftTriggerEffectiveRate = effectiveRate;
    Debug(0, "rate: %f", fbSoftTriggerEffectiveRate);
#endif
    fbIntSoftTriggerDelay = delayQuanta;
    fbSoftTriggerOverruns = 0ul;
    epicsMutexUnlock(fbTriggerConfigLock);

    return OK;
}

int fbGetSoftTriggerRate(double *rate)
{
    if (rate == NULL)
        return ERROR;

    fbTriggerConfigLockInit();
    epicsMutexLock(fbTriggerConfigLock);
    *rate = fbSoftTriggerEffectiveRate;
    epicsMutexUnlock(fbTriggerConfigLock);
    return *rate > 0.0 ? OK : ERROR;
}

int fbSetTriggerMode(int mode)
{
    double softRate;

    if (mode < FB_TRIGGER_MODE_HARDWARE ||
        mode > FB_TRIGGER_MODE_MANUAL)
        return ERROR;

    if (fbLoopRequested != 0) {
        Debug(0, "%s", "Close the feedback loop before changing trigger mode");
        return ERROR;
    }

    fbGetTriggerConfiguration(NULL, &softRate);
    if (mode == FB_TRIGGER_MODE_SOFTWARE && softRate <= 0.0) {
        Debug(0, "%s", "Set a positive software trigger rate first");
        return ERROR;
    }

#if FB_OS_VXWORKS
    if (fb_initialized != 0)
        sysAuxClkDisable();
#endif

    fbTriggerConfigLockInit();
    epicsMutexLock(fbTriggerConfigLock);
    fbCurrentTriggerMode = mode;
    fbSoftTriggerOverruns = 0ul;
    fbHardwareTriggerOverruns = 0ul;
    epicsMutexUnlock(fbTriggerConfigLock);
    return OK;
}

int fbGetTriggerMode(int *mode)
{
    if (mode == NULL)
        return ERROR;
    fbGetTriggerConfiguration(mode, NULL);
    return OK;
}

int fbGetSoftTriggerOverruns(unsigned long *overruns)
{
    if (overruns == NULL)
        return ERROR;

    fbTriggerConfigLockInit();
    epicsMutexLock(fbTriggerConfigLock);
    *overruns = fbSoftTriggerOverruns;
    epicsMutexUnlock(fbTriggerConfigLock);
    return OK;
}

int fbGetHardwareTriggerOverruns(unsigned long *overruns)
{
    if (overruns == NULL)
        return ERROR;

    fbTriggerConfigLockInit();
    epicsMutexLock(fbTriggerConfigLock);
    *overruns = fbHardwareTriggerOverruns;
    epicsMutexUnlock(fbTriggerConfigLock);
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
#if FB_OS_VXWORKS
    if (priority > 254 || priority < 0) {
        Debug(2, "Priority out of range! %d < PRIORITY < %d", 254, 0);
        return ERROR;
    }
    pfbdrvpar->priority = priority;
    return OK;
#else
    (void)priority;
    return ERROR;
#endif                          /* if FB_OS_VXWORKS */
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
{
    tfbdrvpar *pfbdrvpar;
    int mode;

    if (pfbDrvDset == NULL)
        return ERROR;
    pfbdrvpar = pfbDrvDset->parameter;
    if (pfbdrvpar == NULL)
        return ERROR;
    if (fbLoopRunning == 0 || pfbdrvpar->status != OK)
        return ERROR;

    fbGetTriggerConfiguration(&mode, NULL);
#if FB_OS_LINUX
    if (mode != FB_TRIGGER_MODE_MANUAL) {
        Debug(2, "%s",
              "fbTrigger is available only in Linux Manual mode; Hardware uses timerfd and Software uses the absolute wait");
        return ERROR;
    }
#else
    (void)mode;
#endif

    fbSignalFeedbackCycle();
    return OK;
}
