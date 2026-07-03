// SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin für Materialien und Energie GmbH
// SPDX-License-Identifier: MIT

#ifndef __FB_ANALOG_H
#define __FB_ANALOG_H

#include <dbScan.h>

#ifdef __cplusplus
extern "C" {
#endif

    extern int fbAnalogFilterInit(void);
    extern int fbAnalogInit(void);
    extern int fbAnalogSet(unsigned short channel, double value);
    extern int fbAnalogGet(unsigned short channel, double *value);
    extern double fbAnalogFilterApply(fbAnalogFilterState * f, double x);
    extern int fbAnalogSetFilterDelay(double delaySamples, unsigned short channel);
    extern int fbAnalogGetFilterDelay(double *delaySamples, unsigned short channel);
    extern int fbSetAnalogInputUpdateCallback(fbAnalogInputUpdateCallback callback);
    extern fbAnalogInputUpdateCallback fbGetAnalogInputUpdateCallback(void);
    extern int fbSetAnalogFilterUpdateCallback(fbAnalogFilterUpdateCallback callback);
    extern fbAnalogFilterUpdateCallback fbGetAnalogFilterUpdateCallback(void);
    extern int fbSetAnalogPluginInitCallback(fbAnalogPluginHookCallback callback);
    extern fbAnalogPluginHookCallback fbGetAnalogPluginInitCallback(void);
    extern int fbSetAnalogPluginOpenCallback(fbAnalogPluginHookCallback callback);
    extern fbAnalogPluginHookCallback fbGetAnalogPluginOpenCallback(void);
    extern int fbSetAnalogPluginCloseCallback(fbAnalogPluginHookCallback callback);
    extern fbAnalogPluginHookCallback fbGetAnalogPluginCloseCallback(void);
    extern int fbAnalogPluginInstantiate(void);
    extern int fbAnalogGetIoScanPvt(unsigned short channel, IOSCANPVT * ppvt);

#ifdef __cplusplus
}
#endif
#endif                                  /* __FB_ANALOG_H */
