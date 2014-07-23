
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/util.h>
#include <event2/dns.h>

#include <epicsStdio.h>
#include <epicsTime.h>
#include <dbDefs.h>
#include <cantProceed.h>
#include <errlog.h>
#include <ellLib.h>
#include <epicsString.h>
#include <epicsTypes.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsThread.h>
#include <epicsExit.h>
#include <dbScan.h>
#include <recGbl.h>
#include <alarm.h>
#include <initHooks.h>
#include <devSup.h>
#include <drvSup.h>
#include <menuFtype.h>

#include <dbCommon.h>
#include <aiRecord.h>
#include <longinRecord.h>
#include <stringinRecord.h>
#include <waveformRecord.h>
#include <biRecord.h>
#include <mbbiRecord.h>
#include <mbbiDirectRecord.h>

/* Console printing verbosity
 * 0 - Print only fatal errors
 * 1 - Print abnormal errors
 * 2 - Print "normal" errors (eg. connection timeout)
 * 3 - Debugging
 */
int ARMDebug = 1;
/* Communication timeout (no data in # seconds) */
int ARMTimeout = 10;
/* HVP test mask time (sec) */
int ARMHVPMask = 4;

#define EEPROM_SIZE 256

enum hpicommver {
    hpicommunk = 0,
    hpicomm1 = 1,
    hpicomm2
};

typedef struct {
    ELLNODE node;

    IOSCANPVT scan;

    epicsMutexId lock;
    epicsEventId exitEvt;

    enum hpicommver version;

    /* Data received from ARM.
     * These variables are only written from the worker thread,
     * which must lock them for writing, but not reading.
     * Device support must lock for reading, but can't write
     */
    epicsInt32 rate, rate_orig;
    epicsUInt32 integrated;
    epicsUInt16 lvl[3], failcnt;
    /* Alarms based on correct measurements */
    unsigned int alrm_low:1;
    unsigned int alrm_high:1;
    unsigned int alrm_dose:1;
    unsigned int alrm_3:1;
    /* "alarm" when HVP test runs */
    unsigned int alrm_hvp_run:1;
    /* Alarms indicating abnormal conditions or equipment failure */
    unsigned int alrm_fail:1;
    unsigned int alrm_hvp_fail:1;
    unsigned int alrm_oflow_rate:1;
    unsigned int alrm_oflow_dose:1;
    unsigned int alrm_oflow_buck:1;
    epicsUInt8 eeprom[EEPROM_SIZE];

    epicsUInt8 lastAddr; /* last address sent by 6016 */
    epicsUInt16 nBytes; /* # of eeprom bytes received */

    epicsUInt8 hvp_mask_cnt;

    /* Internal status read by device support */

    unsigned int connected:1;
    unsigned int datavalid:1;

    epicsUInt32 cntConn, cntUpdate;

    char lastmsg[100];

    /* internal worker stuff */

    struct event_base *base;
    struct evdns_base* dns;
    struct bufferevent *bev;
    struct event *retry, *datatimo;

    char *name;
    char *host;
    unsigned short port;
    char apad[1];
} ARM;

static ELLLIST allARMs = ELLLIST_INIT;

static
ARM *findARM(const char *name)
{
    ELLNODE *node;

    for(node=ellFirst(&allARMs); node; node=ellNext(node)) {
        ARM *a = CONTAINER(node, ARM, node);
        if(strcmp(a->name, name)==0)
            return a;
    }
    return NULL;
}

static
void clearData(ARM *dev)
{
    dev->datavalid = 0;
    dev->nBytes = 0;
    dev->hvp_mask_cnt = 0;
}

static
void ARMPrintf(int lvl, ARM *dev, const char *fmt, ...) EPICS_PRINTF_STYLE(3,4);

static
void ARMPrintf(int lvl, ARM *dev, const char *fmt, ...)
{
    va_list args;

    if(lvl<3) {
        /* Don't publish debug messages */
        va_start(args, fmt);
        epicsMutexMustLock(dev->lock);
        epicsVsnprintf(dev->lastmsg, sizeof(dev->lastmsg)-1, fmt, args);
        epicsMutexUnlock(dev->lock);
        dev->lastmsg[sizeof(dev->lastmsg)-1] - '\0';
        va_end(args);
        /* Caller must scanIoRequest() */
    }

    if(ARMDebug>=lvl) {
        char scratch[30];
        epicsTimeStamp now;

        epicsTimeGetCurrent(&now);
        epicsTimeToStrftime(scratch, sizeof(scratch), "%Y-%m-%d %H:%M:%S", &now);
        scratch[sizeof(scratch)-1] = '\0';

        if(lvl<3) {
            /* Don't lock here as we (worker thread) are the only writer */
            errlogPrintf("%s: ARM %s: %s\n", scratch, dev->name, dev->lastmsg);
        } else {
            va_start(args, fmt);
            errlogPrintf("%s: ARM %s: ", scratch, dev->name);
            errlogVprintf(fmt, args);
            errlogMessage("\n");
            va_end(args);
        }
    }
}

