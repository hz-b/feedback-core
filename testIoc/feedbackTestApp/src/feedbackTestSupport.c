/* SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin fuer Materialien und Energie GmbH
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include <epicsExport.h>
#include <iocsh.h>

#include "fbCore.h"

static const iocshArg feedbackTestStartArg0 = {
    "software trigger rate in Hz", iocshArgDouble
};

static const iocshArg *feedbackTestStartArgs[] = {
    &feedbackTestStartArg0
};

static const iocshFuncDef feedbackTestStartFuncDef = {
    "feedbackTestStart", 1, feedbackTestStartArgs
};

static void feedbackTestStartCallFunc(const iocshArgBuf *args)
{
    double rate;

    rate = args[0].dval;
    if (rate <= 0.0) {
        printf("feedbackTestStart: rate must be greater than zero\n");
        return;
    }

    if (fbSetSoftTriggerRate(rate) != OK) {
        printf("feedbackTestStart: cannot set software trigger rate %f Hz\n",
               rate);
        return;
    }

    if (fbOpen() != OK) {
        printf("feedbackTestStart: cannot start feedback loop\n");
        return;
    }

    printf("feedbackTestStart: feedback loop started at approximately %f Hz\n",
           rate);
}

static const iocshFuncDef feedbackTestStopFuncDef = {
    "feedbackTestStop", 0, NULL
};

static void feedbackTestStopCallFunc(const iocshArgBuf *args)
{
    (void)args;

    if (fbClose() != OK)
        printf("feedbackTestStop: cannot stop feedback loop\n");
}

static void feedbackTestRegistrar(void)
{
    iocshRegister(&feedbackTestStartFuncDef, feedbackTestStartCallFunc);
    iocshRegister(&feedbackTestStopFuncDef, feedbackTestStopCallFunc);
}

epicsExportRegistrar(feedbackTestRegistrar);
