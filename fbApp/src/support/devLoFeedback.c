// SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin für Materialien und Energie GmbH
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <alarm.h>
#include <dbDefs.h>
#include <recGbl.h>
#include <devSup.h>
#include <epicsExport.h>
#include <longoutRecord.h>
#include <dbScan.h>
#include "fbCore.h"
#include "devFeedback.h"

/* Create the dset for devLiGetOPM */
static long init_record();
static long write_longout();

struct {
    long number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN write_longout;
    DEVSUPFUN special_linconv;
} devLoFBCommon = {
    6,
    NULL,
    NULL,
    init_record,
    NULL,
    write_longout,
    NULL
};

epicsExportAddress(dset, devLoFBCommon);

static long init_record(struct longoutRecord *plongout)
{
    int rc;
    structFbCDefPVT *pfbcdefpvt;
    tfbdrvpar *pfb;

    pfbcdefpvt = (structFbCDefPVT *) calloc(1, sizeof(structFbCDefPVT));
    pfbcdefpvt->firsttime = 1;
    plongout->dpvt = (void *)pfbcdefpvt;        /* Record's device private */

    rc = fbParseDeviceName(plongout->out.value.instio.string, &(pfbcdefpvt->device));

    if (plongout->out.type != INST_IO || rc == ERROR) {
        recGblRecordError(ERROR, (void *)plongout, "devLoFBCommon (init_record) Illegal INP field");
        return ERROR;
    }

    if (pfbDrvDset == NULL) {
        Debug(1, "init_record() No Plugin %s", plongout->name);
        return OK;
    }
    if (pfbDrvDset->parameter == NULL) {
        Debug(1, "init_record() No Plugin %s", plongout->name);
        return OK;
    }
    pfbcdefpvt->fb = pfbDrvDset->parameter;
    pfb = pfbcdefpvt->fb;
    pfb->inode = pfbDrvDset->parameter->inode;
    pfb->onode = pfbDrvDset->parameter->onode;
    return OK;
}

