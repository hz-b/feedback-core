// SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin für Materialien und Energie GmbH
// SPDX-License-Identifier: MIT

#ifndef __DEV_FEEDBACK_H
#define __DEV_FEEDBACK_H

/* Use this value to define the properties of the record instance using the record's INP field*/
#include "fbCore.h"
#define MAX_DEVCHRS        40
#define FB_INIT_STRING     "INIT"
#define FB_OPEN_STRING     "OPEN"
#define FB_CLOSE_STRING    "CLOSE"
#define FB_ICARD_STRING    "ICARD"
#define FB_ISIG_STRING     "ISIG"
#define FB_OCARD_STRING    "OCARD"
#define FB_OSIG_STRING     "OSIG"
#define FB_RATE_STRING     "RATE"
#define FB_PRIORITY_STRING "PRIORITY"
#define FB_STATUS_STRING   "STATUS"
#define FB_VAL_STRING      "VAL"
#define FB_INODE_STRING    "INODE"
#define FB_ONODE_STRING    "ONODE"
#define FB_TRIGGER_STRING  "TRIGGER"
#define FB_HOME_STRING     "HOME"
#define FB_FLAGS_STRING    "FLAGS"
#define FB_IVAL_STRING     "IVAL"
#define FB_UIVAL_STRING    "UIVAL"
#define FB_SVAL_STRING     "SVAL"
#define FB_DVAL_STRING     "DVAL"
#define FB_PROCONODE_STRING "PROC_ONODE"
#define FB_PROCINODE_STRING "PROC_INODE"
#define FB_PLUGIN_STRING   "PLUGIN"
#define FB_SOFTTRIGGERRATE_STRING "SOFTTRIGGERRATE"
#define FB_DATASETEVENTS_STRING "DATASETEVENTS"

/*
 * Legacy parser macros kept for source compatibility with external code that
 * may include devFeedback.h. New parser code uses a lookup table instead.
 */
#define FB_IF_DEV(FB_DEVICE, FB_USE)\
    if (strcmp(c,FB_USE)==0)\
    {\
        fbCDevice = FB_DEVICE;\
    }

#define FB_ELSEIF_DEV(FB_DEVICE, FB_USE)\
    else if (strcmp(c,FB_USE)==0)\
    {\
        fbCDevice = FB_DEVICE;\
    }

#define FB_ELSE_RETURN_DEV(RETURN_CODE)\
    else\
    {\
        return(RETURN_CODE);\
    }
/* Synchronous Monochromator Interface to VxWorks */
#define DEVFB_INPCARD    0
#define DEVFB_INPSIGNAL  0
#define DEVFB_OUTCARD    0
#define DEVFB_OUTSIGNAL  0
#define DEVFB_INODE      0
#define DEVFB_ONODE      0
#define DEVFB_RATE       100
#define DEVFB_PRIORITY   100

extern int fb_common_debug;
#ifdef NODEBUG
#define Debug(L,FMT,ARGS...) ;
#else
#define Debug(L, FMT, ARGS...) { if(L <= fb_common_debug) \
    fprintf(stderr, FMT "\t- file : " __FILE__ ", line %d\n", ## ARGS, __LINE__); }
#endif

typedef struct {
    int source;
    int device;
    int firsttime;
    tfbdrvpar *fb;
} structFbCDefPVT;

enum enumFbCDevice {
    FBINIT,
    FBOPEN,
    FBCLOSE,
    FBICARD,
    FBISIG,
    FBOCARD,
    FBOSIG,
    FBRATE,
    FBPRIORITY,
    FBSTATUS,
    FBVAL,
    FBINODE,
    FBONODE,
    FBTRIGGER,
    FBHOME,
    FBFLAGS,
    FBIVAL,
    FBUIVAL,
    FBSVAL,
    FBDVAL,
    FBPROC_ONODE,
    FBPROC_INODE,
    FBPLUGIN,
    FBSOFTTRIGGERRATE,
    FBDATASETEVENTS
};                                      /* enumFbCDevice */

#ifdef __cplusplus
extern "C" {
#endif
    extern int fbParseDeviceName(char *string, int *device);
    extern int fbLookupDeviceName(const char *name, int *device);
    extern const char *fbDeviceNameFromId(int device);
#ifdef __cplusplus
}
#endif
#endif                                  /* __DEV_FEEDBACK_H */
