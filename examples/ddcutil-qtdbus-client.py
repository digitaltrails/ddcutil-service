#!/usr/bin/python3
# ddcutil-qtdbus-client.py
# ------------------------
# Python Qt QtDBus example of using ddcutil-service
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
import base64
from collections import namedtuple

from PyQt5.QtCore import *
from PyQt5.QtDBus import *

DO_SET_VCP_TEST = False   # If enabled, a test will run to change VDU brightness.

BRIGHTNESS_VCP = 0x10
CONTRAST_VCP = 0x12

bus = QDBusConnection.connectToBus(QDBusConnection.BusType.SessionBus, "session")

ddcutil_dbus_iface = QDBusInterface("com.ddcutil.DdcutilService",
                                    "/com/ddcutil/DdcutilObject",
                                    "com.ddcutil.DdcutilInterface",
                                    connection=bus)

# Properties are available via a separate interface with "Get" and "Set" methods
ddcutil_dbus_props = QDBusInterface("com.ddcutil.DdcutilService",
                                    "/com/ddcutil/DdcutilObject",
                                    "org.freedesktop.DBus.Properties",
                                    connection=bus)

ddcutil_dbus_iface.setTimeout(5000)

# Create a namedtuple that matches the attributes returned by the detect method
DetectedAttributes = namedtuple("DetectedAttributes",
                                ddcutil_dbus_props.call("Get",
                                                        "com.ddcutil.DdcutilInterface",
                                                        "AttributesReturnedByDetect").arguments()[0])
print(DetectedAttributes.__dict__['__doc__'])

# Call detect to get back a list of unnamed tuples, one for each VDU
result = ddcutil_dbus_iface.call("Detect",
                                 QDBusArgument(0, QMetaType.UInt))
print(result.arguments())
number_detected, list_of_displays, status, errmsg = result.arguments()

print(f"Detect returned: {number_detected=} {status=} {errmsg=}\n", list_of_displays, '\n')

# Reform unnamed tuples into a list of namedtuples (optional, for convenience)
vdu_list = [DetectedAttributes(*vdu) for vdu in list_of_displays]
print(f"{vdu_list=}\n")

# Play with each VDU - this will change the brightness
for vdu in vdu_list:

    print(f">>>>>TARGET VDU: {vdu.display_number=} {vdu.manufacturer_id=} {vdu.model_name=} " 
          f"{vdu.serial_number=} {vdu.binary_serial_number=}\n")
    print(f"{vdu.edid_txt=}")
    if vdu.edid_txt.endswith("="):  # base64 encoded
        print(f"vdu.edid in hex={base64.b64decode(vdu.edid_txt).hex()}")

    result = ddcutil_dbus_iface.call(
        "GetVcp", -1,
        vdu.edid_txt,
        QDBusArgument(BRIGHTNESS_VCP, QMetaType.UChar),
        QDBusArgument(0, QMetaType.UInt))

    val, max_val, formatted_val, status, errmsg = result.arguments()
    print(f"GetVcp returned: {val=} {max_val=} {formatted_val=} {status=} {errmsg=}\n")

    if DO_SET_VCP_TEST:
        print(f"SetVcp - Reducing brightness for {vdu.manufacturer_id=} {vdu.model_name=}")
        status, errmsg = ddcutil_dbus_iface.call("SetVcp",
                                                 -1,
                                                 vdu.edid_txt,
                                                 QDBusArgument(BRIGHTNESS_VCP, QMetaType.UChar),
                                                 QDBusArgument(val - 1, QMetaType.UShort),
                                                 QDBusArgument(0, QMetaType.UInt)).arguments()
        print(f"SetVcp returned: {status=} {errmsg=}\n")

    if DO_SET_VCP_TEST:
        print(f"SetVcpWithContext - Reducing brightness for {vdu.manufacturer_id=} {vdu.model_name=}")
        status, errmsg = ddcutil_dbus_iface.call("SetVcpWithContext",
                                                 -1,
                                                 vdu.edid_txt,
                                                 QDBusArgument(BRIGHTNESS_VCP, QMetaType.UChar),
                                                 QDBusArgument(val - 1, QMetaType.UShort),
                                                 "my_special_context",
                                                 QDBusArgument(0, QMetaType.UInt)).arguments()
        print(f"SetVcp returned: {status=} {errmsg=}\n")

    vcp_metadata = ddcutil_dbus_iface.call(
        "GetVcpMetadata",
        -1,
        vdu.edid_txt,
        QDBusArgument(BRIGHTNESS_VCP, QMetaType.UChar),
        QDBusArgument(0, QMetaType.UInt)).arguments()
    print("GetVcpMetadata returned:", vcp_metadata)
    feature_name, desc, is_ro, is_wo, is_rw, is_complex, is_continuous, _, _ = vcp_metadata
    print(f"metadata: {is_rw=} {is_complex=} {is_continuous=}\n")

    vcp_code_array = QDBusArgument()
    vcp_code_array.beginArray(QMetaType.UChar)
    vcp_code_array.add(QDBusArgument(BRIGHTNESS_VCP, QMetaType.UChar))
    vcp_code_array.add(QDBusArgument(CONTRAST_VCP, QMetaType.UChar))
    vcp_code_array.endArray()
    result = ddcutil_dbus_iface.call(
        "GetMultipleVcp",
        -1,
        vdu.edid_txt,
        vcp_code_array,
        QDBusArgument(0, QMetaType.UInt))
    print(result.arguments())
    values, status, errmsg = result.arguments()
    print(f"GetMultipleVcp returned: {values=} {status=} {errmsg=}\n")

    model, mccs_major, mccs_minor, commands, capabilities, status, errmsg = \
        ddcutil_dbus_iface.call(
            "GetCapabilitiesMetadata",
            -1,
            vdu.edid_txt,
            QDBusArgument(0, QMetaType.UInt)).arguments()
    print(f"GetCapabilitiesMetadata returned: {model=}\n{capabilities=}\n")

print("Status Values:")
status_values = ddcutil_dbus_props.call(
    "Get", "com.ddcutil.DdcutilInterface", "StatusValues").arguments()[0]
print(status_values)
for value, name in status_values.items():
    print(f"  {value}: {name}")

output_level = ddcutil_dbus_props.call("Get", "com.ddcutil.DdcutilInterface", "DdcutilOutputLevel").arguments()[0]
print(f"\n{output_level=}\n")
ddcutil_dbus_props.call("Set",
                        "com.ddcutil.DdcutilInterface",
                        "DdcutilOutputLevel",
                        QDBusVariant(QDBusArgument(20, QMetaType.UInt)))

output_level = ddcutil_dbus_props.call("Get", "com.ddcutil.DdcutilInterface", "DdcutilOutputLevel").arguments()[0]
print(f"{output_level=}\n")