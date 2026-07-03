#Makefile at top of application tree
TOP = .
include $(TOP)/configure/CONFIG
DIRS := $(DIRS) $(filter-out $(DIRS), configure)
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *App))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *app))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard iocBoot))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard iocboot))

ifeq ($(T_A),)
ifneq ($(findstring linux,$(EPICS_HOST_ARCH)),)

.PHONY: feedback_common_runtests

install: feedback_common_runtests

feedback_common_runtests: fbApp.install
	$(MAKE) -C fbApp/src O.$(EPICS_HOST_ARCH)/devFeedbackTest
	fbApp/src/O.$(EPICS_HOST_ARCH)/devFeedbackTest

endif
endif

include $(TOP)/configure/RULES_TOP
