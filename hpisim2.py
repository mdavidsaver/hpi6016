#!/usr/bin/env python
"""
Interactive HPI 6016 test box
"""

import sys, random
from PyQt4 import Qt, QtCore, QtGui, QtNetwork
from PyQt4.QtCore import SIGNAL


try:
    _fromUtf8 = QtCore.QString.fromUtf8
except AttributeError:
    _fromUtf8 = lambda s: s

class Ui_MainWindow(object):
    def setupUi(self, MainWindow):
        MainWindow.setObjectName(_fromUtf8("MainWindow"))
        MainWindow.resize(212, 262)

        self.centralwidget = QtGui.QWidget(MainWindow)
        self.centralwidget.setObjectName(_fromUtf8("centralwidget"))
        self.verticalLayout = QtGui.QVBoxLayout(self.centralwidget)
        self.verticalLayout.setObjectName(_fromUtf8("verticalLayout"))

        self.horizontalSlider = QtGui.QSlider(self.centralwidget)
        self.horizontalSlider.setOrientation(QtCore.Qt.Horizontal)
        self.horizontalSlider.setObjectName(_fromUtf8("horizontalSlider"))
        self.verticalLayout.addWidget(self.horizontalSlider)

        self.gridLayout = QtGui.QGridLayout()
        self.gridLayout.setObjectName(_fromUtf8("gridLayout"))

        self.alarm2 = QtGui.QDoubleSpinBox(self.centralwidget)
        self.alarm2.setObjectName(_fromUtf8("alarm2"))
        self.gridLayout.addWidget(self.alarm2, 2, 1, 1, 1)
        self.label = QtGui.QLabel(self.centralwidget)
        self.label.setObjectName(_fromUtf8("label"))
        self.gridLayout.addWidget(self.label, 0, 0, 1, 1)
        self.doseRate = QtGui.QDoubleSpinBox(self.centralwidget)
        self.doseRate.setObjectName(_fromUtf8("doseRate"))
        self.gridLayout.addWidget(self.doseRate, 0, 1, 1, 1)
        self.label_2 = QtGui.QLabel(self.centralwidget)
        self.label_2.setObjectName(_fromUtf8("label_2"))
        self.gridLayout.addWidget(self.label_2, 1, 0, 1, 1)
        self.label_4 = QtGui.QLabel(self.centralwidget)
        self.label_4.setObjectName(_fromUtf8("label_4"))
        self.gridLayout.addWidget(self.label_4, 3, 0, 1, 1)
        self.label_3 = QtGui.QLabel(self.centralwidget)
        self.label_3.setObjectName(_fromUtf8("label_3"))
        self.gridLayout.addWidget(self.label_3, 2, 0, 1, 1)
        self.alarm1 = QtGui.QDoubleSpinBox(self.centralwidget)
        self.alarm1.setObjectName(_fromUtf8("alarm1"))
        self.gridLayout.addWidget(self.alarm1, 1, 1, 1, 1)
        self.alarm3 = QtGui.QDoubleSpinBox(self.centralwidget)
        self.alarm3.setObjectName(_fromUtf8("alarm3"))
        self.gridLayout.addWidget(self.alarm3, 3, 1, 1, 1)
        self.verticalLayout.addLayout(self.gridLayout)

        self.hvpTest = QtGui.QCheckBox(self.centralwidget)
        self.hvpTest.setChecked(True)
        self.hvpTest.setObjectName(_fromUtf8("hvpTest"))
        self.verticalLayout.addWidget(self.hvpTest)

        self.nosend = QtGui.QCheckBox(self.centralwidget)
        self.nosend.setChecked(False)
        self.nosend.setObjectName(_fromUtf8("nosend"))
        self.verticalLayout.addWidget(self.nosend)

        self.sendjunk = QtGui.QPushButton(self.centralwidget)
        self.verticalLayout.addWidget(self.sendjunk)

        self.sendbyte = QtGui.QPushButton(self.centralwidget)
        self.verticalLayout.addWidget(self.sendbyte)

        self.line = QtGui.QFrame(self.centralwidget)
        self.line.setFrameShape(QtGui.QFrame.HLine)
        self.line.setFrameShadow(QtGui.QFrame.Sunken)
        self.line.setObjectName(_fromUtf8("line"))
        self.verticalLayout.addWidget(self.line)

        self.horizontalLayout = QtGui.QHBoxLayout()
        self.horizontalLayout.setObjectName(_fromUtf8("horizontalLayout"))

        self.label_6 = QtGui.QLabel(self.centralwidget)
        self.label_6.setObjectName(_fromUtf8("label_6"))
        self.horizontalLayout.addWidget(self.label_6)

        self.nConn = QtGui.QLineEdit(self.centralwidget)
        self.nConn.setReadOnly(True)
        self.nConn.setObjectName(_fromUtf8("nConn"))
        self.horizontalLayout.addWidget(self.nConn)

        self.verticalLayout.addLayout(self.horizontalLayout)

        self.lastMsg = QtGui.QLineEdit(self.centralwidget)
        self.lastMsg.setReadOnly(True)
        self.lastMsg.setObjectName(_fromUtf8("lastMsg"))
        self.verticalLayout.addWidget(self.lastMsg)

        MainWindow.setCentralWidget(self.centralwidget)
        self.menubar = QtGui.QMenuBar(MainWindow)
        self.menubar.setGeometry(QtCore.QRect(0, 0, 212, 20))
        self.menubar.setObjectName(_fromUtf8("menubar"))
        MainWindow.setMenuBar(self.menubar)
        self.statusbar = QtGui.QStatusBar(MainWindow)
        self.statusbar.setObjectName(_fromUtf8("statusbar"))
        MainWindow.setStatusBar(self.statusbar)

        self.retranslateUi(MainWindow)
        QtCore.QMetaObject.connectSlotsByName(MainWindow)

    def retranslateUi(self, MainWindow):
        MainWindow.setWindowTitle(QtGui.QApplication.translate("MainWindow", "MainWindow", None, QtGui.QApplication.UnicodeUTF8))
        self.label.setText(QtGui.QApplication.translate("MainWindow", "Dose Rate", None, QtGui.QApplication.UnicodeUTF8))
        self.label_2.setText(QtGui.QApplication.translate("MainWindow", "Alarm Level 1", None, QtGui.QApplication.UnicodeUTF8))
        self.label_4.setText(QtGui.QApplication.translate("MainWindow", "Alarm Level 3", None, QtGui.QApplication.UnicodeUTF8))
        self.label_3.setText(QtGui.QApplication.translate("MainWindow", "Alarm Level 2", None, QtGui.QApplication.UnicodeUTF8))
        self.hvpTest.setText(QtGui.QApplication.translate("MainWindow", "HVP Test Pass", None, QtGui.QApplication.UnicodeUTF8))
        self.nosend.setText(QtGui.QApplication.translate("MainWindow", "Pause Tx", None, QtGui.QApplication.UnicodeUTF8))
        self.label_6.setText(QtGui.QApplication.translate("MainWindow", "# conn", None, QtGui.QApplication.UnicodeUTF8))
        self.sendjunk.setText(QtGui.QApplication.translate("MainWindow", "Inject fault", None, QtGui.QApplication.UnicodeUTF8))
        self.sendbyte.setText(QtGui.QApplication.translate("MainWindow", "Inject byte", None, QtGui.QApplication.UnicodeUTF8))

