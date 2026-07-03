// SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin für Materialien und Energie GmbH
// SPDX-License-Identifier: MIT

/*! \file
 * \brief Generic feedback plugin for sampling analog values.
 *
 * Linux default: read_my_adc_channel() returns a deterministic test ramp.
 * VxWorks/IK320: replace read_my_adc_channel() with the hardware call.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <registryFunction.h>
#include <epicsExport.h>
#include <epicsMutex.h>
#include <iocsh.h>

#include "fbCore.h"
#include "fbAnalog.h"
#include "fbRegistry.h"
#include "devFeedback.h"

#ifndef OK
#define OK 0
#endif
#ifndef ERROR
#define ERROR -1
#endif

#ifndef FB_ANALOG_DEFAULT_CHANNELS
#define FB_ANALOG_DEFAULT_CHANNELS 8
#endif

typedef struct fbAnalogChannel {
    IOSCANPVT ioscanpvt;
    epicsMutexId lock;
    double value;
    int valid;
} fbAnalogChannel;

static fbAnalogChannel fbAnalogChannels[FB_ANALOG_MAX_CHANNELS];
static int fbAnalogInitialized = 0;
fbAnalogFilterState fbAnalogFilters[FB_ANALOG_MAX_CHANNELS];
int fbAnalogSetDivider = 1;
epicsExportAddress(int, fbAnalogSetDivider);
static unsigned long fbAnalogSetCounter = 0;

static int fbAnalogDefaultInputUpdate(unsigned short channel, double *value);
static fbAnalogInputUpdateCallback fbAnalogInputUpdate = fbAnalogDefaultInputUpdate;
static fbAnalogFilterUpdateCallback fbAnalogFilterUpdate = NULL;
static fbAnalogPluginHookCallback fbAnalogPluginInitCallback = NULL;
static fbAnalogPluginHookCallback fbAnalogPluginOpenCallback = NULL;
static fbAnalogPluginHookCallback fbAnalogPluginCloseCallback = NULL;

static void fbAnalogFilterInitOne(fbAnalogFilterState * filter)
{
    if (filter == NULL)
        return;

    filter->enabled = 0;
    filter->initialized = 0;
    filter->delaySamples = 0.0;
    filter->alpha = 1.0;
    filter->y = 0.0;
}

int fbAnalogFilterInit(void)
{
    unsigned short channel;

    for (channel = 0; channel < FB_ANALOG_MAX_CHANNELS; channel++) {
        fbAnalogFilterInitOne(&fbAnalogFilters[channel]);
    }

    return OK;
}

int fbAnalogInit(void)
{
    int i;

    if (fbAnalogInitialized)
        return OK;

    Debug(2, "Initializing feedback analog input cache with %d channels", FB_ANALOG_MAX_CHANNELS);
    for (i = 0; i < FB_ANALOG_MAX_CHANNELS; i++) {
        scanIoInit(&fbAnalogChannels[i].ioscanpvt);
        fbAnalogChannels[i].lock = epicsMutexMustCreate();
        fbAnalogChannels[i].value = 0.0;
        fbAnalogChannels[i].valid = 0;
    }

    fbAnalogInitialized = 1;
    return OK;
}

int fbAnalogSet(unsigned short channel, double value)
{
    if (!fbAnalogInitialized)
        fbAnalogInit();

    if (channel >= FB_ANALOG_MAX_CHANNELS) {
        Debug(0, "fbAnalogSet: invalid channel %u >= %d", (unsigned int)channel, FB_ANALOG_MAX_CHANNELS);
        return ERROR;
    }

    epicsMutexLock(fbAnalogChannels[channel].lock);
    fbAnalogChannels[channel].value = value;
    fbAnalogChannels[channel].valid = 1;
    epicsMutexUnlock(fbAnalogChannels[channel].lock);

    Debug(11, "fbAnalogSet: channel=%u value=%f, requesting I/O Intr scan", (unsigned int)channel, value);
    scanIoRequest(fbAnalogChannels[channel].ioscanpvt);
    return OK;
}

int fbAnalogGet(unsigned short channel, double *value)
{
    int valid;

    if (value == NULL)
        return ERROR;

    if (!fbAnalogInitialized)
        fbAnalogInit();

    if (channel >= FB_ANALOG_MAX_CHANNELS) {
        Debug(0, "fbAnalogGet: invalid channel %u >= %d", (unsigned int)channel, FB_ANALOG_MAX_CHANNELS);
        return ERROR;
    }

    epicsMutexLock(fbAnalogChannels[channel].lock);
    *value = fbAnalogChannels[channel].value;
    valid = fbAnalogChannels[channel].valid;
    epicsMutexUnlock(fbAnalogChannels[channel].lock);

    if (!valid) {
        Debug(5, "fbAnalogGet: channel=%u has no valid value yet", (unsigned int)channel);
        return ERROR;
    }

    Debug(10, "fbAnalogGet: channel=%u value=%f", (unsigned int)channel, *value);
    return OK;
}

int fbAnalogGetIoScanPvt(unsigned short channel, IOSCANPVT * ppvt)
{
    if (ppvt == NULL)
        return ERROR;

    if (!fbAnalogInitialized)
        fbAnalogInit();

    if (channel >= FB_ANALOG_MAX_CHANNELS) {
        Debug(0, "fbAnalogGetIoScanPvt: invalid channel %u >= %d", (unsigned int)channel, FB_ANALOG_MAX_CHANNELS);
        return ERROR;
    }

    *ppvt = fbAnalogChannels[channel].ioscanpvt;
    Debug(6, "fbAnalogGetIoScanPvt: channel=%u", (unsigned int)channel);
    return OK;
}

double fbAnalogFilterApply(fbAnalogFilterState * f, double x)
{
    if (f == NULL || !f->enabled)
        return x;

    if (!f->initialized) {
        f->y = x;
        f->initialized = 1;
        return x;
    }

    f->y += f->alpha * (x - f->y);
    return f->y;
}

int fbAnalogSetFilterDelay(double delaySamples, unsigned short channel)
{
    fbAnalogFilterState *f;

    if (channel >= FB_ANALOG_MAX_CHANNELS)
        return ERROR;

    if (delaySamples < 0.0)
        return ERROR;

    f = &fbAnalogFilters[channel];

    f->delaySamples = delaySamples;
    f->alpha = 1.0 / (delaySamples + 1.0);
    f->enabled = (delaySamples > 0.0);

    return OK;
}

int fbAnalogGetFilterDelay(double *delaySamples, unsigned short channel)
{
    fbAnalogFilterState *f;

    if (channel >= FB_ANALOG_MAX_CHANNELS)
        return ERROR;

    f = &fbAnalogFilters[channel];
    *delaySamples = f->delaySamples;
    return OK;
}

/*
 * Default callback used until another software layer installs a real provider.
 * Keep callbacks fast and non-blocking; they are called from feedback task
 * context.
 */
