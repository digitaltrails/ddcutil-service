#!/bin/bash
dbus-send --dest=com.ddcutil.DdcutilService --print-reply --type=method_call /com/ddcutil/DdcutilObject  com.ddcutil.DdcutilInterface.GetVcp int32:1 string: byte:16 uint32:0
