# ddcutil-dasbus-client.py
# ----------------------
# Python dasbus example of using ddcutil-dbus-server
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
# dasbus seems to be recent, popular and actively supported.
# (perhaps also look at dbus-next)

from collections import namedtuple

from dasbus.connection import SessionMessageBus

DO_SET_VCP_TEST = False   # If enabled, a test will run to change VDU brightness.

BRIGHTNESS_VCP = 0x10
CONTRAST_VCP = 0x12

bus = SessionMessageBus()

ddcutil_proxy = bus.get_proxy(
    "com.ddcutil.DdcutilService",  # The bus name
    "/com/ddcutil/DdcutilObject",  # The object name
)

# Create a namedtuple that matches the attributes returned by the detect method
DetectedAttributes = namedtuple("DetectedAttributes", ddcutil_proxy.AttributesReturnedByDetect)

# Call detect to get back a list of unnamed tuples, one for each VDU
number_detected, list_of_displays, status, errmsg = ddcutil_proxy.Detect(1)
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

    val, max_val, formatted_val, status, errmsg = ddcutil_proxy.GetVcp(-1, vdu.edid_txt, BRIGHTNESS_VCP, 0)
    print(f"GetVcp returned: {val=} {max_val=} {formatted_val=} {status=} {errmsg=}\n")

    if DO_SET_VCP_TEST:
        print(f"Reducing brightness for {vdu.manufacturer_id=} {vdu.model_name=}")
        status, errmsg = ddcutil_proxy.SetVcp(-1, vdu.edid_txt, BRIGHTNESS_VCP, val - 1, 0)
        print(f"SetVcp returned: {status=} {errmsg=}\n")

    vcp_metadata = ddcutil_proxy.GetVcpMetadata(-1, vdu.edid_txt, BRIGHTNESS_VCP, 0)
    print("GetVcpMetadata returned:", vcp_metadata)
    feature_name, desc, is_ro, is_wo, is_rw, is_complex, is_continuous, _, _ = vcp_metadata
    print(f"metadata: {is_rw=} {is_complex=} {is_continuous=}\n")

    values, status, errmsg = ddcutil_proxy.GetMultipleVcp(-1, vdu.edid_txt, [BRIGHTNESS_VCP, CONTRAST_VCP], 0)
    print(f"GetMultipleVcp returned: {values=} {status=} {errmsg=}\n")

    model, mccs_major, mccs_minor, commands, capabilities, status, errmsg = \
        ddcutil_proxy.GetCapabilitiesMetadata(-1, vdu.edid_txt, 0)
    print(f"GetCapabilitiesMetadata returned: {model=}\n{capabilities=}\n")

print("Status Values:")
status_values = ddcutil_proxy.StatusValues
for value, name in status_values.items():
    print(f"  {value}: {name}")


