TOP = ../..

include $(TOP)/configure/CONFIG

ifeq ($(OS_CLASS),Linux)

PROD_IOC += feedbackTestIoc

DBD += feedbackTestSupport.dbd
DBD += feedbackTestIoc.dbd

feedbackTestIoc_DBD += base.dbd
feedbackTestIoc_DBD += fbCore.dbd
feedbackTestIoc_DBD += feedbackTestSupport.dbd

feedbackTestIoc_SRCS += feedbackTestIoc_registerRecordDeviceDriver.cpp
feedbackTestIoc_SRCS += feedbackTestSupport.c
feedbackTestIoc_SRCS_DEFAULT += feedbackTestMain.cpp

feedbackTestIoc_LIBS += fbCore
feedbackTestIoc_LIBS += $(EPICS_BASE_IOC_LIBS)

endif

include $(TOP)/configure/RULES
