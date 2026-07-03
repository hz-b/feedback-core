// SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin für Materialien und Energie GmbH
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <dbDefs.h>
#include <devSup.h>
#include <recGbl.h>
#include <alarm.h>
#include <aiRecord.h>
#include <dbScan.h>
#include <epicsExport.h>

#include "fbCore.h"
#include "fbAnalog.h"

#ifndef OK
#define OK 0
#endif
#ifndef ERROR
#define ERROR -1
#endif

#ifdef NODEBUG
#define Debug(L,FMT,ARGS...) ;
#else
#define Debug(L, FMT, ARGS...) { if(L <= fb_common_debug) \
    fprintf(stderr, FMT "\t- file : " __FILE__ ", line %d\n", ## ARGS, __LINE__); }
#endif

typedef struct devAiFbPvt {
    unsigned short channel;
} devAiFbPvt;

static long init_record(struct aiRecord *pai);
static long get_ioint_info(int cmd, struct dbCommon *precord, IOSCANPVT * ppvt);
static long read_ai(struct aiRecord *pai);

struct {
    long number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN read_ai;
    DEVSUPFUN special_linconv;
} devAiFBAnalog = {
    6,
    NULL,
    NULL,
    init_record,
    get_ioint_info,
    read_ai,
    NULL
};

epicsExportAddress(dset, devAiFBAnalog);

static int parse_ain_channel(const char *string, unsigned short *channel)
{
    const char *p;
    char *endp;
    long value;

    if (string == NULL || channel == NULL)
        return ERROR;

    p = string;
    while (*p != '\0' && isspace((int)(unsigned char)*p))
        p++;

    if (*p == '@')
        p++;

    while (*p != '\0' && isspace((int)(unsigned char)*p))
        p++;

    if (strncmp(p, "AIN", 3) == 0) {
        p += 3;
        while (*p != '\0' && isspace((int)(unsigned char)*p))
            p++;
        if (*p != ',')
            return ERROR;
        p++;
    }

    while (*p != '\0' && isspace((int)(unsigned char)*p))
        p++;

    value = strtol(p, &endp, 0);
    if (endp == p)
        return ERROR;

    while (*endp != '\0' && isspace((int)(unsigned char)*endp))
        endp++;
    if (*endp != '\0')
        return ERROR;

    if (value < 0 || value >= FB_ANALOG_MAX_CHANNELS)
        return ERROR;

    *channel = (unsigned short)value;
    return OK;
}

static long init_record(struct aiRecord *pai)
{
    devAiFbPvt *pvt;

    if (pai == NULL)
        return ERROR;

    if (pai->inp.type != INST_IO) {
        recGblRecordError(ERROR, (void *)pai, "devAiFBAnalog (init_record) Illegal INP field");
        return ERROR;
    }

    pvt = (devAiFbPvt *) calloc(1, sizeof(devAiFbPvt));
    if (pvt == NULL) {
        recGblRecordError(ERROR, (void *)pai, "devAiFBAnalog (init_record) calloc failed");
        return ERROR;
    }

    if (parse_ain_channel(pai->inp.value.instio.string, &pvt->channel) != OK) {
        Debug(0, "%s: Bad INP '%s', expected @AIN,<channel>", pai->name, pai->inp.value.instio.string);
        free(pvt);
        recGblRecordError(ERROR, (void *)pai, "devAiFBAnalog (init_record) Bad INP, expected @AIN,<channel>");
        return ERROR;
    }

    if (fbAnalogInit() != OK) {
        free(pvt);
        recGblRecordError(ERROR, (void *)pai, "devAiFBAnalog (init_record) fbAnalogInit failed");
        return ERROR;
    }

    pai->dpvt = (void *)pvt;
    Debug(2, "%s: initialized feedback analog input channel %u", pai->name, (unsigned int)pvt->channel);

    return OK;
}

static long get_ioint_info(int cmd, struct dbCommon *precord, IOSCANPVT * ppvt)
{
    struct aiRecord *pai;
    devAiFbPvt *pvt;

    if (precord == NULL || ppvt == NULL)
        return ERROR;

    pai = (struct aiRecord *)precord;
    pvt = (devAiFbPvt *) pai->dpvt;
    if (pvt == NULL)
        return ERROR;

    Debug(5, "%s: get_ioint_info cmd=%d channel=%u", pai->name, cmd, (unsigned int)pvt->channel);

    return fbAnalogGetIoScanPvt(pvt->channel, ppvt);
}

static long read_ai(struct aiRecord *pai)
{
    devAiFbPvt *pvt;
    double value;

    if (pai == NULL)
        return ERROR;

    pvt = (devAiFbPvt *) pai->dpvt;
    if (pvt == NULL)
        return ERROR;

    if (fbAnalogGet(pvt->channel, &value) != OK) {
        Debug(0, "%s: channel %u has no valid analog value yet", pai->name, (unsigned int)pvt->channel);
        recGblSetSevr(pai, READ_ALARM, INVALID_ALARM);
        return ERROR;
    }

    pai->val = value;
    pai->udf = FALSE;
    Debug(8, "%s: read channel %u value=%f", pai->name, (unsigned int)pvt->channel, value);

    return 2;
}