static long write_longout(struct longoutRecord *plongout)
{
    structFbCDefPVT *pfbcdefpvt;
    tfbdrvpar *pfb;
    INTSUPFUN *InputNodeFunction;
    INTSUPFUN *OutputNodeFunction;
    static int olddataselect = 0;
    int newdataselect;

    pfbcdefpvt = (structFbCDefPVT *) plongout->dpvt;

    if (pfbcdefpvt->device == FBDATASETEVENTS) {
        // to heaviside this signal
        if (plongout->val > 0)
            newdataselect = 1;
        else
            newdataselect = 0;
        Debug(9, "newdataselect=%d", newdataselect);
        if (olddataselect - newdataselect != 0) {
            // event dataset2 is event dataset1 + 1 by definition
            post_event(FB_DATA_SET1_READY_EVENT + newdataselect);
            Debug(1, "posting event %d + %d", FB_DATA_SET1_READY_EVENT, newdataselect);
        }
        olddataselect = newdataselect;
        return OK;
    }

    if (pfbcdefpvt->device == FBTRIGGERMODE) {
        if (fbSetTriggerMode((int)plongout->val) != OK) {
            recGblSetSevr(plongout, WRITE_ALARM, INVALID_ALARM);
            return ERROR;
        }
        return OK;
    }

    if (pfbDrvDset == NULL || pfbDrvDset->parameter == NULL)
        return ERROR;

    pfb = pfbcdefpvt->fb;
    if (pfb == NULL)
        pfb = pfbDrvDset->parameter;
    InputNodeFunction = (INTSUPFUN *)&pfbDrvDset->input_node1;
    OutputNodeFunction = (INTSUPFUN *)&pfbDrvDset->output_node1;

    if (pfbcdefpvt->device == FBVAL)
        pfb->lval = plongout->val;
    else if (pfbcdefpvt->device == FBIVAL)
        pfb->ival = (int)plongout->val;
    else if (pfbcdefpvt->device == FBUIVAL)
        pfb->uival = (unsigned int)plongout->val;
    else if (pfbcdefpvt->device == FBSVAL)
        pfb->sval = (short)plongout->val;
    else if (pfbcdefpvt->device == FBDVAL)
        pfb->dval = (double)plongout->val;
    else if (pfbcdefpvt->device == FBPROC_ONODE) {
        if (pfbcdefpvt->firsttime == 1) {
            pfbcdefpvt->firsttime = 0;
            plongout->val = pfb->procout;       /* set val to feedback support specific default output node */
        } else if (plongout->val >= 0 && plongout->val <= FB_MAX_NODES) {
            if (OutputNodeFunction[plongout->val]) {
                OutputNodeFunction[plongout->val] (pfb);
            }
        }
    }                                   /* FBPROC_ONODE */
    else if (pfbcdefpvt->device == FBPROC_INODE) {
        if (pfbcdefpvt->firsttime == 1) {
            pfbcdefpvt->firsttime = 0;
            plongout->val = pfb->procin;        /* set val to feedback support specific default input node */
        }
        if (plongout->val >= 0 && plongout->val <= FB_MAX_NODES) {
            if (InputNodeFunction[plongout->val]) {
                InputNodeFunction[plongout->val] (pfb);
            }
        }
    }                                   /* FBPROC_INODE */
    else if (pfbcdefpvt->device == FBINODE) {
        Debug(2, ":Setting Nodes : inode = %d\tonode = %d", pfb->inode, pfb->onode);
        if (fbSetNodes((short)plongout->val, pfb->onode) != OK) {
            Debug(0, "ERROR in Common Feedback Interface: %d, %d\n", pfb->inode, pfb->onode);
            recGblSetSevr(plongout, READ_ALARM, INVALID_ALARM);
        }

    }                                   /* FBINODE */
    else if (pfbcdefpvt->device == FBONODE) {
        Debug(2, ":Setting Nodes : inode = %d\tonode = %d", pfb->inode, pfb->onode);
        if (fbSetNodes(pfb->inode, (short)plongout->val) != OK) {
            Debug(0, "ERROR in Common Feedback Interface: %d, %d\n", pfb->inode, pfb->onode);
            recGblSetSevr(plongout, READ_ALARM, INVALID_ALARM);
        }
    }                                   /* FBONODE */
    /* TODO: in/out signal + card, rate, priority */
    else if (pfbcdefpvt->device == FBOSIG) {
        Debug(2, ":Setting Output Signal : outsignal = %d\tinpsignal = %d", pfb->outsignal, pfb->inpsignal);
        if (fbConfigure(pfb->inpcard, pfb->inpsignal, pfb->outcard, (short)plongout->val, pfb->inode, pfb->onode, pfb->rate, pfb->priority) != OK) {
            Debug(0, "ERROR in Common Feedback Interface: %d, %d\n", pfb->outsignal, pfb->inpsignal);
            recGblSetSevr(plongout, READ_ALARM, INVALID_ALARM);
        }                               /* if (bConfigure!=OK) */

    }                                   /* FBOSIG */
    else if (pfbcdefpvt->device == FBISIG) {
        Debug(2, ":Setting Input Signal : outsignal = %d\tinpsignal = %d", pfb->outsignal, pfb->inpsignal);
        if (fbConfigure(pfb->inpcard, (short)plongout->val, pfb->outcard, pfb->outsignal, pfb->inode, pfb->onode, pfb->rate, pfb->priority) != OK) {
            Debug(0, "ERROR in Common Feedback Interface: %d, %d\n", pfb->outsignal, pfb->inpsignal);
            recGblSetSevr(plongout, READ_ALARM, INVALID_ALARM);
        }                               /* if (bConfigure!=OK) */
    }                                   /* FBISIG */
    else if (pfbcdefpvt->device == FBHOME) {
        Debug(2, ":Home Flags = %x", (unsigned int)plongout->val);
        if (fbHome((unsigned int)plongout->val) != OK) {
            Debug(0, "ERROR in feedback setting %d\n", plongout->val);
            // FIXME: WRITE_ALARM
            recGblSetSevr(plongout, READ_ALARM, INVALID_ALARM);
        }                               /* if (bConfigure!=OK) */

    }                                   /* FBHOME */
    else if (pfbcdefpvt->device == FBRATE) {
        Debug(2, ":Rate = %d", plongout->val);
        if (fbSetRate((int)plongout->val) != OK) {
            Debug(0, "ERROR in feedback setting %d\n", plongout->val);
            recGblSetSevr(plongout, READ_ALARM, INVALID_ALARM);
        }
    }                                   /* FBRATE */
    else if (pfbcdefpvt->device == FBSOFTTRIGGERRATE) {
        Debug(1, ":Rate = %d", plongout->val);
        if (fbSetSoftTriggerRate((double)plongout->val) != OK) {
            Debug(0, "ERROR in feedback setting %d\n", plongout->val);
            recGblSetSevr(plongout, READ_ALARM, INVALID_ALARM);
        }
    }                                   /* FBSOFTTRIGGERRATE */
    else if (pfbcdefpvt->device == FBPRIORITY) {
        Debug(2, ":Priority = %d", plongout->val);
        if (fbSetPriority((int)plongout->val) != OK) {
            Debug(0, "ERROR in feedback setting %d\n", plongout->val);
            recGblSetSevr(plongout, READ_ALARM, INVALID_ALARM);
        }
    }                                   /* FBRATE */
    else {
        Debug(0, "%d: No valid input field\n", pfbcdefpvt->device);
        return (ERROR);
    }
    return (OK);
}