static int fbAnalogDefaultInputUpdate(unsigned short channel, double *value)
{
    static double testValue[FB_ANALOG_MAX_CHANNELS];

    if (value == NULL || channel >= FB_ANALOG_MAX_CHANNELS) {
        Debug(0, "value=%p, channel=%d", value, channel);
        return ERROR;
    }
    testValue[channel] += 0.001 * (double)(channel + 1);
    *value = testValue[channel];
    return OK;
}

/*
int fbSetAnalogInputUpdateCallback(fbAnalogInputUpdateCallback callback)
{
    if (callback == NULL)
        return ERROR;

    fbAnalogInputUpdate = callback;
    return OK;
}
*/
int fbSetAnalogInputUpdateCallback(fbAnalogInputUpdateCallback callback)
{
    printf("||||||  fbSetAnalogInputUpdateCallback: old=%p new=%p\n", (void *)fbAnalogInputUpdate, (void *)callback);

#if defined(__GNUC__)
    printf("||||||||||||  fbSetAnalogInputUpdateCallback: caller=%p\n", __builtin_return_address(0));
#endif

    fbAnalogInputUpdate = callback;

    printf("||||||  fbSetAnalogInputUpdateCallback: now=%p\n", (void *)fbAnalogInputUpdate);

    return OK;
}

fbAnalogInputUpdateCallback fbGetAnalogInputUpdateCallback(void)
{
    return fbAnalogInputUpdate;
}

int fbSetAnalogFilterUpdateCallback(fbAnalogFilterUpdateCallback callback)
{
    fbAnalogFilterUpdate = callback;
    return OK;
}

