#!/bin/bash
busctl --user tree com.ddcutil.DdcutilService
busctl --user introspect com.ddcutil.DdcutilService /com/ddcutil/DdcutilObject
busctl --user call com.ddcutil.DdcutilService /com/ddcutil/DdcutilObject com.ddcutil.DdcutilInterface Detect u "0"
busctl --user call com.ddcutil.DdcutilService /com/ddcutil/DdcutilObject com.ddcutil.DdcutilInterface GetVcp isyu "1" "" "16" "0"
busctl --user call com.ddcutil.DdcutilService /com/ddcutil/DdcutilObject com.ddcutil.DdcutilInterface SetVcp isyqu "1" "" "16" "50"  "0"
