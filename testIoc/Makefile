TOP = .

include $(TOP)/configure/CONFIG

DIRS += configure
DIRS += feedbackTestApp
DIRS += iocBoot

feedbackTestApp_DEPEND_DIRS += configure
iocBoot_DEPEND_DIRS += feedbackTestApp

.PHONY: run
run: install
	cd iocBoot/iocfeedbackTest && \
		../../bin/$(EPICS_HOST_ARCH)/feedbackTestIoc st.cmd

include $(TOP)/configure/RULES_TOP
