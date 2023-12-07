#!/bin/bash
# Turn off change signalling
set -x
echo Setting property:
dbus-send --session --dest=com.ddcutil.DdcutilService --print-reply \
   --type=method_call /com/ddcutil/DdcutilObject org.freedesktop.DBus.Properties.Set \
   string:com.ddcutil.DdcutilInterface \
   string:ServiceSignalChanges variant:boolean:false
echo Getting property:
dbus-send --session --dest=com.ddcutil.DdcutilService --print-reply \
   --type=method_call /com/ddcutil/DdcutilObject org.freedesktop.DBus.Properties.Get \
   string:com.ddcutil.DdcutilInterface \
   string:ServiceSignalChanges

