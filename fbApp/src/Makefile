TOP=../..

include $(TOP)/configure/CONFIG

LIBRARY_IOC += fbCore

INC += fbCore.h
INC += fbAnalog.h
INC += fbRegistry.h
DBD += fbCore.dbd

fbCore_SRCS  += fbCore.c
fbCore_SRCS  += fbRegistry.c
fbCore_SRCS  += iocshell.c
SRC_DIRS += $(TOP)/fbApp/src/support/
fbCore_SRCS  += devFeedback.c
fbCore_SRCS  += devLiFeedback.c
fbCore_SRCS  += devLoFeedback.c
fbCore_SRCS  += devMbbioFeedback.c
fbCore_SRCS  += devAiFeedback.c
SRC_DIRS += $(TOP)/fbApp/src/plugin/
fbCore_SRCS  += fbAnalog.c
# Linux host-only unit tests.  Keep these out of VxWorks builds.
ifneq ($(findstring linux,$(EPICS_HOST_ARCH)),)
TESTPROD_HOST += devFeedbackTest
devFeedbackTest_SRCS += devFeedbackTest.c
devFeedbackTest_SRCS += devFeedback.c
devFeedbackTest_LIBS += $(EPICS_BASE_IOC_LIBS)
TESTS += devFeedbackTest
endif

fbCore_LIBS += $(EPICS_BASE_IOC_LIBS)
fbCore_SYS_LIBS_Linux += rt

OBJS_vxWorks += fbCore.c

include $(TOP)/configure/RULES