fbAnalogFilterUpdateCallback fbGetAnalogFilterUpdateCallback(void)
{
    return fbAnalogFilterUpdate;
}

int fbSetAnalogPluginInitCallback(fbAnalogPluginHookCallback callback)
{
    fbAnalogPluginInitCallback = callback;
    return OK;
}

fbAnalogPluginHookCallback fbGetAnalogPluginInitCallback(void)
{
    return fbAnalogPluginInitCallback;
}

int fbSetAnalogPluginOpenCallback(fbAnalogPluginHookCallback callback)
{
    fbAnalogPluginOpenCallback = callback;
    return OK;
}

fbAnalogPluginHookCallback fbGetAnalogPluginOpenCallback(void)
{
    return fbAnalogPluginOpenCallback;
}

int fbSetAnalogPluginCloseCallback(fbAnalogPluginHookCallback callback)
{
    fbAnalogPluginCloseCallback = callback;
    return OK;
}

fbAnalogPluginHookCallback fbGetAnalogPluginCloseCallback(void)
{
    return fbAnalogPluginCloseCallback;
}

static int fbAnalogPluginGetBaseAndCount(tfbdrvpar * pfb, unsigned short *base, unsigned short *count)
{
    int requested_count;

    if (pfb == NULL || base == NULL || count == NULL)
        return ERROR;

    if (pfb->inpsignal < 0 || pfb->inpsignal >= FB_ANALOG_MAX_CHANNELS) {
        Debug(0, "Analog plugin: invalid base channel %d", pfb->inpsignal);
        return ERROR;
    }

    requested_count = pfb->outsignal;
    if (requested_count <= 0)
        requested_count = FB_ANALOG_DEFAULT_CHANNELS;

    if (requested_count < 1 || requested_count > FB_ANALOG_MAX_CHANNELS) {
        Debug(0, "Analog plugin: invalid channel count %d", requested_count);
        return ERROR;
    }

    if ((pfb->inpsignal + requested_count) > FB_ANALOG_MAX_CHANNELS) {
        Debug(0, "Analog plugin: base %d + count %d exceeds %d channels", pfb->inpsignal, requested_count, FB_ANALOG_MAX_CHANNELS);
        return ERROR;
    }

    *base = (unsigned short)pfb->inpsignal;
    *count = (unsigned short)requested_count;
    return OK;
}

static void fbAnalogPluginReportHook(const char *name, fbAnalogPluginHookCallback callback)
{
    printf("\t%s hook: %s\n", name, (callback == NULL) ? "not set" : "custom");
    printf("\t%s hook pointer: %p\n", name, (void *)callback);
}

static int fbAnalogPluginReport(tfbdrvpar * pfb)
{
    unsigned short base;
    unsigned short count;
    fbAnalogInputUpdateCallback inputCb;
    fbAnalogFilterUpdateCallback filterCb;

    if (fbAnalogPluginGetBaseAndCount(pfb, &base, &count) != OK)
        return ERROR;

    inputCb = fbGetAnalogInputUpdateCallback();
    filterCb = fbGetAnalogFilterUpdateCallback();

    printf("Analog feedback plugin:\n");
    printf("\tbase channel: %u\n", (unsigned int)base);
    printf("\tchannel count: %u\n", (unsigned int)count);
    printf("\trate: %d Hz\n", pfb->rate);
    printf("\tpriority: %d\n", pfb->priority);
    printf("\tanalog input update: %s\n", (inputCb == fbAnalogDefaultInputUpdate) ? "default" : "custom");
    printf("\tanalog input update pointer: %p\n", (void *)inputCb);
    printf("\tanalog filter update: %s\n", (filterCb == NULL) ? "not set" : "custom");
    printf("\tanalog filter update pointer: %p\n", (void *)filterCb);
    printf("\tdefault callback pointer: %p\n", (void *)fbAnalogDefaultInputUpdate);
    fbAnalogPluginReportHook("init", fbAnalogPluginInitCallback);
    fbAnalogPluginReportHook("open", fbAnalogPluginOpenCallback);
    fbAnalogPluginReportHook("close", fbAnalogPluginCloseCallback);

    return OK;
}

