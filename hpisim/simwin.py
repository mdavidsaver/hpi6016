# -*- coding: utf-8 -*-

import sys, random
from PyQt4 import Qt, QtCore, QtGui, QtNetwork

from .simwin_ui import Ui_simwin

class SockHandler(QtCore.QObject):
    def __init__(self, parent, sock):
        QtCore.QObject.__init__(self, parent)
        self.sock = sock
        self.sock.disconnected.connect(self.connLost)

    def connLost(self):
        P = self.parent()
        print 'Drop connection', self.sock.peerAddress()
        P.socks.remove(self)
        P.updateNConn()

class eeprom(QtGui.QStandardItemModel):
    def __init__(self):
        super(eeprom, self).__init__()
        self.data = 256*[0]

class RadMon(QtGui.QMainWindow):
    def __init__(self, *args):
        super(RadMon, self).__init__(*args)
        self.ui = Ui_simwin()
        self.ui.setupUi(self)

        self.sendT = QtCore.QTimer(self)
        self.sendT.setSingleShot(False)
        self.sendT.timeout.connect(self.tick)

        self.servSock = QtNetwork.QTcpServer(self)
        self.socks = set()
        self.servSock.newConnection.connect(self.newConn)
        
        self.failcnt = 60
        self.nextaddr = 0
        self.alrmlow = False
        self.alrmhigh = False
        self.alrmdose = False
        self.alrm3 = False
        self.alarmrateoflow = False # dose rate overflow
        self.alarmdoseoflow = False # integrated dose overflow (total range)
        self.alarmbucketoflow = False # int. dose bucket overflow

        self.EE = eeprom()

        self.servSock.listen(QtNetwork.QHostAddress.LocalHost, 4001)
        self.sendT.start(1000)

    def closeEvent(self, evt):
        evt.accept()
        print 'Shutdown'
        for S in list(self.socks):
            S.sock.close()
        self.socks.clear()
        return super(RadMon, self).closeEvent(evt)

    def newConn(self):
        sock = self.servSock.nextPendingConnection()
        print 'New connection', sock.peerAddress()
        self.socks.add(SockHandler(self, sock))
        self.updateNConn()

    def updateNConn(self):
        N = len(self.socks)
        self.ui.numconn.setText(str(N))

    def write(self, bytes):
        self.ui.lastline.setText(bytes)
        for S in self.socks:
            if S.sock.isOpen():
                S.sock.write(bytes)

    def tick(self):
        # update device simulation
        rate = self.ui.vrate.value()
        if rate<=-0.5:
            self.failcnt = max(0, self.failcnt-1)
        else:
            self.failcnt = 60

        self.alrmlow = rate>self.ui.vlevel2.value()
        self.alrmhigh = rate>self.ui.vlevel1.value()
        self.alrm3 = rate>self.ui.vlevel3.value()
        self.alarmrateoflow = rate>100.0

        self.sendData()

        self.nextaddr = (self.nextaddr+1)%256

    def sendData(self):
        if self.ui.btnPause.isChecked():
            return

        T = self.ui.fwmode.currentIndex()
        if self.ui.btnFault.isChecked():
            msg = self.sendFault()
        else:
            if T==0:
                msg = self.sendFW1()
            else:
                msg = self.sendFW2()
            if self.ui.btnByte.isChecked():
                msg = self.mangleByte(msg)

        self.write(msg)

        self.ui.btnTest.setChecked(False)
        self.ui.btnFault.setChecked(False)
        self.ui.btnByte.setChecked(False)

    def sendFault(self):
        "Return a random byte string of up to 40 charactors"
        N = random.randint(1,40)
        return ''.join([chr(random.randint(0,255)) for n in range(N)])

    def mangleByte(self, msg):
        return msg

    def sendFW1(self):
        rate = int(self.ui.vrate.value()*100)
        if rate<0:
            rate = -rate | 0x800000

        bits = 0
        if self.alrmhigh:
            bits |= 1<<0 # alarm 1 (high)
        if self.alrmlow:
            bits |= 1<<1 # alarm 2 (low)
        if self.alrm3:
            bits |= 1<<2 # alarm 3 (unused)
        if self.failcnt==0:
            bits |= 1<<3 # fail
        if self.alarmrateoflow:
            bits |= 1<<4 # overflow (>100 mR/h)
        if self.ui.btnFail.isChecked():
            bits |= 1<<6 # HVP test fails

        data = {
            'rate':rate,
            'alarm1':int(self.ui.vlevel1.value()*100),
            'alarm2':int(self.ui.vlevel2.value()*100),
            'alarm3':int(self.ui.vlevel3.value()*100),
            'failCnt':self.failcnt,
            'bits':bits,
        }
        
        return '%(rate)06x %(alarm1)04x %(alarm2)04x %(alarm3)04x %(failCnt)02x %(bits)02x\r\n'%data

    def sendFW2(self):
        rate = int(self.ui.vrate.value()*100)
        if rate<0:
            rate = -rate | 0x800000

        bits = 0
        if self.alrmlow:
            bits |= 1<<0 # alarm Lo
        if self.alrmhigh:
            bits |= 1<<1 # alarm Hi
        if self.alrm3:
            bits |= 1<<2 # alarm 3 (unused)
        if self.alrmdose:
            bits |= 1<<3 # alarm I1 (integrated dose)
        if self.failcnt==0:
            bits |= 1<<4 # fail
        if self.alarmrateoflow:
            bits |= 1<<5 # overflow (>100 mR/h)
        # 1<<6, negative number?
        if self.ui.btnFail.isChecked():
            bits |= 1<<7 # HVP test fails
        if self.alarmdoseoflow:
            bits |= 1<<8
        if self.alarmbucketoflow:
            bits |= 1<<9
        if self.ui.btnTest.isChecked():
            bits |= 1<<10 # HVP test runs

        data = {
            'dose':int(self.ui.vdose.value()*100),
            'rate':rate,
            'failCnt':self.failcnt,
            'bits':bits,
            'eeaddr':self.nextaddr,
            'eeval':int(self.EE.data[self.nextaddr])&0xff
        }
        
        return '%(rate)06x %(dose)06x %(failCnt)02x %(bits)04x %(eeaddr)02x%(eeval)02x\r\n'%data
