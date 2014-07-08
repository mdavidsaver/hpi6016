#!/usr/bin/env python
"""
Interactive HPI 6016 test box
"""

if __name__=='__main__':
    import sys
    from PyQt4 import QtGui
    app = QtGui.QApplication(sys.argv)
    from hpisim.simwin import RadMon
    rad = RadMon()
    rad.show()
    sys.exit(app.exec_())
