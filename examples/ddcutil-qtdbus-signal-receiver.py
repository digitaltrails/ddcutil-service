#!/usr/bin/python3
# ddcutil-qtdbus-signal-receiver.py
# ---------------------------------
# Python Qt QtDBus example of receiving signals from ddcutil-service
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
                    self.callback_on_displays_changed);

        bus.connect("com.ddcutil.DdcutilService",
                    "/com/ddcutil/DdcutilObject",
                    "com.ddcutil.DdcutilInterface",
                    "VcpValueChanged",
                    self.callback_on_vcp_value_changed);

    @pyqtSlot(QDBusMessage)
    def callback_on_displays_changed(self, message: QDBusMessage):
        edid_encoded, event_type, flags = message.arguments()
        print(f"ConnectedDisplaysChanged Callback called {event_type=} {flags=} {edid_encoded:.30}...")

    @pyqtSlot(QDBusMessage)
    def callback_on_vcp_value_changed(self, message: QDBusMessage):
        display_number, edid_encoded, vcp_code, vcp_new_value, client_name, client_context, flags = message.arguments()
        print(f"VcpValueChanged Callback called {display_number=} {edid_encoded=:.30}... {vcp_code=} {vcp_new_value=} "
              f" {client_name=} {client_context=} {flags=} ")

if __name__ == '__main__':
    app = QCoreApplication(sys.argv)
    test = Test()
    sys.exit(app.exec_())

