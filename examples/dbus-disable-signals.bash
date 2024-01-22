#!/bin/bash
# Turn off change signalling
echo "INFO: Optionally pass true or false as first script argument"
set -x
echo Setting property:
dbus-send --session --dest=com.ddcutil.DdcutilService --print-reply \
   --type=method_call /com/ddcutil/DdcutilObject org.freedesktop.DBus.Properties.Set \
   string:com.ddcutil.DdcutilInterface \
   string:MuteSignals variant:boolean:${1:-true}
echo Getting property:
dbus-send --session --dest=com.ddcutil.DdcutilService --print-reply \
   --type=method_call /com/ddcutil/DdcutilObject org.freedesktop.DBus.Properties.Get \
   string:com.ddcutil.DdcutilInterface \
   string:MuteSignals