static
void ARMConnect(ARM *dev);

/* Start retry timer */
static
void ARMRetry(ARM *dev)
{
    struct timeval tv = {5,0};

    ARMPrintf(3, dev, "Request Retry");

    if(dev->bev)
        bufferevent_free(dev->bev);
    dev->bev = NULL;

    if(dev->connected) {
        epicsMutexMustLock(dev->lock);
        dev->connected = 0;
        epicsMutexUnlock(dev->lock);

        scanIoRequest(dev->scan);
    }

    if(evtimer_add(dev->retry, &tv)) {
        ARMPrintf(0,dev,"fail to set failure retry timer");
        scanIoRequest(dev->scan);
        cantProceed("I'm stuck\n");
        return;
    }
}

/* restart connection attempt after error or timeout */
static
void ARMDoRetry(evutil_socket_t junk, short evt, void *raw)
{
    ARM *dev = raw;
    assert(evt&EV_TIMEOUT);

    ARMConnect(dev);
}

/* Data timeout */
static
void ARMDoTimeout(evutil_socket_t junk, short evt, void *raw)
{
    ARM *dev = raw;
    assert(evt&EV_TIMEOUT);


    epicsMutexMustLock(dev->lock);
    clearData(dev);
    epicsMutexUnlock(dev->lock);

    ARMPrintf(2, dev, "Data timeout");

    scanIoRequest(dev->scan);
}

