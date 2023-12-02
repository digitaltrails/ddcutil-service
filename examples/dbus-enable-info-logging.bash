#!/bin/bash
# Turn on the service info and debug messages
set -x
echo Setting property:
dbus-send --session --dest=com.ddcutil.DdcutilService --print-reply \
   --type=method_call /com/ddcutil/DdcutilObject org.freedesktop.DBus.Properties.Set \
   string:com.ddcutil.DdcutilInterface \
   string:ServiceInfoLogging variant:boolean:true
echo Getting property:
dbus-send --session --dest=com.ddcutil.DdcutilService --print-reply \
   --type=method_call /com/ddcutil/DdcutilObject org.freedesktop.DBus.Properties.Get \
   string:com.ddcutil.DdcutilInterface \
   string:ServiceInfoLogging

