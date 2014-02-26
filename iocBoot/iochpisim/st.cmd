#!../../bin/linux-x86-debug/hpi6016

## You may have to change hpi6016 to something else
## everywhere it appears in this file

< envPaths

cd ${TOP}

## Register all support components
dbLoadDatabase "dbd/hpi6016.dbd"
hpi6016_registerRecordDeviceDriver pdbbase

#var("ARMDebug","3")

# Run hpisim2.py
ARMInit("sim", "localhost:4001")

## Load record instances
dbLoadRecords("db/HPI6016.db","P=TST{RadMon:X},PORT=sim")

cd ${TOP}/iocBoot/${IOC}
iocInit

## Start any sequence programs
#seq sncxxx,"user=mdavidsaverHost"
