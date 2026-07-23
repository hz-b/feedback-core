// SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin für Materialien und Energie GmbH
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <devSup.h>
#include <mbboRecord.h>
#include <mbbiRecord.h>
#include <dbCommon.h>
#include <alarm.h>
#include <recGbl.h>
#include <epicsExport.h>
#include "fbCore.h"
#include "fbRegistry.h"
#include "devFeedback.h"

#define MAX_EPICS_STR_SIZE          38
#define MAX_EPICS_NAME_SIZE         59
#define MAX_EPICS_MBB_STRING_SIZE   26
//#define UPDATE_STRING_VALUES    2
//#define UPDATE_VALUE            0
#define MBBOTYPE    0
#define MBBITYPE    1

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

static long mbbi_init_record();
static long mbbo_init_record();
static long mbbi_read();
static long mbbo_write();

struct {
    long number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN write_mbbo;
    DEVSUPFUN special_linconv;
} devFbSetMbbo = {
    6, NULL, NULL, mbbo_init_record, NULL, mbbo_write, NULL
};

epicsExportAddress(dset, devFbSetMbbo);

struct {
    long number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN read_mbbi;
} devFbGetMbbi = {
    5, NULL, NULL, mbbi_init_record, NULL, mbbi_read
};

epicsExportAddress(dset, devFbGetMbbi);

static void get_strval(char *dest, int index, int device)
{
    const char *modeName;

    if (dest == NULL)
        return;
    dest[0] = '\0';

    switch (device) {
    case FBPLUGIN:
        feedbackGetPluginNameById(index, dest, MAX_EPICS_MBB_STRING_SIZE - 1);
        dest[MAX_EPICS_MBB_STRING_SIZE - 1] = '\0';
        break;
    case FBINODE:
        if (index < 8)
            fbGetNodeName(index, dest, MAX_EPICS_MBB_STRING_SIZE - 1);
        break;
    case FBONODE:
        if (index < 8)
            fbGetNodeName(index + 8, dest, MAX_EPICS_MBB_STRING_SIZE - 1);
        break;
    case FBTRIGGERMODE:
        if (index <= FB_TRIGGER_MODE_MANUAL) {
            modeName = fbGetTriggerModeName(index);
            strncpy(dest, modeName, MAX_EPICS_MBB_STRING_SIZE - 1);
            dest[MAX_EPICS_MBB_STRING_SIZE - 1] = '\0';
        }
        break;
    default:
        break;
    }
    Debug(10, "%s", dest);
}

static int updatestrings(struct dbCommon *precord, int device, int type)
{
    mbboRecord *pmbbo = (mbboRecord *) precord;
    mbbiRecord *pmbbi = (mbbiRecord *) precord;

    switch (type) {
    case MBBOTYPE:
        get_strval(pmbbo->zrst, 0, device);
        get_strval(pmbbo->onst, 1, device);
        get_strval(pmbbo->twst, 2, device);
        get_strval(pmbbo->thst, 3, device);
        get_strval(pmbbo->frst, 4, device);
        get_strval(pmbbo->fvst, 5, device);
        get_strval(pmbbo->sxst, 6, device);
        get_strval(pmbbo->svst, 7, device);
        get_strval(pmbbo->eist, 8, device);
        get_strval(pmbbo->nist, 9, device);
        get_strval(pmbbo->test, 10, device);
        get_strval(pmbbo->elst, 11, device);
        get_strval(pmbbo->tvst, 12, device);
        get_strval(pmbbo->ttst, 13, device);
        get_strval(pmbbo->ftst, 14, device);
        get_strval(pmbbo->ffst, 15, device);
        break;
    case MBBITYPE:
        get_strval(pmbbi->zrst, 0, device);
        get_strval(pmbbi->onst, 1, device);
        get_strval(pmbbi->twst, 2, device);
        get_strval(pmbbi->thst, 3, device);
        get_strval(pmbbi->frst, 4, device);
        get_strval(pmbbi->fvst, 5, device);
        get_strval(pmbbi->sxst, 6, device);
        get_strval(pmbbi->svst, 7, device);
        get_strval(pmbbi->eist, 8, device);
        get_strval(pmbbi->nist, 9, device);
        get_strval(pmbbi->test, 10, device);
        get_strval(pmbbi->elst, 11, device);
        get_strval(pmbbi->tvst, 12, device);
        get_strval(pmbbi->ttst, 13, device);
        get_strval(pmbbi->ftst, 14, device);
        get_strval(pmbbi->ffst, 15, device);
        break;
    default:
        Debug(0, "Unknown type %d", type);
        return ERROR;
    }
    return OK;
}

static long mbbo_init_record(struct mbboRecord *pmbbo)
{
    int rc;
    structFbCDefPVT *pdpvt;

    pdpvt = (structFbCDefPVT *) calloc(1, sizeof(structFbCDefPVT));
    pmbbo->dpvt = (void *)pdpvt;
    rc = fbParseDeviceName(pmbbo->out.value.instio.string, &(pdpvt->device));
    if (pmbbo->out.type != INST_IO || rc == ERROR) {
        Debug(0, "%d:%s", pdpvt->device, pmbbo->out.value.instio.string);
        recGblRecordError(ERROR, (void *)pmbbo, "devFbSetMbbo (init_record) Illegal OUT field");
        return ERROR;
    }
    updatestrings((dbCommon *) pmbbo, pdpvt->device, MBBOTYPE);
    return OK;
}

