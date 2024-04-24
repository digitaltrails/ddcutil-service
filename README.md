# ddcutil-service 
A D-Bus ddcutil service for control of DDC Monitors/VDUs

The aim of this service is to make it easier to create widgets and apps for 
**ddcutil**.  The service's client interface is now quite stable, but there 
may be some additions or tweaks if new requirements are discovered.

The service is written in C.  It has very few dependencies (glib and libddcutil) and is
consequently quite easy to build.  Once built, running the executable should make 
a ddcutil service available on the D-Bus Session-Bus.


Testing using the d-feet D-Bus interactive GUI: 
1. start d-feet;
2. press the `session-bus` button in the d-feet header;
3. search for `ddcutil`.
4. click on `com.ddcutil.DdcutilService`
5. open the *Object Path* `com/ddcutil/DdcutilService` and 
   navigate down to the `com.ddcutil.DdcutilInterface`
7. Double-click methods and properties to run or view them.

Method inputs can be supplied as CSV, for example, Method Input to `GetVcp` could be 

```
1,'',0x10
```
This would access display `1`, blank-edid `''`, DDC VCP Feature code `0x10` 
(brightness). VDU's are identified either by display-number or base-64-encoded
EDID.

Several bash and python scripts that demonstrate using the service are included in 
the [examples](https://github.com/digitaltrails/ddcutil-service/tree/master/examples)
folder.  They cover the use of the `dbus-send` command line utiltity
and the python `dasbus` and `QtDBus` libraries. 

The service was developed with the assistance of amendments to [libddcutil](https://www.ddcutil.com/) by @rockowitz.  The current intention is 
to eventually package it with ddcutil/libddcutil.

### Command-line and API Documentation

Detailed documentation can be found in the two manual pages:

- [ddcutil-service.1](https://htmlpreview.github.io/?raw.githubusercontent.com/digitaltrails/ddcutil-service/master/docs/html/ddcutil-service.1.html) - Command line options and service overview. 
- [ddcutil-service.7](https://htmlpreview.github.io/?raw.githubusercontent.com/digitaltrails/ddcutil-service/master/docs/html/ddcutil-service.7.html) - Detailed D-Bus API documentation.


### Installing the DdcutilService as a dbus-daemon auto-started service

#### Installation via prebuilt binaries 

###### OpenSUSE Tumbleweed RPM:

There is an official Tumbleweed RPM:

 - https://software.opensuse.org/package/ddcutil-service

The same page also provides links to unoffical builds I've done for Leap.

###### AUR (Arch Linux User Repository):

 - https://aur.archlinux.org/packages/ddcutil-service

#### Installation via Makefile

Check the install PREFIX in the `Makefile` and make any changes required 
for your circumstances, and issue `make install` or `sudo make install`
as appropriate.

#### Manual installation steps

Manual steps for installing a dbus-daemon service file for auto-starting and 
auto-restarting the service are as follows:

1. Edit `com.ddcutil.DdcutilService.service` and set the Exec location of 
   the service executable (can be anywhere).
2. Install to: `/usr/share/dbus-1/services/com.ddutil.DdcutilService.service`
   Or maybe to: `$HOME/.local/share/dbus-1/services/com.ddutil.DdcutilService.service`
3. Install the executable at the location set in step 1.
4. Logout and login to a new session.
5. Check service is available (use d-feet or any other test program).

Installing the service file is optional. The service file enables autostart of 
the service by `dbus-daemon`.  The service executable can be manually started 
without the service file being installed.

### Use with `vdu_controls`

The `vdu_controls` GUI has been modified to use the service by default, see:
https://github.com/digitaltrails/vdu_controls

### Acknowledgements

Thanks go out to Sanford Rockowitz ([rockowitz](https://github.com/rockowitz)) 
for [libddcutil, ddcutil](https://www.ddcutil.com/) and all the assistance and 
advice provided during the development of this service.

Thanks also to Michal Suchanek for assistance with the OpenSUSE RPM spec; 
Johan Grande ([nahoj](https://github.com/nahoj)) for the Ubuntu patches; 
and Mark Wagie ([yochananmarqos](https://github.com/yochananmarqos)) for AUR packaging.

The development IDE is **[JetBrains CLion-Nova/CLion](https://www.jetbrains.com/help/clion/clion-nova-introduction.html)**. Thanks go out to JetBrains for
granting the [Open Source development license]( https://jb.gg/OpenSourceSupport).

### Version History
- 1.0.5
  - Add SetVcp NO_VERIFY flag option.
  - Default to verify-and-retry for all SetVcp method calls - match the behaviour of the ddcutil command.
  - Ensure the service defaults to verify-and-retry for all libddcutil versions.
  - Remove the stateful DdcutilVerifySetVcp property and replace it with the stateless SetVcp verification flag.
  - Fix the ServiceFlagOptions property - it was not listing all flag options.
  - Check the status returned by libddcutil ddca_init() and exit on error - prevent any inconsistent behaviour.
  - Cleanup the --prefer-polling and --prefer-drm options.
- 1.0.4
  - Provide an API flag RETURN_RAW_VALUES which disables GetVcp high-byte masking of Simple Non-Continuous features.
  - Provide the --return-raw-values command line option for the same purpose.
- 1.0.3
  - Reduce unnecessary logging.
  - Improve the description of the service's signals in ddcutil-service.1.
  - Correct the typo in option name --perfer-drm (it was mistakenly called --prefer-dma).
  - For simple VCP-features, only return the low-byte, for some VDUs the high-byte might contain junk.
- 1.0.2
  - Added VcpValueChanged D-Bus signal which triggers if the SetVcp method succeeds. This is to allow
    multiple clients to be aware of changes made by each.
  - Added SetVcpWithContext which accepts a client-context to be returned with the VcpValueChanged signal.
  - ServiceEmitSignals renamed to ServiceEmitConnectivitySignals to avoid confusion.
  - Command line option --emit-signals renamed to --emit-connectivity-signals for the same reason.
  - Fix ServiceEmitSignals property assignment so that it correctly toggles hotplug signals.
  - Fix hotplug polling so that it remains an option no matter what version of libddcutil is in use. ]()
- 1.0.1
  - Use gcc with -Wformat-security for safety and to match Arch and Ubuntu defaults.
  - Calling ddca_init() before verify_i2c() to fix runtime error for libddcutil >= 2.1.
- 1.0.0
  - Initial Release