/* more data received */
static
void ARMDoRead(struct bufferevent *bev, void *raw)
{
    int update = 0;
    size_t N;
    ARM *dev = raw;
    struct evbuffer *rbuf = bufferevent_get_input(dev->bev);

    while(1) {
        int kickdog = 0;
        enum hpicommver lastver = hpicommunk;
        unsigned int parts[6];
        size_t len;
        char *line = evbuffer_readln(rbuf, &len, EVBUFFER_EOL_CRLF_STRICT);
        if(!line)
            break;


        /* Line format for original firmware
         *  AAAAAA BBBB CCCC DDDD EE FF\r\n
         * 6 parts, 29 bytes w/ EOL
         *  A - Dose rate
         *  B - level 0
         *  C - level 1
         *  D - level 2
         *  E - fail counter
         *  F - Status bits
         *
         * Line format for new firmware
         * AAAAAA BBBBBB CC DDDD EEEE\r\n
         * 5 parts, 28 bytes w/ EOL
         *  A - Dose rate
         *  B - Integrated dose
         *  C - fail counter
         *  D - Status bits
         *  E - eeprom addr+value
         */
        int nparts = sscanf(line, "%x %x %x %x %x %x",
                            &parts[0], &parts[1], &parts[2], &parts[3], &parts[4], &parts[5]);

        if(nparts==6) /* original format */
        {
            epicsMutexMustLock(dev->lock);
            /* rate is stored as 3 byte sign and magnatude */
            dev->rate = parts[0]&0x7fffff;
            if(parts[0]&0x800000)
                dev->rate *= -1;
            dev->rate_orig = dev->rate;

            dev->lvl[0] = parts[1];
            dev->lvl[1] = parts[2];
            dev->lvl[2] = parts[3];
            dev->failcnt = parts[4];

            dev->alrm_low        = !!(parts[5]&(1<<1));
            dev->alrm_high       = !!(parts[5]&(1<<0));
            dev->alrm_3          = !!(parts[5]&(1<<2));
            dev->alrm_fail       = !!(parts[5]&(1<<3));
            dev->alrm_dose       = 0;
            dev->alrm_hvp_fail   = !!(parts[5]&(1<<6));
            dev->alrm_hvp_run    = 0;
            dev->alrm_oflow_rate = !!(parts[5]&(1<<4));
            dev->alrm_oflow_dose = 0;
            dev->alrm_oflow_buck = 0;

            dev->integrated = 0;
            dev->nBytes = 0;

            dev->datavalid = 1;
            lastver = dev->version;
            dev->version = hpicomm1;
            dev->cntUpdate++;
            epicsMutexUnlock(dev->lock);
            update = 1;


            ARMPrintf(3, dev, "Decoded: '%s'\n as %d %u %03u %03u %03u", line, dev->rate,
                      dev->lvl[0], dev->lvl[1], dev->lvl[2], dev->failcnt);
            kickdog = 1;

        } else if(nparts==5) /* new format */
        {
            int mask = ARMHVPMask;

            epicsMutexMustLock(dev->lock);
            /* rate is stored as 3 byte sign and magnatude */
            dev->rate = parts[0]&0x7fffff;
            if(parts[0]&0x800000)
                dev->rate *= -1;
            dev->rate_orig = dev->rate;

            dev->integrated = parts[1];
            dev->failcnt = parts[2];

            dev->alrm_low        = !!(parts[3]&0x0100);
            dev->alrm_high       = !!(parts[3]&0x0200);
            dev->alrm_dose       = !!(parts[3]&0x0400);
            dev->alrm_3          = !!(parts[3]&0x0800);
            dev->alrm_fail       = !!(parts[3]&0x1000);
            dev->alrm_oflow_rate = !!(parts[3]&0x2000);
            /* pos/neg number 0x4000 */
            dev->alrm_hvp_fail   = !!(parts[3]&0x8000);

            dev->alrm_oflow_dose = !!(parts[3]&0x0001);
            dev->alrm_oflow_buck = !!(parts[3]&0x0002);
            dev->alrm_hvp_run    = !!(parts[3]&0x0004);

            if(dev->alrm_hvp_run && mask>0) {
                /* HVP test ran this cycle.  Start mask countdown */
                dev->hvp_mask_cnt = mask;
            }
            if(dev->hvp_mask_cnt) {
               /* apply HVP masking. */
               dev->hvp_mask_cnt--;
               if(dev->rate>0)
                   dev->rate /= -10;
            }

            dev->lastAddr = (parts[4]>>8)&0xff;
            dev->eeprom[dev->lastAddr] = parts[4]&0xff;
            if(dev->nBytes<0xff)
                dev->nBytes++; /* assumes bytes are given in sequence. TODO: use checksum */

            dev->lvl[0] = dev->lvl[1] = dev->lvl[2] = 0;
            if(dev->nBytes==0xff) {
                epicsUInt16 scratch;
                /* for compatibility, pull these from the eeprom */
                scratch = dev->eeprom[8];
                scratch <<= 8;
                scratch |= dev->eeprom[9];
                dev->lvl[0] = scratch;
                scratch = dev->eeprom[11];
                scratch <<= 8;
                scratch |= dev->eeprom[12];
                dev->lvl[1] = scratch;
                scratch = dev->eeprom[14];
                scratch <<= 8;
                scratch |= dev->eeprom[15];
                dev->lvl[2] = scratch;
            }

            dev->datavalid = 1;
            lastver = dev->version;
            dev->version = hpicomm2;
            dev->cntUpdate++;
            epicsMutexUnlock(dev->lock);
            update = 1;


            ARMPrintf(3, dev, "Decoded: '%s'\n as %08x %08x %02x %04x %04x", line,
                      parts[0], parts[1], parts[2], parts[3], parts[4]);
            kickdog = 1;


        } else {

            epicsMutexMustLock(dev->lock);
            clearData(dev);
            epicsMutexUnlock(dev->lock);
            update = 1;

            ARMPrintf(1, dev, "invalid line: '%s'", line);
        }
        free(line);

        if(lastver!=dev->version && lastver && dev->version) {
            ARMPrintf(1, dev, "FW Version change from %d to %d", lastver, dev->version);
        }

        if(kickdog) {
            /* data timeout always 2 second longer than read timeout */
            struct timeval tv = {ARMTimeout+2, 0};
            if(tv.tv_sec<1)
                tv.tv_sec = 1;

            if(evtimer_add(dev->datatimo, &tv))
                ARMPrintf(1, dev, "Failed to add data timeout");
        }
    }

    N = evbuffer_get_length(rbuf);

    /* If we receive 29 bytes or more without an EOL, something is wrong. */
    if(N>=29) {
        ARMPrintf(1, dev, "Flushing junk from recv buffer");
        evbuffer_drain(rbuf, N);
        N = 0;

        epicsMutexMustLock(dev->lock);
        clearData(dev);
        epicsMutexUnlock(dev->lock);

        update = 1;
    }

    ARMPrintf(3, dev, "Waiting for %ld bytes", (unsigned long)(N+1));

    /* ensure we aren't called unless there is at least one more byte available */
    bufferevent_setwatermark(dev->bev, EV_READ, N+1, 1000);

    if(update)
        scanIoRequest(dev->scan);
}