static long mbbi_init_record(struct mbbiRecord *pmbbi)
{
    int rc;
    structFbCDefPVT *pdpvt;

    pdpvt = (structFbCDefPVT *) calloc(1, sizeof(structFbCDefPVT));
    pdpvt->firsttime = 0;

    pmbbi->dpvt = (void *)pdpvt;
    rc = fbParseDeviceName(pmbbi->inp.value.instio.string, &(pdpvt->device));
    if (pmbbi->inp.type != INST_IO || rc == ERROR) {
        Debug(0, "%d:%s", pdpvt->device, pmbbi->inp.value.instio.string);
        recGblRecordError(ERROR, (void *)pmbbi, "devFbGetMbbi (init_record) Illegal INP field");
        return ERROR;
    }
    updatestrings((dbCommon *) pmbbi, pdpvt->device, MBBITYPE);
    return OK;
}

static long mbbi_read(struct mbbiRecord *pmbbi)
{
    structFbCDefPVT *pdpvt;
    int ival;
    short inode;
    short onode;

    pdpvt = (structFbCDefPVT *)pmbbi->dpvt;
    ival = 0;
    inode = 0;
    onode = 0;
    updatestrings((dbCommon *)pmbbi, pdpvt->device, MBBITYPE);

    if (pdpvt->device == FBPLUGIN) {
        if (feedbackGetPluginId(&ival) != OK)
            return ERROR;
    } else if (pdpvt->device == FBTRIGGERMODE) {
        if (fbGetTriggerMode(&ival) != OK)
            return ERROR;
    } else if (pdpvt->device == FBINODE ||
               pdpvt->device == FBONODE) {
        if (fbGetNodes(&inode, &onode) != OK) {
            Debug(1, "%s: Error with active nodes.", pmbbi->name);
            return ERROR;
        }
        if (pdpvt->device == FBINODE)
            ival = (int)inode;
        else
            ival = (int)onode;
    } else {
        return ERROR;
    }

    pmbbi->val = ival;
    return 2;
}

static long mbbo_write(struct mbboRecord *pmbbo)
{
    structFbCDefPVT *pdpvt;
    int id;
    int mode;
    short inode;
    short onode;

    pdpvt = (structFbCDefPVT *)pmbbo->dpvt;
    updatestrings((dbCommon *)pmbbo, pdpvt->device, MBBOTYPE);

    if (pdpvt->device == FBPLUGIN) {
        if (pdpvt->firsttime == 0) {
            pdpvt->firsttime = 1;
            if (feedbackGetPluginId(&id) == OK)
                pmbbo->val = (int)id;
            return 0;
        }
        if (fbDeactivate() != OK ||
            fbActivate((int)pmbbo->val) != OK) {
            recGblSetSevr(pmbbo, WRITE_ALARM, INVALID_ALARM);
            Debug(0, "%s: Cannot change active plugin.", pmbbo->name);
            return ERROR;
        }
        return 0;
    }

    if (pdpvt->device == FBTRIGGERMODE) {
        if (pdpvt->firsttime == 0) {
            pdpvt->firsttime = 1;
            if (fbGetTriggerMode(&mode) == OK)
                pmbbo->val = (unsigned short)mode;
            return 0;
        }
        if (fbSetTriggerMode((int)pmbbo->val) != OK) {
            recGblSetSevr(pmbbo, WRITE_ALARM, INVALID_ALARM);
            Debug(0, "%s: Cannot change trigger mode while running or without a soft rate.", pmbbo->name);
            return ERROR;
        }
        return 0;
    }

    if (pdpvt->device != FBINODE && pdpvt->device != FBONODE)
        return ERROR;

    if (fbGetNodes(&inode, &onode) != OK) {
        Debug(1, "%s: Error with active nodes.", pmbbo->name);
        return ERROR;
    }

    if (pdpvt->device == FBINODE) {
        if (pdpvt->firsttime == 0) {
            pdpvt->firsttime = 1;
            pmbbo->val = (unsigned short)inode;
            return 0;
        }
        if (fbSetNodes((short)pmbbo->val, onode) != OK) {
            recGblSetSevr(pmbbo, WRITE_ALARM, INVALID_ALARM);
            Debug(0, "%s: Cannot activate input node.", pmbbo->name);
            return ERROR;
        }
    } else {
        if (pdpvt->firsttime == 0) {
            pdpvt->firsttime = 1;
            pmbbo->val = (unsigned short)onode;
            return 0;
        }
        if (fbSetNodes(inode, (short)pmbbo->val) != OK) {
            recGblSetSevr(pmbbo, WRITE_ALARM, INVALID_ALARM);
            Debug(0, "%s: Cannot activate output node.", pmbbo->name);
            return ERROR;
        }
    }
    return 0;
}
