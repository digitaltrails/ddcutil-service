# ddcutil-qtdbus-signal-receiver.py
# ---------------------------------
# Python Qt QtDBus example of receiving signals from ddcutil-dbus-server
#
# Copyright (C) 2023, Michael Hamilton
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
# dasbus seems to be recent, popular and actively supported.
# (perhaps also look at dbus-next)

import sys

from PyQt5.QtCore import *
from PyQt5.QtDBus import *


class Test(QObject):

    def __init__(self):
        super().__init__()
        bus = QDBusConnection.connectToBus(QDBusConnection.BusType.SessionBus, "session")
        # Register self to receive bus messages 
        bus.registerObject("/", self)
        # Connect receiving slot
        bus.connect("com.ddcutil.DdcutilService",
                    "/com/ddcutil/DdcutilObject",
                    "com.ddcutil.DdcutilInterface",
                    "ConnectedDisplaysChanged",
                    self.callback);

    @pyqtSlot(QDBusMessage)
    def callback(self, message: QDBusMessage):
        print("ConnectedDisplaysChanged Callback called", message.arguments())

if __name__ == '__main__':
    app = QCoreApplication(sys.argv)
    test = Test()
    sys.exit(app.exec_())

