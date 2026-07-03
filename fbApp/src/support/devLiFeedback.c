// SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin für Materialien und Energie GmbH
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <alarm.h>
#include <dbDefs.h>
#include <recGbl.h>
#include <devSup.h>
#include <epicsExport.h>
#include <longinRecord.h>
#include "fbCore.h"
#include "devFeedback.h"

/* Create the dset for devLiGetOPM */
static long init_record();
static long read_longin();

struct {
    long number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN read_longin;
    DEVSUPFUN special_linconv;
} devLiFBCommon = {
    6,
    NULL,
    NULL,
    init_record,
    NULL,
    read_longin,
    NULL
};

epicsExportAddress(dset, devLiFBCommon);

static long init_record(struct longinRecord *plongin)
{
    int rc;
    structFbCDefPVT *pfbcdefpvt;
    tfbdrvpar *pfb;

    pfbcdefpvt = (structFbCDefPVT *) calloc(1, sizeof(structFbCDefPVT));
    pfbcdefpvt->fb = (tfbdrvpar *) calloc(1, sizeof(tfbdrvpar));
    pfb = pfbcdefpvt->fb;
    plongin->dpvt = (void *)pfbcdefpvt; /* Record's device private */
    if (pfbDrvDset == NULL) {
        recGblRecordError(ERROR, (void *)plongin, "devLiFeedback (init_record) No Plugin");
        return ERROR;
    }
    if (pfbDrvDset->parameter == NULL) {
        recGblRecordError(ERROR, (void *)plongin, "devLiFeedback (init_record) No Plugin");
        return ERROR;
    }
    pfb->inode = pfbDrvDset->parameter->inode;
    pfb->onode = pfbDrvDset->parameter->onode;
    rc = fbParseDeviceName(plongin->inp.value.instio.string, &(pfbcdefpvt->device));

    if (plongin->inp.type != INST_IO || rc == ERROR) {
        recGblRecordError(ERROR, (void *)plongin, "devLiFBCommon (init_record) Illegal INP field");
        return ERROR;
    }
    return OK;
}

static long read_longin(struct longinRecord *plongin)
{
    structFbCDefPVT *pfbcdefpvt;
    tfbdrvpar *pfb;
    unsigned int flags;
    int ival;
    double dval;

    if (pfbDrvDset == NULL)
        return ERROR;
    if (pfbDrvDset->parameter == NULL) {
        return ERROR;
    }
    pfbcdefpvt = (structFbCDefPVT *) plongin->dpvt;
    pfb = pfbcdefpvt->fb;

    if (pfbcdefpvt->device == FBINIT) {
        plongin->val =
            (long)fbInit(DEVFB_INPCARD, DEVFB_INPSIGNAL, DEVFB_OUTCARD, DEVFB_OUTSIGNAL, DEVFB_INODE, DEVFB_ONODE, DEVFB_RATE, DEVFB_PRIORITY);
    }

    else if (pfbcdefpvt->device == FBOPEN) {
        plongin->val = (long)fbOpen();
    } else if (pfbcdefpvt->device == FBCLOSE) {
        plongin->val = (long)fbClose();
    } else if (pfbcdefpvt->device == FBINODE) {
        pfb->inode = pfbDrvDset->parameter->inode;
        plongin->val = (long)pfbDrvDset->parameter->inode;
    } else if (pfbcdefpvt->device == FBONODE) {
        pfb->onode = pfbDrvDset->parameter->onode;
        plongin->val = (long)pfbDrvDset->parameter->onode;
    } else if (pfbcdefpvt->device == FBIVAL) {
        plongin->val = (long)pfbDrvDset->parameter->ival;
    } else if (pfbcdefpvt->device == FBVAL) {
        plongin->val = (long)pfbDrvDset->parameter->lval;
    } else if (pfbcdefpvt->device == FBUIVAL) {
        plongin->val = (long)pfbDrvDset->parameter->uival;
    } else if (pfbcdefpvt->device == FBSVAL) {
        plongin->val = (long)pfbDrvDset->parameter->sval;
    } else if (pfbcdefpvt->device == FBDVAL) {
        plongin->val = (long)pfbDrvDset->parameter->dval;
    } else if (pfbcdefpvt->device == FBISIG) {
        plongin->val = (long)pfbDrvDset->parameter->inpsignal;
    } else if (pfbcdefpvt->device == FBOSIG) {
        plongin->val = (long)pfbDrvDset->parameter->outsignal;
    } else if (pfbcdefpvt->device == FBTRIGGER) {
        plongin->val = fbTrigger();
    } else if (pfbcdefpvt->device == FBSTATUS) {
        plongin->val = pfbDrvDset->parameter->status;
    } else if (pfbcdefpvt->device == FBFLAGS) {
        fbGetFlags(&flags);
        plongin->val = (long)flags;
    } else if (pfbcdefpvt->device == FBHOME) {
        plongin->val = fbHome(pfbDrvDset->parameter->flags);
    } else if (pfbcdefpvt->device == FBRATE) {
        if (fbGetRate(&ival) == OK) {
            plongin->val = (long)ival;
        }
    } else if (pfbcdefpvt->device == FBSOFTTRIGGERRATE) {
        if (fbGetSoftTriggerRate(&dval) == OK) {
            plongin->val = (long)dval;
        }
    } else if (pfbcdefpvt->device == FBPRIORITY) {
        if (fbGetPriority(&ival) == OK) {
            plongin->val = (long)ival;
        }
    } else {
        Debug(0, ", Reason called: %d: No valid input field\n", pfbcdefpvt->device);
        return ERROR;
    }
    return OK;
}
