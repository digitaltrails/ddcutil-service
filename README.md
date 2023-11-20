# ddcutil-dbus
A D-Bus ddcutil server for control of DDC Monitors/VDUs

This code is a work in progress, it is incomplete, possibly buggy and the API will
change when the requirements are better understood.

Once built, running the executable should make a ddcutil service available on 
the D-Bus Session-Bus.

Testing using the d-feet D-Bus interactive GUI: 
1. start d-feet;
2. press the session-bus button in the d-feet header;
3. search for ddcutil.
4. click on com.ddcutil.DdcutilServer
5. open the Object Path com/ddcutil/DdcutilServer and 
   navigate down to the com.ddcutil.DdcutilInterface
7. Double-click methods and properties to run them.

Method inputs can be supplied as CSV, for example, Method Input to GetFeatureValue could be 

```
1,'',0x10
```
This would access display `1`, blank-edid `''`, DDC VCP Feature code `0x10`. 
VDU's are identified either by display-number or EDID-hex.

Some properties are R/W, but I don't think d-feet can write them.