/* non-data event */
static
void ARMDoEvent(struct bufferevent *bev, short events, void *raw)
{
    ARM *dev = raw;

    if(events & BEV_EVENT_CONNECTED) {
        epicsMutexMustLock(dev->lock);
        dev->connected = 1;
        clearData(dev);
        dev->cntConn++;
        epicsMutexUnlock(dev->lock);
        ARMPrintf(2, dev, "Connected");

        scanIoRequest(dev->scan);

    } else if (events&(BEV_EVENT_ERROR|BEV_EVENT_TIMEOUT|BEV_EVENT_EOF)) {
        evtimer_del(dev->datatimo);

        if(events&BEV_EVENT_TIMEOUT) {
            ARMPrintf(2,dev,"%s timeout", dev->connected ? "Read":"Connect");
        }
        if(events&BEV_EVENT_ERROR) {
            int err = EVUTIL_SOCKET_ERROR(), lvl=1;
            const char *msg = evutil_socket_error_to_string(err);
            /* increase level for common (expected) errors */
            switch(err) {
            case ECONNREFUSED: lvl=2;
            }
            ARMPrintf(lvl, dev,"Error: (%d) %s", err, msg);
        }
        if(events&BEV_EVENT_EOF) {
            ARMPrintf(2, dev, "Connection closed by peer");
        }
        scanIoRequest(dev->scan);

        epicsMutexMustLock(dev->lock);
        dev->connected = 0;
        clearData(dev);
        epicsMutexUnlock(dev->lock);

        ARMRetry(dev);
    }
}

/* Begin connection attempt */
static
void ARMConnect(ARM *dev)
{
    struct timeval rxtv = {ARMTimeout, 0}, txtv = {5,0};
    if(rxtv.tv_sec<1)
        rxtv.tv_sec = 1;

    assert(!dev->bev);

    dev->bev = bufferevent_socket_new(dev->base, -1, BEV_OPT_CLOSE_ON_FREE);
    if(!dev->bev) {
        ARMPrintf(1, dev, "failed to create BEV");
        goto fail;
    }

    if(bufferevent_set_timeouts(dev->bev, &rxtv, &txtv)) {
        ARMPrintf(1, dev, "failed to set BEV timeout");
        goto fail;
    }

    bufferevent_setcb(dev->bev, &ARMDoRead, NULL, &ARMDoEvent, dev);
    bufferevent_setwatermark(dev->bev, EV_READ, 1, 1000);

    if(bufferevent_enable(dev->bev, EV_READ)) {
        ARMPrintf(1, dev, "failed to enable BEV");
        goto fail;
    }

    if(bufferevent_socket_connect_hostname(dev->bev, dev->dns, AF_UNSPEC, dev->host, dev->port)) {
        ARMPrintf(1, dev, "failed to start connecting BEV");
        goto fail;
    }

    return;
fail:
    ARMRetry(dev);
}

/* worker thread */
static
void ARMRunner(void *raw)
{
    ARM *dev = raw;
    ARMPrintf(3, dev, "Worker starting");

    ARMConnect(dev); /* start first connection attempt */

    event_base_loop(dev->base, 0);
    ARMPrintf(3, dev, "Worker stopping");
    epicsEventSignal(dev->exitEvt);
}

