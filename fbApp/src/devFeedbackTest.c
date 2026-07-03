// SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin für Materialien und Energie GmbH
// SPDX-License-Identifier: MIT

#include <stddef.h>
#include <string.h>

#include <epicsUnitTest.h>
#include <testMain.h>

#include "devFeedback.h"

typedef struct DeviceNameTestCase {
    const char *name;
    int device;
} DeviceNameTestCase;

static DeviceNameTestCase deviceNameCases[] = {
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
    {FB_DATASETEVENTS_STRING, FBDATASETEVENTS}
};

static void testLookupTable(void)
{
    size_t i;
    int device;
    const char *name;

    for (i = 0u; i < sizeof(deviceNameCases) / sizeof(deviceNameCases[0]); i++) {
        device = -1;
        testOk(fbLookupDeviceName(deviceNameCases[i].name, &device) == OK, "lookup %s succeeds", deviceNameCases[i].name);
        testOk(device == deviceNameCases[i].device, "lookup %s returns expected enum", deviceNameCases[i].name);

        name = fbDeviceNameFromId(deviceNameCases[i].device);
        testOk(name != NULL && strcmp(name, deviceNameCases[i].name) == 0, "reverse lookup for %s succeeds", deviceNameCases[i].name);
    }
}

static void testParseDeviceName(void)
{
    int device;
    char plain[] = "RATE";
    char whitespace[] = " \t RATE \r\n";
    char comma[] = "RATE,ignored";
    char control[] = "R\001A\002T\003E";
    char unknown[] = "UNKNOWN";
    char tooLong[MAX_DEVCHRS + 1];
    int i;

    device = -1;
    testOk(fbParseDeviceName(plain, &device) == OK, "parse plain token succeeds");
    testOk(device == FBRATE, "plain token maps to FBRATE");

    device = -1;
    testOk(fbParseDeviceName(whitespace, &device) == OK, "parse token with whitespace succeeds");
    testOk(device == FBRATE, "whitespace token maps to FBRATE");

    device = -1;
    testOk(fbParseDeviceName(comma, &device) == OK, "parse token before comma succeeds");
    testOk(device == FBRATE, "comma token maps to FBRATE");

    device = -1;
    testOk(fbParseDeviceName(control, &device) == OK, "parse token with control characters succeeds");
    testOk(device == FBRATE, "control-character token maps to FBRATE");

    device = FBRATE;
    testOk(fbParseDeviceName(unknown, &device) == ERROR, "unknown parsed token fails");

    for (i = 0; i < MAX_DEVCHRS; i++)
        tooLong[i] = 'A';
    tooLong[MAX_DEVCHRS] = '\0';
    testOk(fbParseDeviceName(tooLong, &device) == ERROR, "overlong token fails");

    testOk(fbParseDeviceName(NULL, &device) == ERROR, "NULL parse string fails");
    testOk(fbParseDeviceName(plain, NULL) == ERROR, "NULL parse output pointer fails");
}

static void testInvalidLookupArguments(void)
{
    int device;

    device = FBRATE;
    testOk(fbLookupDeviceName(NULL, &device) == ERROR, "NULL lookup name fails");
    testOk(fbLookupDeviceName(FB_RATE_STRING, NULL) == ERROR, "NULL lookup output pointer fails");
    testOk(fbLookupDeviceName("UNKNOWN", &device) == ERROR, "unknown lookup name fails");

    testOk(fbDeviceNameFromId(-1) == NULL, "negative enum id has no reverse lookup");
    testOk(fbDeviceNameFromId(9999) == NULL, "large enum id has no reverse lookup");
}

MAIN(devFeedbackTest)
{
    testPlan(92);

    testLookupTable();
    testParseDeviceName();
    testInvalidLookupArguments();

    return testDone();
}
