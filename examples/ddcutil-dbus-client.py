# ddcutil-dbus-client.py
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

# dasbus seems to be recent, popular and actively supported.
# (perhaps also look at dbus-next)

from collections import namedtuple

from dasbus.connection import SessionMessageBus

BRIGHTNESS_VCP = 0x10

bus = SessionMessageBus()

ddcutil_proxy = bus.get_proxy(
    "com.ddcutil.libddcutil.DdcutilServer",  # The bus name
    "/com/ddcutil/libddcutil/DdcutilObject",  # The object name
)

# Create a namedtuple that matches the attributes returned by the detect method
DetectedAttributes = namedtuple("DetectedAttributes", ddcutil_proxy.AttributesReturnedByDetect)

# Call detect to get back a list of unnamed tuples, one for each VDU
number_detected, list_of_displays, status, errmsg = ddcutil_proxy.Detect()
print(f"{number_detected=} {status=} {errmsg=}\n")
print(list_of_displays)

# Reform unnamed tuples into a list of namedtuples (optional, for convenience)
vdu_list = [DetectedAttributes(*vdu) for vdu in list_of_displays]
print(vdu_list)

# Play with each VDU - this will change the brightness
for vdu in vdu_list:
    print(vdu.display_number, vdu.manufacturer_id, vdu.model_name)

    val, max_val, formatted_val, status, errmsg = ddcutil_proxy.GetVcp(-1, vdu.edid_hex, BRIGHTNESS_VCP)
    print(f"{val=} {max_val=} {formatted_val=} {status=} {errmsg=}\n")

    # If uncommented, brightness will be changed:
    # print(f"Reducing brightness for {vdu.manufacturer_id=} {vdu.model_name=}")
    # status, errmsg = ddcutil_proxy.SetVcp(-1, vdu.edid_hex, BRIGHTNESS_VCP, val - 1)
    # print(f"{status=} {errmsg=}\n")

    vcp_metadata = ddcutil_proxy.GetVcpMetadata(-1, vdu.edid_hex, BRIGHTNESS_VCP)
    print(vcp_metadata)

    feature_name, desc, is_ro, is_wo, is_rw, is_complex, is_continuous, _, _ = vcp_metadata
    print(f"{is_rw=} {is_complex=} {is_continuous=}\n")

    model, mccs_major, mccs_minor, commands, capabilities, status, errmsg = \
        ddcutil_proxy.GetCapabilitiesMetadata(-1, vdu.edid_hex)

    print(f"{model=}\n{capabilities=}")