class SockHandler(QtCore.QObject):
    def __init__(self, parent, sock):
        QtCore.QObject.__init__(self, parent)
        self.sock = sock
        ok = True
        ok &= self.connect(self.sock, SIGNAL('disconnected()'), self.connLost)

    def connLost(self):
        self.parent().dropConn(self)
        

class RadMon(QtGui.QMainWindow, Ui_MainWindow):
    def __init__(self, *args):
        QtGui.QMainWindow.__init__(self, *args)
        self.setupUi(self)

        self.setWindowTitle("HPI 6016 Sim")

        self.doseRate.setMinimum(-0.5)
        self.doseRate.setMaximum(1000000)
        self.doseRate.setSingleStep(0.1)
        
        self.alarm1.setValue(5.0)
        self.alarm2.setValue(1.0)
        
        self.sendT = QtCore.QTimer(self)
        self.sendT.setSingleShot(False)
        
        self.servSock = QtNetwork.QTcpServer(self)
        self.socks = set()

        self.data = {'failCnt':60}
        self.update()
        self.updateNConn()

        ok = True
        
        ok &= self.connect(self.doseRate, SIGNAL("valueChanged(double)"), self.update)
        ok &= self.connect(self.alarm1, SIGNAL("valueChanged(double)"), self.update)
        ok &= self.connect(self.alarm2, SIGNAL("valueChanged(double)"), self.update)
        ok &= self.connect(self.alarm3, SIGNAL("valueChanged(double)"), self.update)
        ok &= self.connect(self.hvpTest, SIGNAL("stateChanged(int)"), self.update)
        ok &= self.connect(self.sendT, SIGNAL("timeout()"), self.sendData)
        ok &= self.connect(self.servSock, SIGNAL("newConnection()"), self.newConn)
        ok &= self.connect(self.sendjunk, SIGNAL("clicked()"), self.sendBad)
        ok &= self.connect(self.sendbyte, SIGNAL("clicked()"), self.sendOne)

        assert ok, "Failed to connect some signals"

        self.servSock.listen(QtNetwork.QHostAddress.LocalHost, 4001)
        self.sendT.start(1000)

    def closeEvent(self, evt):
        evt.accept()
        print 'Shutdown'
        for S in list(self.socks):
            S.sock.close()
        self.socks.clear()
        
    def newConn(self):
        sock = self.servSock.nextPendingConnection()
        print 'New connection', sock.peerAddress()
        self.socks.add(SockHandler(self, sock))
        self.updateNConn()

    def dropConn(self, conn):
        print 'Drop connection', conn.sock.peerAddress()
        self.socks.remove(conn)
        self.updateNConn()

    def write(self, bytes):
        for S in self.socks:
            if S.sock.isOpen():
                S.sock.write(bytes)

    def update(self):
        rate = int(self.doseRate.value()*100)

        self.data['alarm1'] = int(self.alarm1.value()*100)
        self.data['alarm2'] = int(self.alarm2.value()*100)
        self.data['alarm3'] = int(self.alarm3.value()*100)

        bits = 0
        
        if rate >= self.data['alarm1']:
            bits |= 1<<0
        if rate >= self.data['alarm2']:
            bits |= 1<<1
        if rate >= self.data['alarm3']:
            bits |= 1<<2

            
        self.isValid = rate > -0.4*100

        if rate >= 100*100:
            bits |= 1<<4 # overflow

        if not self.hvpTest.isChecked():
            bits |= 1<<6

        if rate<0:
            rate = -rate | 0x800000
        self.data['rate'] = rate
        self.data['bits'] = bits
        #print 'update',self.data
            
    def sendData(self):
        if self.isValid:
            self.data['failCnt'] = 60
        elif self.data['failCnt']>0:
            self.data['failCnt']-=1 # decrement holdoff counter

        if self.data['failCnt']<=0:
            self.data['bits'] |= 1<<3 # Fail alarm
        else:
            self.data['bits'] &= ~(1<<3)

        if self.nosend.isChecked():
            self.lastMsg.setText("Paused")
            return

        msg = '%(rate)06x %(alarm1)04x %(alarm2)04x %(alarm3)04x %(failCnt)02x %(bits)02x\r\n'%self.data

        #print '>>>',msg,
        self.write(msg)
        self.lastMsg.setText(msg)

    def sendBad(self):
        N = random.randint(1,40)
        self.write(''.join([chr(random.randint(0,255)) for n in range(N)]))

    def sendOne(self):
        self.write('0')

    def updateNConn(self):
        N = len(self.socks)
        self.nConn.setText(str(N))
        
if __name__=='__main__':
    app = QtGui.QApplication(sys.argv)
    rad = RadMon()
    rad.show()
    sys.exit(app.exec_())
