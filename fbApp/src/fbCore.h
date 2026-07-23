// SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin für Materialien und Energie GmbH
// SPDX-License-Identifier: MIT

/*! \file
 * \brief Feedback Core Task */

#ifndef __FB_CORE_H
#define __FB_CORE_H

#include <stdlib.h>

#ifndef OK
#define OK  0
#endif
#ifndef ERROR
#define ERROR   -1
#endif

#if defined(vxWorks) && defined(CPU) && (CPU == PPC603)
#define SYS_CLK_RATE_MIN 40
#endif

#ifndef SYS_CLK_RATE_MIN
#define  SYS_CLK_RATE_MIN	3
#endif
#ifndef SYS_CLK_RATE_MAX
#define  SYS_CLK_RATE_MAX	5000
#endif

#define FB_PRECONFIGURE		2
#define FB_CONFIGURE		3
#define FB_FORCE_CONFIGURE	4
#define FB_INIT			   -2
#define FB_NOT_READY	   -3
#define FB_MAX_NODES		7
#define FB_MAX_RATE		SYS_CLK_RATE_MAX
#define FB_MIN_RATE		SYS_CLK_RATE_MIN
#define FB_SEM_NAME		"/feedback.sem"
#define FB_SEM_INITIAL_VALUE	0
#define FB_SEM_MODE		S_IRWXU | S_IRWXG | S_IRWXO
#define FB_NO_OPTIONS		0
#define FB_SIGNAL_CLOSE		1
#define FB_SIGNAL_NOCLOSE	0

/* Feedback cycle trigger source. */
typedef enum fbTriggerMode {
    FB_TRIGGER_MODE_HARDWARE = 0,
    FB_TRIGGER_MODE_SOFTWARE = 1,
    FB_TRIGGER_MODE_MANUAL = 2
} fbTriggerMode;
#ifdef __cplusplus
typedef int (*INTSUPFUN)(void *);       /* ptr to device support function */
#else
typedef int (*INTSUPFUN)();             /* ptr to device support function */
#endif

#define FB_MAX_MEMBERS 32
#define FB_DATA_SET1_READY_EVENT 231
#define FB_DATA_SET2_READY_EVENT 232

#define FB_ANALOG_MAX_CHANNELS 16

/* Callback used by the generic analog feedback plugin to fetch one channel.
 * The callback must be fast and non-blocking; it is called from the feedback
 * task context. Return OK when *value was updated, otherwise ERROR.
 */
typedef int (*fbAnalogInputUpdateCallback)(unsigned short channel, double *value);

/* Optional callback used by the generic analog feedback plugin after the
 * per-channel filter has been applied. The callback receives both the raw
 * value and the filtered value on every feedback-loop iteration. It is called
 * from feedback task context and must be fast and non-blocking. Return OK on
 * success, otherwise ERROR.
 */
typedef int (*fbAnalogFilterUpdateCallback)(unsigned short channel, double rawValue, double filteredValue);

/*! This may be used to identify encoder types */
typedef struct {
    short inpcard;
    short inpsignal;
    short outcard;
    short outsignal;
    short inode;
    short onode;
    int rate;
    int priority;
    int status;
    unsigned int flags;
    short sval;
    int ival;
    unsigned int uival;
    long lval;
    double dval;
    void *indata;
    void *outdata;
    int procin;                         /* process input nodes in epics scan task context */
    int procout;                        /* process output nodes in epics scan task context */
} tfbdrvpar;

/* Optional lifecycle hooks used by the generic analog feedback plugin.
 * The hook is called from the feedback task context and must be fast and
 * non-blocking. Return OK on success, otherwise ERROR.
 */
typedef int (*fbAnalogPluginHookCallback)(tfbdrvpar * pfb);
typedef struct {
    /*! This points to the null-terminated name (e.g. "PM1 GRATING"). */
    const char *name;
    /*! This points to the null-terminated strings of node names (e.g. "IK320 Input C1"). */
    const char *nodenames[16];
    /*! Unique ID of created plugin (to be written at run time) */
    int id;
    /*! This may be evaluated by the host */
    int version;
    /*! This may be evaluated by the host */
    size_t size;
    tfbdrvpar *parameter;
    INTSUPFUN report;
    INTSUPFUN configure;
    INTSUPFUN init;
    INTSUPFUN open;
    INTSUPFUN close;
    INTSUPFUN home;
    INTSUPFUN activate;
    INTSUPFUN deactivate;
    INTSUPFUN input_node1;
    INTSUPFUN input_node2;
    INTSUPFUN input_node3;
    INTSUPFUN input_node4;
    INTSUPFUN input_node5;
    INTSUPFUN input_node6;
    INTSUPFUN input_node7;
    INTSUPFUN input_node8;
    INTSUPFUN output_node1;
    INTSUPFUN output_node2;
    INTSUPFUN output_node3;
    INTSUPFUN output_node4;
    INTSUPFUN output_node5;
    INTSUPFUN output_node6;
    INTSUPFUN output_node7;
    INTSUPFUN output_node8;
} tfbdrvset;

typedef struct {
    int enabled;
    int initialized;
    double delaySamples;
    double alpha;
    double y;
} fbAnalogFilterState;

#ifdef __cplusplus
extern "C" {
#endif
    extern tfbdrvset *pfbDrvDset;
    extern tfbdrvset *feedbackPluginList[FB_MAX_MEMBERS];
    extern fbAnalogFilterState fbAnalogFilters[FB_ANALOG_MAX_CHANNELS];
    extern int fbIntSoftTriggerDelay;
    extern int fb_common_debug;
    extern int fbIntSigFlag;
    extern long fbLngCtrCatcher;
    extern int fbSetNodes(short inode, short onode);
    extern int fbGetNodes(short *inode, short *onode);
    extern int fbGetNodeName(int index, char *name, int length);
    extern int fbInit(short inpcard, short inpsignal, short outcard, short outsignal, short inode, short onode, int rate, int priority);
    extern int fbConfigure(short inpcard, short inpsignal, short outcard, short outsignal, short inode, short onode, int rate, int priority);
    extern int fbGetRate(int *rate);
    extern int fbSetRate(int rate);
    extern int fbSetSoftTriggerRate(double rate);
    extern int fbGetSoftTriggerRate(double *rate);
    extern int fbSetTriggerMode(int mode);
    extern int fbGetTriggerMode(int *mode);
    extern const char *fbGetTriggerModeName(int mode);
    extern int fbGetSoftTriggerOverruns(unsigned long *overruns);
    extern int fbGetHardwareTriggerOverruns(unsigned long *overruns);
    extern int fbGetPriority(int *priority);
    extern int fbSetPriority(int priority);
    extern int fbpr(void);
    extern int fbOpen(void);
    extern int fbClose(void);
    extern int fbDeactivate(void);
    extern int fbActivate(int id);
    extern int fbHome(unsigned int flags);
    extern int fbGetStatus(int *status);
    extern int fbGetFlags(unsigned int *flags);
    extern int fbSetFlags(unsigned int flags);
    extern int fbTrigger(void);
#ifdef __cplusplus
}
#endif
#endif                                  /* __FB_CORE_H */
