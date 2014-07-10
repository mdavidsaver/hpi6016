#!../../bin/linux-x86_64-debug/hpi6016

< envPaths

cd ${TOP}

## Register all support components
dbLoadDatabase "dbd/hpi6016.dbd"
hpi6016_registerRecordDeviceDriver pdbbase

#var("ARMDebug","3")

# Run hpisim2.py
ARMInit("sim", "localhost:4001")

## Load record instances
dbLoadRecords("db/HPI6016.db","SYS=TST,DEV=RadMon:X,PORT=sim")

cd ${TOP}/iocBoot/${IOC}
iocInit