/* Build a new ARM client */
static
ARM* ARMInit(const char *name, const char *ep)
{
    ARM * dev = calloc(1,sizeof(*dev)+strlen(name)+strlen(ep)+2);
    if(!dev)
        return NULL;
    dev->name = dev->apad;
    dev->host = dev->apad+strlen(name)+1;
    strcpy(dev->name, name);

    /* dev->host allocated for worst cast (no ':') */
    if(sscanf(ep, "%[^:]:%hu", dev->host, &dev->port)!=2) {
        printf("Unable to parse '%s' to host:port\n", ep);
        free(dev);
        return NULL;
    }

    dev->base = event_base_new();
    if(!dev->base)
        goto fail;

    dev->dns = evdns_base_new(dev->base, 1);
    dev->retry = evtimer_new(dev->base, &ARMDoRetry, dev);
    dev->datatimo = evtimer_new(dev->base, &ARMDoTimeout, dev);
    if(!dev->dns || !dev->retry || !dev->datatimo)
        goto fail;

    dev->lock = epicsMutexMustCreate();
    dev->exitEvt = epicsEventMustCreate(epicsEventEmpty);

    /* no locking for writes since ARMInit() not called concurrently */
    ellAdd(&allARMs, &dev->node);

    scanIoInit(&dev->scan);

    return dev;
fail:
    printf("Failed to create %s\n", name);
    if(dev->retry) event_free(dev->retry);
    if(dev->datatimo) event_free(dev->datatimo);
    if(dev->dns) evdns_base_free(dev->dns, 0);
    if(dev->base) event_base_free(dev->base);
    free(dev);
    return NULL;
}

static
void ARMStop(void *raw)
{
    ARM *dev = raw;

    ARMPrintf(3, dev, "Stopping");
    errlogFlush();

    event_base_loopbreak(dev->base);
    epicsEventMustWait(dev->exitEvt);

    event_free(dev->retry);
    event_free(dev->datatimo);
    evdns_base_free(dev->dns, 0);
    event_base_free(dev->base);
    epicsMutexDestroy(dev->lock);
    epicsEventDestroy(dev->exitEvt);
    free(dev);
}

static
void ARMStartup(initHookState state)
{
    ELLNODE *node;
    if(state!=initHookAfterIocRunning && state!=initHookAtBeginning)
        return;

    errlogPrintf("ARM Threads starting\n");
    errlogFlush();

    for(node=ellFirst(&allARMs); node; node=ellNext(node)) {
        ARM *dev = CONTAINER(node, ARM, node);

        /* register exit hook early, before scanning starts, so that
         * it runs after scanning stops
         */
        if(state==initHookAtBeginning)
            epicsAtExit(&ARMStop, dev);

        /* Start worker thread as final part of initialization */
        if(state==initHookAfterIocRunning) {
            (void)epicsThreadMustCreate("ARMRunner",
                                        epicsThreadPriorityHigh,
                                        epicsThreadGetStackSize(epicsThreadStackSmall),
                                        &ARMRunner, dev);
        }
    }
}

typedef enum {
    ARMParamUnknown = 0,
    /* device data */
    ARMParamRate,
    ARMParamRateOrig,
    ARMParamIntegrated,
    ARMParamLvl1,
    ARMParamLvl2,
    ARMParamLvl3,
    ARMParamFailCnt,
    ARMParamAlrmLow,
    ARMParamAlrmHigh,
    ARMParamAlrmDose,
    ARMParamAlrm3,
    ARMParamAlrmFail,
    ARMParamAlrmHVPFail,
    ARMParamAlrmHVPRun,
    ARMParamAlrmOFlowRate,
    ARMParamAlrmOFlowDose,
    ARMParamAlrmOFlowBuck,
    /* driver internals */
    ARMConnected,
    ARMValid,
    ARMCommVer,
    ARMNumEEPROM,
    ARMCntConn,
    ARMCntUpdate
} ARMParamID;

typedef struct {
    const char * name;
    ARMParamID id;
    unsigned int devdata:1; /* parameter is data reported by the 6016 */
    unsigned int ver2only:2; /* only available in newer firmware */
} ARMParam;

static const ARMParam parammap[] = {
    /* device data */
    {"DoseRate", ARMParamRate, 1},
    {"DoseRateOrig", ARMParamRateOrig, 1},
    {"Dose", ARMParamIntegrated, 1, 1},
    {"Lvl1", ARMParamLvl1, 1},
    {"Lvl2", ARMParamLvl2, 1},
    {"Lvl3", ARMParamLvl3, 1},
    {"FailCnt",       ARMParamFailCnt, 1},
    {"AlrmLow",       ARMParamAlrmLow, 1},
    {"AlrmHigh",      ARMParamAlrmHigh, 1},
    {"AlrmDose",      ARMParamAlrmDose, 1, 1},
    {"Alrm3",         ARMParamAlrm3, 1},
    {"AlrmFail",      ARMParamAlrmFail, 1},
    {"AlrmHVPFail",   ARMParamAlrmHVPFail, 1},
    {"AlrmHVPRun",    ARMParamAlrmHVPRun, 1, 1},
    {"AlrmOFlowRate", ARMParamAlrmOFlowRate, 1},
    {"AlrmOFlowDose", ARMParamAlrmOFlowDose, 1, 1},
    {"AlrmOFlowBuck", ARMParamAlrmOFlowBuck, 1, 1},
    /* driver internals */
    {"Connected",  ARMConnected, 0},
    {"Valid",      ARMValid, 0},
    {"CommVer",    ARMCommVer, 1},
    {"NumEEBytes", ARMNumEEPROM, 0},
    {"CntConn",    ARMCntConn, 0},
    {"CntUpdate",  ARMCntUpdate, 0},
    /* dummy */
    {"EEPROM", ARMParamUnknown, 1, 1},
    {NULL}
};

