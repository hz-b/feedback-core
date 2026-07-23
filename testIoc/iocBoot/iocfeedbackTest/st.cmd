#!../../bin/linux-x86_64/feedbackTestIoc
< envPaths

cd "${TOP}"

dbLoadDatabase "dbd/feedbackTestIoc.dbd"
feedbackTestIoc_registerRecordDeviceDriver pdbbase

dbLoadRecords "db/feedback_test.db", "P=TEST:fb:"

fbAnalogInitPlugin
fbInit 0,0,0,4,0,0,100,100
fbSetSoftTriggerRate 200
fbSetTriggerMode 1
fbOpen

iocInit
# fbpr
