// SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin für Materialien und Energie GmbH
// SPDX-License-Identifier: MIT

#include <string.h>
#include <epicsStdioRedirect.h>
#include "fbCore.h"
#include "fbRegistry.h"

#ifdef NODEBUG
#define Debug(L,FMT,ARGS...) ;
#else
#define Debug(L, FMT, ARGS...) { if(L <= fb_common_debug) \
	fprintf(stderr, FMT "\t- file : " __FILE__ ", line %d\n", ## ARGS, __LINE__); }
#endif

int feedbackInit(tfbdrvset * plugin)
{
    int i;
    static int firsttime = 1;
    if (firsttime == 1) {
        for (i = 0; i < FB_MAX_MEMBERS; i++)
            feedbackPluginList[i] = NULL;
        firsttime = 0;
    }

    if (plugin == NULL)
        return ERROR;

    for (i = 0; i < FB_MAX_MEMBERS; i++) {
        if (feedbackPluginList[i] == plugin) {
            plugin->id = i;
            Debug(3, "Feedback plugin already registered: id=%d name=%s", i, plugin->name ? plugin->name : "<unnamed>");
            return i;
        }
    }

    for (i = 0; i < FB_MAX_MEMBERS; i++) {
        if (feedbackPluginList[i] == NULL) {
            feedbackPluginList[i] = plugin;
            plugin->id = i;
            Debug(2, "Registered feedback plugin: id=%d name=%s", i, plugin->name ? plugin->name : "<unnamed>");
            return i;
        }
    }
    return ERROR;
}

int feedbackGetPluginById(int id, tfbdrvset ** feedbackPlugin)
{
    if (id < 0 || id >= FB_MAX_MEMBERS)
        return ERROR;
    *feedbackPlugin = feedbackPluginList[id];
    return OK;
}

int feedbackGetPluginNameById(int id, char *name, size_t n)
{
    if (id < 0 || id >= FB_MAX_MEMBERS)
        return ERROR;
    if (feedbackPluginList[id] != NULL) {
        if (feedbackPluginList[id]->name != NULL) {
            strncpy(name, feedbackPluginList[id]->name, n);
        }
    }
    return OK;
}

int feedbackGetPluginId(int *id)
{
    if (pfbDrvDset == NULL)
        return ERROR;
    *id = pfbDrvDset->id;
    return OK;
}
