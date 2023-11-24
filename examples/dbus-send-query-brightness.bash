#!/bin/bash
# Example 'ddcutil getvcp 1 10' where display is 1 and the VCP-Code is 10 hex (0x10).
dbus-send --dest=com.ddcutil.DdcutilService --print-reply --type=method_call /com/ddcutil/DdcutilObject  com.ddcutil.DdcutilInterface.GetVcp int32:1 string: byte:16 uint32:0
