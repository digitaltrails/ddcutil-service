#!/bin/bash
# Turn on the service info and debug messages
dbus-send --session --dest=com.ddcutil.DdcutilService --print-reply \
   --type=method_call /com/ddcutil/DdcutilObject org.freedesktop.DBus.Properties.Set \
   string:com.ddcutil.DdcutilInterface \
   string:ServiceInfoLogging variant:boolean:true
