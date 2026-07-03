// SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin für Materialien und Energie GmbH
// SPDX-License-Identifier: MIT

#ifndef __FB_REGISTRY_H
#define __FB_REGISTRY_H
#include "fbCore.h"

#ifndef OK
#define OK  0
#endif
#ifndef ERROR
#define ERROR   -1
#endif

#ifdef __cplusplus
extern "C" {
#endif

    extern int feedbackInit(tfbdrvset * plugin);
    extern int feedbackGetPluginById(int id, tfbdrvset ** feedbackPlugin);
    extern int feedbackGetPluginNameById(int id, char *name, size_t n);
    extern int feedbackGetPluginId(int *id);

#ifdef __cplusplus
}
#endif
#endif                                  /* __FB_REGISTRY_H */
