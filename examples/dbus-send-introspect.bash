#!/bin/bash
dbus-send --session --dest=com.ddcutil.DdcutilService --print-reply \
	/com/ddcutil/DdcutilObject \
        org.freedesktop.DBus.Introspectable.Introspect
