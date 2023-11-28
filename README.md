# ddcutil-dbus
A D-Bus ddcutil server for control of DDC Monitors/VDUs

This code is a work in progress, it is possibly buggy and the API may
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

Method inputs can be supplied as CSV, for example, Method Input to `GetVcp` could be 

```
1,'',0x10
```
This would access display `1`, blank-edid `''`, DDC VCP Feature code `0x10` (brightness). 
VDU's are identified either by display-number or EDID-hex.

D-feet can also query the service properties.  Some properties are R/W, but I don't think d-feet can write them.

Several bash and python scripts that demonstrate using the service are included in the [examples](https://github.com/digitaltrails/ddcutil-dbus/tree/master/examples)
folder.


### Installing the DdcutilService as a dbus-daemon auto-started service

#### Installation via Makefile

Check the install PREFIX in the `Makefile` and make any changes required 
for your circumstances, and issue `make install` or `sudo make install`
as appropriate.

#### Manual installation steps

Manual steps for installing a dbus-daemon service file for auto-starting and 
auto-restarting the service are as follows:

1. Edit `com.ddcutil.DdcutilService.service` and set the Exec location of 
   the server executable (can be anywhere).
2. Install to: `/usr/share/dbus-1/services/com.ddutil.DdcutilService.service`
   Or maybe to: `$HOME/.local/share/dbus-1/services/com.ddutil.DdcutilService.service`
3. Install the executable at the location set in step 1.
4. Logout and login to a new session.
5. Check service is available (use d-feet or any other test program).

Installing the service file is optional. The service file enables autostart of 
the service by `dbus-daemon`.  The server executable can be manually started 
without the service file being installed.  

### Use with `vdu_controls`

The `vdu_controls` GUI has been branched and modified to optionally use the service, see:
https://github.com/digitaltrails/vdu_controls/tree/d-bus