typedef struct {
    const ARM *dev;
    const ARMParam *param;
    int extra;
    int extralen;
} ARMRec;

static
void ARMInitCommon(dbCommon *prec, DBLINK *link)
{
    ARMRec *priv = calloc(1, sizeof(*priv));
    char *str = NULL, *param, *extra;
    assert(link->type==INST_IO);

    if(!priv)
        goto fail;

    str = epicsStrDup(link->value.instio.string); /* leaked, as devsup gives no place to cleanup */

    str = strtok(str, " ");
    if(!str) {
        errlogPrintf("%s: Empty link\n", prec->name);
        goto fail;
    }
    priv->dev = findARM(str);
    param = strtok(NULL, " ");
    extra = strtok(NULL, " ");
    if(extra)
        priv->extra = atoi(extra);
    extra = strtok(NULL, " ");
    if(extra)
        priv->extralen = atoi(extra);

    if(!priv->dev) {
        errlogPrintf("%s: device %s not found\n", prec->name, str);
        goto fail;
    }

    if(param) {
        const ARMParam *curmap;

        for(curmap = parammap; curmap->name; curmap++) {
            if(strcmp(param, curmap->name)==0) {
                priv->param = curmap;
                break;
            }
        }
        if(!priv->param) {
            errlogPrintf("%s: parameter %s not found\n", prec->name, param);
            goto fail;
        }
    }

    prec->dpvt = priv;

    free(str);

    return;
fail:
    errlogFlush();
    (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
    free(str);
    free(priv);
}

static
long ARMIoIntr(int cmd, dbCommon *prec, IOSCANPVT *io)
{
    ARMRec *priv = prec->dpvt;
    if(priv)
        *io = priv->dev->scan;
    return 0;
}

static
const ARMRec *ARMDSetup(dbCommon *prec)
{
    const ARMRec *priv = prec->dpvt;
    if(!priv)
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
    return priv;
}

static
epicsUInt32 ARMGetParam(const ARM *dev, int id)
{
    epicsUInt32 ret;

    switch(id) {
    case ARMParamRate:    ret = dev->rate; break;
    case ARMParamRateOrig:    ret = dev->rate_orig; break;
    case ARMParamIntegrated: ret = dev->integrated; break;
    case ARMParamLvl1:    ret = dev->lvl[0]; break;
    case ARMParamLvl2:    ret = dev->lvl[1]; break;
    case ARMParamLvl3:    ret = dev->lvl[2]; break;
    case ARMParamFailCnt: ret = dev->failcnt; break;

    case ARMParamAlrmLow:       ret = dev->alrm_low; break;
    case ARMParamAlrmHigh:      ret = dev->alrm_high; break;
    case ARMParamAlrmDose:      ret = dev->alrm_dose; break;
    case ARMParamAlrm3:         ret = dev->alrm_3; break;
    case ARMParamAlrmFail:      ret = dev->alrm_fail; break;
    case ARMParamAlrmHVPFail:   ret = dev->alrm_hvp_fail; break;
    case ARMParamAlrmHVPRun:    ret = dev->alrm_hvp_run; break;
    case ARMParamAlrmOFlowRate: ret = dev->alrm_oflow_rate; break;
    case ARMParamAlrmOFlowDose: ret = dev->alrm_oflow_dose; break;
    case ARMParamAlrmOFlowBuck: ret = dev->alrm_oflow_buck; break;

    case ARMConnected: ret = dev->connected; break;
    case ARMValid:     ret = !(dev->datavalid && dev->connected); break;
    case ARMNumEEPROM: ret = dev->nBytes; break;
    case ARMCommVer:   ret = (int)dev->version; break;
    case ARMCntConn:   ret = dev->cntConn; break;
    case ARMCntUpdate: ret = dev->cntUpdate; break;

    default:
        ret = (epicsUInt32)-1;
    }
    return ret;
}

static
epicsUInt32 ARMReadParam(dbCommon *prec)
{
    epicsUInt32 ret;
    const ARMRec *priv = ARMDSetup((dbCommon*)prec);
    if(!priv)
        return 0;

    /* for device data, alarm if not connected */
    if(priv->param->devdata && (!priv->dev->connected || !priv->dev->datavalid))
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);

    epicsMutexMustLock(priv->dev->lock);
    if(priv->dev->version!=hpicomm2 && priv->param->ver2only) {
        ret = 0;
        (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
    } else {
        ret = ARMGetParam(priv->dev, priv->param->id);
    }
    epicsMutexUnlock(priv->dev->lock);
    return ret;
}

static epicsUInt32 ARMReadEEPROM(dbCommon *prec)
{
    epicsUInt32 ret = 0;
    const ARMRec *priv = ARMDSetup(prec);
    int N;
    int addr;
    if(!priv)
        return 0;

    N = priv->extralen;
    addr = priv->extra&0xff;

    epicsMutexMustLock(priv->dev->lock);
    if(!priv->dev->connected || !priv->dev->datavalid || priv->dev->nBytes!=0xff || addr+N>0xff) {
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);

    } else {
        while(N--) {
            ret <<= 8;
            ret |= priv->dev->eeprom[addr++];
        }
    }
    epicsMutexUnlock(priv->dev->lock);
    return ret;
}

