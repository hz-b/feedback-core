// SPDX-FileCopyrightText: 2026 Helmholtz-Zentrum Berlin fuer Materialien und Energie GmbH
// SPDX-License-Identifier: MIT

#include <stddef.h>
#include <stdlib.h>

#include <epicsExit.h>
#include <epicsThread.h>
#include <iocsh.h>

int main(int argc, char *argv[])
{
    if (argc >= 2) {
        iocsh(argv[1]);
        epicsThreadSleep(0.2);
    }

    /* Interactive IOC shell; keeps the IOC process alive. */
    iocsh(NULL);

    epicsExit(0);
    return 0;
}
