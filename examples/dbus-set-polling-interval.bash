#!/bin/bash
cat <<EOF

  $0 <seconds>

  Set the change detection polling interval for the ddcutil-service's 
  internal change detection - an alternative to libddcutil's change
  detection.

  Optionally pass the number of seconds as a script argument.
  Defaults to setting 30 seconds.
  Pass 0 (zero) to disable polling.

EOF
echo Setting property:
dbus-send --session --dest=com.ddcutil.DdcutilService --print-reply \
   --type=method_call /com/ddcutil/DdcutilObject org.freedesktop.DBus.Properties.Set \
   string:com.ddcutil.DdcutilInterface \
   string:ServicePollInterval variant:uint32:${1:-30}
echo Getting property:
dbus-send --session --dest=com.ddcutil.DdcutilService --print-reply \
   --type=method_call /com/ddcutil/DdcutilObject org.freedesktop.DBus.Properties.Get \
   string:com.ddcutil.DdcutilInterface \
   string:ServicePollInterval

