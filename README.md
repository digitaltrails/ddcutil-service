# ddcutil-service 
A D-Bus ddcutil service for control of DDC Monitors/VDUs

The aim of this service is to make it easier to create highly-responsive widgets and apps for 
[ddcutil](https://www.ddcutil.com/).  The service's client interface is now quite stable, but there 
may be some additions or tweaks if new requirements are discovered.

Compared to the `ddcutil` command, the service has a much lower overhead and
much faster response time.  The service only fully initializes `libddcutil` 
and DDC connectivity when first called, whereas `ddcutil` starts from scratch
each time it is run.

The service is written in C.  It has very few dependencies (glib-2 and libddcutil) 
and is consequently quite easy to build.  Compared to other implementations of similar 
services, the code for `ddcutil-service` is quite compact and the abstractions 
are relatively shallow. Providing you know C and a little about glib-2, the code 
should be quite easy to follow.

Once built, running the executable should make a ddcutil service available 
on the D-Bus Session-Bus.  Any type of D-Bus client can be used to interact with 
the service. For example, from the command line you could use the 
systemd `busctl` command:

```
% SERVICE='com.ddcutil.DdcutilService'
% OBJECT='/com/ddcutil/DdcutilObject'
% INTERFACE='com.ddcutil.DdcutilInterface'
% busctl --user call $SERVICE $OBJECT $INTERFACE Detect u 0
% busctl --user call $SERVICE $OBJECT $INTERFACE GetVcp isyu 1 "" 0x10 0
% busctl --user call $SERVICE $OBJECT $INTERFACE SetVcp isyqu 1 "" 16 50 0
```

Several bash and python clients are included in 
the [examples](https://github.com/digitaltrails/ddcutil-service/tree/master/examples)
folder.  They cover multiple ways to interact with the service, including 
the `dbus-send` command and the python `dasbus` and `QtDBus` libraries. 

The service was developed with the assistance of 
amendments to [libddcutil](https://www.ddcutil.com/) by @rockowitz.  

### Usage warning/guidelines

When using this service, avoid excessively writing VCP values because each VDU's NVRAM
likely has a write-cycle limit/lifespan. The suggested guideline is to limit updates
to rates comparable to those observed when using the VDU's onboard controls. Avoid coding
that might rapidly or infinitely loop, including when recovering from errors and bugs.

Non-standard manufacturer specific features should only be experimented with caution,
some may have irreversible consequences, including bricking the hardware.

### Command-line and API Documentation

Detailed documentation can be found in the two manual pages:

- [ddcutil-service.1](https://htmlpreview.github.io/?raw.githubusercontent.com/digitaltrails/ddcutil-service/master/docs/html/ddcutil-service.1.html) - Command line options and service overview. 
- [ddcutil-service.7](https://htmlpreview.github.io/?raw.githubusercontent.com/digitaltrails/ddcutil-service/master/docs/html/ddcutil-service.7.html) - Detailed D-Bus API documentation.


### Installing the DdcutilService as a dbus-daemon auto-started service

#### Installation via prebuilt binaries 

##### OpenSUSE Tumbleweed RPM:

There is an official Tumbleweed RPM:

 - https://software.opensuse.org/package/ddcutil-service

The same page also provides links to unofficial builds I've done for Leap.

##### AUR (Arch Linux User Repository):

Mark Wagie ([yochananmarqos](https://github.com/yochananmarqos)) has kindly provided AUR packaging:

 - https://aur.archlinux.org/packages/ddcutil-service

##### Debian/Ubuntu unofficial packages

Maciej Wójcik ([https://gitlab.com/w8jcik](https://gitlab.com/w8jcik)) has kindly
provided a collection of unofficial debian/ubuntu packages along with instructions
for installing the required versions of `libddcutil`:

 - https://gitlab.com/w8jcik/ddcutil-service.deb#latest

#### Installation via Makefile

Check/modify the dependencies specified in the `Makefile`:
 - `libddcutil-devel` >= 1.4.0
 - `glib2-devel` >= 2.40 (`gio-2` is normally part of `glib2`)

The names of required development packages may vary depending on which distribution 
you are targeting.

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

### Optional ddcutil-client

The source includes an `ddcutil-client` (`ddcutil-client.c` and 
man page `ddcutil-client.1`).  Packaging and deployment of the client is optional.
If `busctl` or `dbus-send` is available, they will be about as fast.
A purpose built client is mostly a convenience for those that might
want a syntactically straight forward command line interface.  

### Acknowledgements

Thanks go out to Sanford Rockowitz ([rockowitz](https://github.com/rockowitz)) 
for [libddcutil, ddcutil](https://www.ddcutil.com/) and all the assistance and 
advice provided during the development of this service.

Thanks also to Michal Suchanek for assistance with the OpenSUSE RPM spec; 
Johan Grande ([nahoj](https://github.com/nahoj)) for the Ubuntu patches; 
Mark Wagie ([yochananmarqos](https://github.com/yochananmarqos)) for AUR packaging; and
Maciej Wójcik ([https://gitlab.com/w8jcik](https://gitlab.com/w8jcik)) for debian/ubuntu
packaging.

The original development IDE was **[JetBrains CLion-Nova/CLion](https://www.jetbrains.com/help/clion/clion-nova-introduction.html)**. Thanks go out to JetBrains for
granting me an [Open Source development license]( https://jb.gg/OpenSourceSupport).

### Version History
- 1.0.14
  - Default to libddcutil event detection for libddcutil >= 2.2 (for faster response to changes).
  - Add option --prefer-libddcutil-events as a better name for deprecated option --prefer-drm.
  - Deprecate option --prefer-drm as it's name is misleading.
  - Add option --disable-connectivity-signals to allow connectivity signals to be turned off.
  - Add options --disable-hotplug-polling and --disable-dpms-polling to accommodate quirky monitors.
  - Always internally poll for DPMS changes (DPMS is not covered by libddcutil events). 
  - Add method ListDetected to take advantage of hotplug detection in libddcutil >= 2.2.
  - Add the list command to ddcutil-client to provide access to the new ListDetected method. 
  - Add wait, wait-connection-change, and wait-vcp-change commands to ddcutil-client.
  - Log more information when get_vcp fails.
- 1.0.13
  - Version 1.0.13 existed in development for some months, but was not released. 
  - Connectivity DBUS signalling was going to be on by default, but drivers, GPUs, and 
    monitors proved too inconsistent.
- 1.0.12
  - Return the error status-code if enable_ddca_watch_displays fails - was returning OK even on failure.
- 1.0.11
  - Alter the detect-function for ddcutil 2.5.1 (generates more logging/warning info).
  - Add a DETECT_ALL option to control whether disabled/powered-off VDU's are to be included in the results from detect.
  - Reduce the number of messages generated when polling for hotplug events.
  - Fixes to API documentation.
- 1.0.9
  - Fixed a GetCapabilitiesMetadata bug that caused some VCP features to lack metadata values.
  - Fixed the return of feature-name and feature-description from GetVcpMetadata.
  - Fixed potential hotplug/DPMS polling memory leaks and simplified event locking.
  - Recoded hotplug/DPMS polling to avoid a potential libddcutil assertion failure.
  - Fixed code/doc typos, improved code readability/structure, reduced IDE warnings.
  - Updated documentation to caution against excessive updates when coding loops, as this may impact VDU NVRAM lifespan.
  - Updated documentation to caution against experimenting with non-standard features, as it may risk damage to the VDU.
  - Added an optional ddcutil-client, a fast counterpart to ddcutil. Not built/packaged by default (unnecessary). 
- 1.0.7
  - Slightly improved setvcp diagnostics.
  - Fix methods failing with return code DDCRC_OTHER (-3022) when only some i2c devices are accessible.
- 1.0.6
  - Add SetVcp/SetVcpWithContext NO_VERIFY (no retry) flag option.
  - Match the behaviour of the ddcutil command, default to verify-and-retry for all set-vcp method calls.
  - Default to verify-and-retry for all libddcutil versions.
  - Replace the stateful DdcutilVerifySetVcp property with the stateless NO_VERIFY flag.
  - Fix the ServiceFlagOptions property so that it lists all flag options.
  - Check the status returned by libddcutil ddca_init() and exit on error to prevent any inconsistent behaviour.
  - Cleanup the --prefer-polling and --prefer-drm options to make them consistent with each other.
- 1.0.4
  - Provide an API flag RETURN_RAW_VALUES which disables GetVcp high-byte masking of Simple Non-Continuous features.
  - Provide the --return-raw-values command line option for the same purpose.
- 1.0.3
  - Reduce unnecessary logging.
  - Improve the description of the service's signals in ddcutil-service.1.
  - Correct the typo in option name --prefer-drm (it was mistakenly called --prefer-dma).
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
