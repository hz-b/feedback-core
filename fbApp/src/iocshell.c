// SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin für Materialien und Energie GmbH
// SPDX-License-Identifier: MIT

#include <string.h>
#include <epicsStdioRedirect.h>
#include <iocsh.h>
#include <epicsExport.h>
#include <registryFunction.h>

#include "fbCore.h"
#include "fbAnalog.h"

#ifdef NODEBUG
#define Debug(L,FMT,ARGS...) ;
#else
#define Debug(L, FMT, ARGS...) { if(L <= fb_common_debug) \
	fprintf(stderr, FMT "\t- file : " __FILE__ ", line %d\n", ## ARGS, __LINE__); }
#endif

static const iocshFuncDef fbAnalogPluginInstantiateFuncDef = { "fbAnalogPluginInstantiate", 0, NULL };

static void fbAnalogPluginInstantiateCallFunc(const iocshArgBuf * args)
{
    if (fbAnalogPluginInstantiate() != OK)
        printf("Unable to register/activate analog feedback plugin\n");
}

static const iocshFuncDef fbAnalogInitPluginFuncDef = { "fbAnalogInitPlugin", 0, NULL };

static void fbAnalogInitPluginCallFunc(const iocshArgBuf * args)
{
    if (fbAnalogPluginInstantiate() != OK)
        printf("Unable to register/activate analog feedback plugin\n");
}

static const iocshArg fbAnalogSetFilterDelayArg0 = { "delaySamples", iocshArgDouble };
static const iocshArg fbAnalogSetFilterDelayArg1 = { "channel", iocshArgInt };

static const iocshArg *fbAnalogSetFilterDelayArgs[] = {
    &fbAnalogSetFilterDelayArg0,
    &fbAnalogSetFilterDelayArg1
};

static const iocshFuncDef fbAnalogSetFilterDelayFuncDef = {
    "fbAnalogSetFilterDelay",
    2,
    fbAnalogSetFilterDelayArgs
};

static void fbAnalogSetFilterDelayCallFunc(const iocshArgBuf * args)
{
    int status;
    double delaySamples;
    int channel;

    delaySamples = args[0].dval;
    channel = args[1].ival;

    if (channel < 0 || channel >= FB_ANALOG_MAX_CHANNELS) {
        printf("fbAnalogSetFilterDelay: invalid channel=%d, valid range is 0..%d\n", channel, FB_ANALOG_MAX_CHANNELS - 1);
        return;
    }

    status = fbAnalogSetFilterDelay(delaySamples, (unsigned short)channel);
    if (status == OK) {
        printf("fbAnalogSetFilterDelay: channel=%d delaySamples=%f\n", channel, delaySamples);
    } else {
        printf("fbAnalogSetFilterDelay: failed channel=%d delaySamples=%f status=%d\n", channel, delaySamples, status);
    }
}

static const iocshArg fbAnalogSetArg0 = { "channel", iocshArgInt };
static const iocshArg fbAnalogSetArg1 = { "value", iocshArgDouble };
static const iocshArg *fbAnalogSetArgs[] = { &fbAnalogSetArg0, &fbAnalogSetArg1 };
static const iocshFuncDef fbAnalogSetFuncDef = { "fbAnalogSet", 2, fbAnalogSetArgs };

static void fbAnalogSetCallFunc(const iocshArgBuf * args)
{
    int rc;

    rc = fbAnalogSet((unsigned short)args[0].ival, args[1].dval);
    Debug(2, "fbAnalogSet: channel=%d value=%f rc=%d", args[0].ival, args[1].dval, rc);
}

static const iocshArg fbAnalogGetArg0 = { "channel", iocshArgInt };
static const iocshArg *fbAnalogGetArgs[] = { &fbAnalogGetArg0 };
static const iocshFuncDef fbAnalogGetFuncDef = { "fbAnalogGet", 1, fbAnalogGetArgs };

static void fbAnalogGetCallFunc(const iocshArgBuf * args)
{
    int rc;
    double value;

    rc = fbAnalogGet((unsigned short)args[0].ival, &value);
    if (rc == OK)
        printf("fbAnalogGet: channel=%d value=%f\n", args[0].ival, value);
    else
        printf("fbAnalogGet: channel=%d unavailable, rc=%d\n", args[0].ival, rc);
}

/*
	int fbInit(short inpcard, short inpsignal,short outcard, short outsignal, short inode, short onode, int rate, int priority)
*/
static const iocshArg fbInitArg0 = { "inpcard", iocshArgInt };
static const iocshArg fbInitArg1 = { "inpsignal", iocshArgInt };
static const iocshArg fbInitArg2 = { "outcard", iocshArgInt };
static const iocshArg fbInitArg3 = { "outsignal", iocshArgInt };
static const iocshArg fbInitArg4 = { "inode", iocshArgInt };
static const iocshArg fbInitArg5 = { "onode", iocshArgInt };
static const iocshArg fbInitArg6 = { "rate", iocshArgInt };
static const iocshArg fbInitArg7 = { "priority", iocshArgInt };
static const iocshArg *fbInitArgs[] = { &fbInitArg0, &fbInitArg1, &fbInitArg2, &fbInitArg3, &fbInitArg4, &fbInitArg5, &fbInitArg6, &fbInitArg7 };
static const iocshFuncDef fbInitFuncDef = { "fbInit", 8, fbInitArgs };

static void fbInitCallFunc(const iocshArgBuf * args)
{
    if (fbInit((short)args[0].ival,
               (short)args[1].ival,
               (short)args[2].ival, (short)args[3].ival, (short)args[4].ival, (short)args[5].ival, args[6].ival, args[7].ival) != OK) {
        Debug(2, "ERROR in fbInit(%d)", args[0].ival);
    }
}

/* 
 * extern int fbOpen(void);
 */
static const iocshArg fbOpenArg0 = { "arg", iocshArgInt };
static const iocshArg *fbOpenArgs[] = { &fbOpenArg0 };
static const iocshFuncDef fbOpenFuncDef = { "fbOpen", 1, fbOpenArgs };

static void fbOpenCallFunc(const iocshArgBuf * args)
{
    int rc;

    rc = fbOpen();
    if (args[0].ival) {
        if (rc != OK)
            printf("Unable to open feedback loop\n");
    }
    Debug(2, "fbOpen: rc = %d", rc);
}

/* 
 * extern int fbClose(void);
 */
static const iocshArg fbCloseArg0 = { "arg", iocshArgInt };
static const iocshArg *fbCloseArgs[] = { &fbCloseArg0 };
static const iocshFuncDef fbCloseFuncDef = { "fbClose", 1, fbCloseArgs };

static void fbCloseCallFunc(const iocshArgBuf * args)
{
    int rc;

    rc = fbClose();
    if (args[0].ival) {
        if (rc != OK)
            printf("Unable to close feedback loop\n");
    }
    Debug(2, "fbClose: rc = %d", rc);
}

/* 
 * extern int fbTrigger(void);
 */
static const iocshArg fbTriggerArg0 = { "arg", iocshArgInt };
static const iocshArg *fbTriggerArgs[] = { &fbTriggerArg0 };
static const iocshFuncDef fbTriggerFuncDef = { "fbTrigger", 1, fbTriggerArgs };

static void fbTriggerCallFunc(const iocshArgBuf * args)
{
    int rc;

    rc = fbTrigger();
    if (args[0].ival) {
        if (rc != OK)
            printf("Unable to trigger feedback loop\n");
    }
    Debug(2, "fbTrigger: rc = %d", rc);
}

/* 
 * extern int fbpr(void);
 */
static const iocshArg fbprArg0 = { "arg", iocshArgInt };
static const iocshArg *fbprArgs[] = { &fbprArg0 };
static const iocshFuncDef fbprFuncDef = { "fbpr", 1, fbprArgs };

static void fbprCallFunc(const iocshArgBuf * args)
{
    int rc;

    rc = fbpr();
    if (args[0].ival) {
        if (rc != OK)
            printf("Unable to open feedback loop\n");
    }
    Debug(5, "fbpr: rc = %d", rc);
}
static const iocshArg fbActivateArg0 = { "id", iocshArgInt };
static const iocshArg *fbActivateArgs[] = { &fbActivateArg0 };
static const iocshFuncDef fbActivateFuncDef = { "fbActivate", 1, fbActivateArgs };

static void fbActivateCallFunc(const iocshArgBuf * args)
{
    int rc;

    rc = fbActivate(args[0].ival);
    if (rc != OK)
        printf("Unable to activate feedback plugin id=%d\n", args[0].ival);
    Debug(2, "fbActivate: id=%d rc=%d", args[0].ival, rc);
}

static const iocshFuncDef fbDeactivateFuncDef = { "fbDeactivate", 0, NULL };

static void fbDeactivateCallFunc(const iocshArgBuf * args)
{
    int rc;

    rc = fbDeactivate();
    if (rc != OK)
        printf("Unable to deactivate active feedback plugin\n");
    Debug(5, "fbDeactivate: rc = %d", rc);
}

static const iocshFuncDef fbListPluginsFuncDef = { "fbListPlugins", 0, NULL };

static void fbListPluginsCallFunc(const iocshArgBuf * args)
{
    int i;

    printf("Registered feedback plugins:\n");
    for (i = 0; i < FB_MAX_MEMBERS; i++) {
        if (feedbackPluginList[i] != NULL) {
            printf("\t%d: %s%s\n", i,
                   feedbackPluginList[i]->name ? feedbackPluginList[i]->name : "<unnamed>", feedbackPluginList[i] == pfbDrvDset ? " (active)" : "");
        }
    }
}

static void fbAnalogPluginRegister(void)
{
    static int firstTime = 1;

    if (firstTime) {
        firstTime = 0;
    }
}

epicsExportRegistrar(fbAnalogPluginRegister);

/* register IOC Shell commands*/
static void fbRegisterCommands(void)
{
    static int firstTime = 1;
    Debug(4, "fbRegisterCommand(): firstTime = %d\n", firstTime);
    if (firstTime) {
        iocshRegister(&fbAnalogPluginInstantiateFuncDef, fbAnalogPluginInstantiateCallFunc);
        iocshRegister(&fbAnalogInitPluginFuncDef, fbAnalogInitPluginCallFunc);
        iocshRegister(&fbAnalogSetFilterDelayFuncDef, fbAnalogSetFilterDelayCallFunc);
        iocshRegister(&fbAnalogSetFuncDef, fbAnalogSetCallFunc);
        iocshRegister(&fbAnalogGetFuncDef, fbAnalogGetCallFunc);
        iocshRegister(&fbInitFuncDef, fbInitCallFunc);
        iocshRegister(&fbOpenFuncDef, fbOpenCallFunc);
        iocshRegister(&fbCloseFuncDef, fbCloseCallFunc);
        iocshRegister(&fbTriggerFuncDef, fbTriggerCallFunc);
        iocshRegister(&fbprFuncDef, fbprCallFunc);
        iocshRegister(&fbActivateFuncDef, fbActivateCallFunc);
        iocshRegister(&fbDeactivateFuncDef, fbDeactivateCallFunc);
        iocshRegister(&fbListPluginsFuncDef, fbListPluginsCallFunc);
        firstTime = 0;
    }
}

epicsExportRegistrar(fbRegisterCommands);
