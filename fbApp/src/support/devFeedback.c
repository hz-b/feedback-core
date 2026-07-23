// SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin für Materialien und Energie GmbH
// SPDX-License-Identifier: MIT

#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include "devFeedback.h"

typedef struct fbDeviceNameEntry {
    const char *name;
    enum enumFbCDevice device;
} fbDeviceNameEntry;

static const fbDeviceNameEntry fbDeviceNameTable[] = {
    {FB_INIT_STRING, FBINIT},
    {FB_OPEN_STRING, FBOPEN},
    {FB_CLOSE_STRING, FBCLOSE},
    {FB_ICARD_STRING, FBICARD},
    {FB_ISIG_STRING, FBISIG},
    {FB_OCARD_STRING, FBOCARD},
    {FB_OSIG_STRING, FBOSIG},
    {FB_RATE_STRING, FBRATE},
    {FB_PRIORITY_STRING, FBPRIORITY},
    {FB_STATUS_STRING, FBSTATUS},
    {FB_VAL_STRING, FBVAL},
    {FB_IVAL_STRING, FBIVAL},
    {FB_UIVAL_STRING, FBUIVAL},
    {FB_SVAL_STRING, FBSVAL},
    {FB_DVAL_STRING, FBDVAL},
    {FB_INODE_STRING, FBINODE},
    {FB_ONODE_STRING, FBONODE},
    {FB_TRIGGER_STRING, FBTRIGGER},
    {FB_HOME_STRING, FBHOME},
    {FB_FLAGS_STRING, FBFLAGS},
    {FB_PROCONODE_STRING, FBPROC_ONODE},
    {FB_PROCINODE_STRING, FBPROC_INODE},
    {FB_PLUGIN_STRING, FBPLUGIN},
    {FB_SOFTTRIGGERRATE_STRING, FBSOFTTRIGGERRATE},
    {FB_TRIGGERMODE_STRING, FBTRIGGERMODE},
    {FB_DATASETEVENTS_STRING, FBDATASETEVENTS}
};

static size_t fbDeviceNameTableCount(void)
{
    return sizeof(fbDeviceNameTable) / sizeof(fbDeviceNameTable[0]);
}

static int fbCopyDeviceToken(const char *string, char *token, size_t tokenSize)
{
    size_t i;
    size_t k;
    unsigned char ch;

    if (string == NULL || token == NULL || tokenSize == 0u)
        return ERROR;

    k = 0u;
    for (i = 0u; string[i] != ',' && string[i] != '\0'; i++) {
        ch = (unsigned char)string[i];
        if (isspace((int)ch) == 0 && iscntrl((int)ch) == 0) {
            if (k + 1u >= tokenSize)
                return ERROR;
            token[k] = (char)ch;
            k++;
        }
    }

    token[k] = '\0';
    return OK;
}

int fbLookupDeviceName(const char *name, int *device)
{
    size_t i;

    if (name == NULL || device == NULL)
        return ERROR;

    for (i = 0u; i < fbDeviceNameTableCount(); i++) {
        if (strcmp(name, fbDeviceNameTable[i].name) == 0) {
            *device = (int)fbDeviceNameTable[i].device;
            return OK;
        }
    }

    return ERROR;
}

const char *fbDeviceNameFromId(int device)
{
    size_t i;

    for (i = 0u; i < fbDeviceNameTableCount(); i++) {
        if ((int)fbDeviceNameTable[i].device == device)
            return fbDeviceNameTable[i].name;
    }

    return NULL;
}

int fbParseDeviceName(char *string, int *device)
{
    char token[MAX_DEVCHRS];

    if (fbCopyDeviceToken(string, token, sizeof(token)) != OK)
        return ERROR;

    return fbLookupDeviceName(token, device);
}                                       /* fbParseDeviceName */