static epicsUInt32 ARMReadEEPROMlongin(longinRecord *prec)
{
    prec->val = ARMReadEEPROM((dbCommon*)prec);
    return 0;
}

static epicsUInt32 ARMReadEEPROMai(aiRecord *prec)
{
    prec->rval = ARMReadEEPROM((dbCommon*)prec);
    return 0;
}

static epicsUInt32 ARMReadEEPROMstringin(stringinRecord *prec)
{
    const ARMRec *priv = ARMDSetup((dbCommon*)prec);
    int N;
    int addr;
    if(!priv)
        return 0;

    N = priv->extralen;
    addr = priv->extra&0xff;

    epicsMutexMustLock(priv->dev->lock);
    if(!priv->dev->connected || !priv->dev->datavalid || priv->dev->nBytes!=0xff
            || addr+N>0xff || N>=MAX_STRING_SIZE) {
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);

    } else {
        memcpy(prec->val, &priv->dev->eeprom[addr], N);
        prec->val[N] = '\0';
    }
    epicsMutexUnlock(priv->dev->lock);
    return 0;
}

static
long ARMLastMsg(waveformRecord* prec)
{
    int alarm = 0;
    char *bptr = prec->bptr;
    const ARMRec *priv = ARMDSetup((dbCommon*)prec);
    if(!priv)
        return 0;
    if(prec->ftvl!=menuFtypeCHAR) {
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
        return 0;
    }

    epicsMutexMustLock(priv->dev->lock);
    alarm = !priv->dev->connected || !priv->dev->datavalid;
    strncpy(bptr, priv->dev->lastmsg, prec->nelm);
    epicsMutexUnlock(priv->dev->lock);

    bptr[prec->nelm-1]='\0';
    prec->nord = strlen(bptr);
    if(alarm)
        (void)recGblSetSevr(prec, READ_ALARM, MAJOR_ALARM);
    return 0;
}

#define initInRec(type, RET) static long ARMInit_ ## type (type ## Record *prec) { ARMInitCommon((dbCommon*)prec, &prec->inp); return (RET); }

initInRec(ai, 0)
initInRec(longin, 0)
initInRec(stringin, 0)
initInRec(waveform, 0)
initInRec(bi, 0)
initInRec(mbbi, 0)
initInRec(mbbiDirect, 0)

#define readInParam(type, fld, RET) static long ARMReadParam_ ## type (type ## Record *prec) { prec->fld = ARMReadParam((dbCommon*)prec); return (RET); }

readInParam(ai, rval, 0)
readInParam(longin, val, 0)
readInParam(bi, rval, 0)
readInParam(mbbi, rval, 0)
readInParam(mbbiDirect, rval, 0)

typedef struct {
    dset common;
    DEVSUPFUN readwrite;
} dset6;

