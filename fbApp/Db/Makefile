TOP=../..
include $(TOP)/configure/CONFIG
include $(TOP)/configure/RULES
install: doversion
doversion:
	sed s/__DATE__/`date +%Y%m%d%H%M`/ < $(TOP)/fbApp/Db/fb_version.db.in > $(TOP)/fbApp/Db/fb_version.db
	if [ ! -d $(TOP)/db ]; then mkdir $(TOP)/db; fi
	cp $(TOP)/fbApp/Db/fb_version.db $(TOP)/db

