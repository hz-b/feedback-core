#Makefile at top of application tree
TOP = .
include $(TOP)/configure/CONFIG
DIRS := $(DIRS) $(filter-out $(DIRS), configure)
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *App))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *app))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard iocBoot))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard iocboot))

DIRS := $(DIRS) $(filter-out $(DIRS), testIoc)
testIoc_DEPEND_DIRS += fbApp

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

# testIoc is an embedded EPICS top.  Explicitly propagate cleanup goals so
# its top-level bin/, dbd/, lib/, include/, and generated O.* directories are
# removed as well.  This also makes `make clean distclean` reliable.
.PHONY: testIoc-clean testIoc-distclean

clean: testIoc-clean
distclean: testIoc-distclean

testIoc-clean:
	$(MAKE) -C testIoc clean

testIoc-distclean: testIoc-clean
	$(MAKE) -C testIoc distclean