#include <epicsExport.h>
#include <iocsh.h>

#define dsetInParam(type) static dset6 devARMReadParam ## type = {{6, NULL, NULL, (DEVSUPFUN)&ARMInit_ ## type, (DEVSUPFUN)ARMIoIntr}, (DEVSUPFUN)ARMReadParam_ ## type}; epicsExportAddress(dset, devARMReadParam ## type);

dsetInParam(ai)
dsetInParam(longin)
dsetInParam(bi)
dsetInParam(mbbi)
dsetInParam(mbbiDirect)

static dset6 devARMEEPROMlongin = {{6, NULL, NULL, (DEVSUPFUN)&ARMInit_longin, (DEVSUPFUN)ARMIoIntr}, (DEVSUPFUN)ARMReadEEPROMlongin};
epicsExportAddress(dset, devARMEEPROMlongin);

static dset6 devARMEEPROMai = {{6, NULL, NULL, (DEVSUPFUN)&ARMInit_ai, (DEVSUPFUN)ARMIoIntr}, (DEVSUPFUN)ARMReadEEPROMai};
epicsExportAddress(dset, devARMEEPROMai);

static dset6 devARMEEPROMstringin = {{6, NULL, NULL, (DEVSUPFUN)&ARMInit_stringin, (DEVSUPFUN)ARMIoIntr}, (DEVSUPFUN)ARMReadEEPROMstringin};
epicsExportAddress(dset, devARMEEPROMstringin);

static dset6 devARMLastMsgwaveform = {{6, NULL, NULL, (DEVSUPFUN)&ARMInit_waveform, (DEVSUPFUN)ARMIoIntr}, (DEVSUPFUN)ARMLastMsg};
epicsExportAddress(dset, devARMLastMsgwaveform);

static
long ARMReport(int lvl)
{
    errlogPrintf("Data timeout: %d sec\n", ARMTimeout);
    if(lvl<=0) {
        errlogPrintf("Have %d ARMs\n", ellCount(&allARMs));

    } else {
        const ARMParam *pmap;
        ELLNODE *node;
        for(node=ellFirst(&allARMs); node; node=ellNext(node)) {
            ARM *dev = CONTAINER(node, ARM, node);
            epicsUInt8 ee[EEPROM_SIZE];
            unsigned int i;
            errlogPrintf("ARM %s - %s:%u\n", dev->name, dev->host, dev->port);
            errlogPrintf(" connected: %s, data valid: %s\n",
                         dev->connected?"YES":"NO", dev->datavalid?"YES":"NO");
            if(lvl<2)
                continue;
            epicsMutexMustLock(dev->lock);
            for(pmap=parammap; pmap->name; pmap++) {
                epicsUInt32 val = ARMGetParam(dev, pmap->id);
                errlogPrintf(" %s = %u\n", pmap->name, (unsigned int)val);
            }
            if(lvl>=3)
                memcpy(ee, dev->eeprom, EEPROM_SIZE);
            epicsMutexUnlock(dev->lock);

            if(lvl>=3) {
                for(i=0; i<EEPROM_SIZE; i++) {
                    if(i%16==0)
                        errlogPrintf("%02x: ", i);
                    errlogPrintf("%02x", ee[i]);
                    if(i%4==3)
                        errlogPrintf(" ");
                    if(i%16==15)
                        errlogPrintf("\n");
                }
            }
        }
    }
    errlogFlush();
    return 0;
}

static drvet drvARM = {2, (DRVSUPFUN)ARMReport, NULL};
epicsExportAddress(drvet, drvARM);

static const iocshArg ARMInit0 = {"name", iocshArgString};
static const iocshArg ARMInit1 = {"host:port", iocshArgString};
static const iocshArg * const ARMInitArgs[] = {&ARMInit0, &ARMInit1};
static const iocshFuncDef ARMInitDef = {"ARMInit", 2, ARMInitArgs};
static void ARMInitCallFunc(const iocshArgBuf *args)
{
    ARMInit(args[0].sval, args[1].sval);
}

static void ARMRegistrar(void)
{
    iocshRegister(&ARMInitDef, &ARMInitCallFunc);
    initHookRegister(&ARMStartup);
}

epicsExportRegistrar(ARMRegistrar);
epicsExportAddress(int, ARMDebug);
epicsExportAddress(int, ARMTimeout);
epicsExportAddress(int, ARMHVPMask);
