TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application

PROD_IOC = hpi6016
# hpi6016.dbd will be created and installed
DBD += hpi6016.dbd

# hpi6016.dbd will be made up from these files:
hpi6016_DBD += base.dbd asSupport.dbd devIocStats.dbd system.dbd

# Include dbd files from all support applications:
hpi6016_DBD += hpi6016Support.dbd

# Add all the support libraries needed by this IOC
hpi6016_SYS_LIBS += event_extra event_core

# hpi6016_registerRecordDeviceDriver.cpp derives from hpi6016.dbd
hpi6016_SRCS += hpi6016_registerRecordDeviceDriver.cpp

hpi6016_SRCS += hpidrv.c
hpi6016_SRCS += timedbuffer.c

# Build the main IOC entry point on workstation OSs.
hpi6016_SRCS_DEFAULT += hpi6016Main.cpp
hpi6016_SRCS_vxWorks += -nil-

# Finally link to the EPICS Base libraries
hpi6016_LIBS += $(EPICS_BASE_IOC_LIBS)
hpi6016_LIBS += autosave devIocStats

ifneq ($(CAPUTLOG),)
PROD_LIBS += caPutLog
COMM_DBD += caPutLog.dbd
endif

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