static int fbAnalogPluginConfigure(tfbdrvpar * pfb)
{
    unsigned short base;
    unsigned short count;

    if (fbAnalogPluginGetBaseAndCount(pfb, &base, &count) != OK)
        return ERROR;

    pfb->inpsignal = (short)base;
    pfb->outsignal = (short)count;
    Debug(2, "Analog plugin configure: status=%d base=%u count=%u rate=%d priority=%d",
          pfb->status, (unsigned int)base, (unsigned int)count, pfb->rate, pfb->priority);
    return OK;
}

static int fbAnalogPluginInit(tfbdrvpar * pfb)
{
    int status;

    if (pfb == NULL)
        return ERROR;

    if (fbAnalogInit() != OK)
        return ERROR;
    if (fbAnalogFilterInit() != OK)
        return ERROR;

    Debug(2, "Analog plugin init %p", pfb);

    status = fbAnalogPluginConfigure(pfb);
    if (status != OK)
        return status;

    if (fbAnalogPluginInitCallback != NULL) {
        status = fbAnalogPluginInitCallback(pfb);
        if (status != OK) {
            Debug(0, "Analog plugin init hook failed status=%d", status);
            return status;
        }
    }

    return OK;
}

static int fbAnalogPluginOpen(tfbdrvpar * pfb)
{
    int status;

    if (pfb == NULL)
        return ERROR;

    pfb->status = OK;
    Debug(2, "Analog plugin open status=%d", pfb->status);

    if (fbAnalogPluginOpenCallback != NULL) {
        status = fbAnalogPluginOpenCallback(pfb);
        if (status != OK) {
            Debug(0, "Analog plugin open hook failed status=%d", status);
            return status;
        }
    }

    return OK;
}

static int fbAnalogPluginClose(tfbdrvpar * pfb)
{
    int status;

    if (pfb == NULL)
        return ERROR;

    pfb->status = ERROR;
    Debug(2, "Analog plugin close status=%d", pfb->status);

    if (fbAnalogPluginCloseCallback != NULL) {
        status = fbAnalogPluginCloseCallback(pfb);
        if (status != OK) {
            Debug(0, "Analog plugin close hook failed status=%d", status);
            return status;
        }
    }

    return OK;
}

static int fbAnalogPluginActivate(void)
{
    Debug(2, "%s", "Analog plugin activate");
    return OK;
}

static int fbAnalogPluginDeactivate(void)
{
    Debug(2, "%s", "Analog plugin deactivate");
    return OK;
}

static int fbAnalogPluginSample(tfbdrvpar * pfb)
{
    unsigned short base;
    unsigned short count;
    unsigned short i;
    unsigned short channel;
    double value;
    double filtered;
    int publish;

    if (fbAnalogPluginGetBaseAndCount(pfb, &base, &count) != OK)
        return ERROR;

    fbAnalogSetCounter++;

    publish = (fbAnalogSetDivider <= 1) || ((fbAnalogSetCounter % (unsigned long)fbAnalogSetDivider) == 0);

    for (i = 0; i < count; i++) {
        channel = (unsigned short)(base + i);

        /*
         * ADC read runs on every feedback-loop iteration.
         */
        if (fbAnalogInputUpdate == NULL || fbAnalogInputUpdate(channel, &value) != OK) {
            Debug(10, "Analog plugin: failed to read channel=%u", (unsigned int)channel);
            return ERROR;
        }

        /*
         * Filter runs on every feedback-loop iteration.
         */
        filtered = fbAnalogFilterApply(&fbAnalogFilters[channel], value);
        /*
         * Optional filtered-value consumer hook. This runs every feedback-loop
         * iteration, independent of the fbAnalogSet() publish divider.
         */
        if (fbAnalogFilterUpdate != NULL) {
            if (fbAnalogFilterUpdate(channel, value, filtered) != OK) {
                Debug(10, "Analog plugin: filter update hook failed channel=%u", (unsigned int)channel);
                return ERROR;
            }
        }
        /*
         * Publishing to fbAnalogSet() is expensive.
         */
        if (publish) {
            if (fbAnalogSet(channel, filtered) != OK) {
                Debug(10, "Analog plugin: failed to publish channel=%u", (unsigned int)channel);
                return ERROR;
            }
            Debug(11, "Analog plugin: channel=%u raw=%f filtered=%f", (unsigned int)channel, value, filtered);
        }
    }
    return OK;
}

static int fbAnalogPluginNoOutput(tfbdrvpar * pfb)
{
    return OK;
}

static tfbdrvpar fbAnalogParameter = {
    0,                                  /* inpcard */
    0,                                  /* inpsignal: base analog channel */
    0,                                  /* outcard */
    FB_ANALOG_DEFAULT_CHANNELS,         /* outsignal: number of channels */
    0,                                  /* inode */
    0,                                  /* onode */
    100,                                /* rate */
    100,                                /* priority */
    ERROR,                              /* status */
    0,                                  /* flags */
    0,                                  /* sval */
    0,                                  /* ival */
    0,                                  /* uival */
    0,                                  /* lval */
    0.0,                                /* dval */
    NULL,                               /* indata */
    NULL,                               /* outdata */
    0,                                  /* procin */
    0                                   /* procout */
};

static tfbdrvset fbAnalogDrvSet = {
    "Analog Feedback",
    {
     "Sample analog channels", "Sample analog channels",
     "Sample analog channels", "Sample analog channels",
     "Sample analog channels", "Sample analog channels",
     "Sample analog channels", "Sample analog channels",
     "No output", "No output", "No output", "No output",
     "No output", "No output", "No output", "No output"},
    -1,
    1,
    sizeof(tfbdrvset),
    &fbAnalogParameter,
    (INTSUPFUN) fbAnalogPluginReport,
    (INTSUPFUN) fbAnalogPluginConfigure,
    (INTSUPFUN) fbAnalogPluginInit,
    (INTSUPFUN) fbAnalogPluginOpen,
    (INTSUPFUN) fbAnalogPluginClose,
    NULL,
    (INTSUPFUN) fbAnalogPluginActivate,
    (INTSUPFUN) fbAnalogPluginDeactivate,
    (INTSUPFUN) fbAnalogPluginSample,
    (INTSUPFUN) fbAnalogPluginSample,
    (INTSUPFUN) fbAnalogPluginSample,
    (INTSUPFUN) fbAnalogPluginSample,
    (INTSUPFUN) fbAnalogPluginSample,
    (INTSUPFUN) fbAnalogPluginSample,
    (INTSUPFUN) fbAnalogPluginSample,
    (INTSUPFUN) fbAnalogPluginSample,
    (INTSUPFUN) fbAnalogPluginNoOutput,
    (INTSUPFUN) fbAnalogPluginNoOutput,
    (INTSUPFUN) fbAnalogPluginNoOutput,
    (INTSUPFUN) fbAnalogPluginNoOutput,
    (INTSUPFUN) fbAnalogPluginNoOutput,
    (INTSUPFUN) fbAnalogPluginNoOutput,
    (INTSUPFUN) fbAnalogPluginNoOutput,
    (INTSUPFUN) fbAnalogPluginNoOutput
};

int fbAnalogPluginInstantiate(void)
{
    int id;

    if (fbAnalogDrvSet.id < 0) {
        id = feedbackInit(&fbAnalogDrvSet);
        if (id == ERROR) {
            Debug(0, "Analog plugin: feedbackInit failed. id=%id", fbAnalogDrvSet.id);
            return ERROR;
        }
    } else {
        id = feedbackInit(&fbAnalogDrvSet);
        if (id == ERROR) {
            Debug(0, "Analog plugin: feedbackInit failed for existing id=%d", fbAnalogDrvSet.id);
            return ERROR;
        }
    }

    if (pfbDrvDset == &fbAnalogDrvSet) {
        Debug(1, "Analog plugin already active id=%d", fbAnalogDrvSet.id);
        return OK;
    }

    if (pfbDrvDset != NULL) {
        Debug(0, "Analog plugin: cannot activate while '%s' is active", pfbDrvDset->name ? pfbDrvDset->name : "<unnamed>");
        return ERROR;
    }

    if (fbActivate(fbAnalogDrvSet.id) != OK) {
        Debug(0, "Analog plugin: fbActivate(%d) failed", fbAnalogDrvSet.id);
        return ERROR;
    }

    Debug(1, "Analog plugin registered and activated id=%d", fbAnalogDrvSet.id);
    return OK;
}
