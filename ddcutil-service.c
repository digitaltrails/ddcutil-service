/* ----------------------------------------------------------------------------------------------------
 * ddcutil-service.c
 * -----------------
 * A D-Bus server for libddcutil/ddcutil
 *
 *
 * Copyright (C) 2023, Michael Hamilton
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*
 * Code structure based on https://github.com/bratsche/glib/blob/master/gio/tests/gdbus-example-server.c
 *
 * Info on D-Bus:     https://dbus.freedesktop.org/doc/dbus-specification.html
 * Info on GDbus:     https://docs.gtk.org/glib/
 * Info on lbddcutil: https://www.ddcutil.com/api_main/ and the inline documentation the header files at
 *                    https://github.com/rockowitz/ddcutil/tree/2.0.2-dev/src/public
 */

/**
 * Logging:
 * Using glib logging which defaults to syslog if running unde dbus-daemon, or the stderr otherwise
 * g_info() and g_debug are classed the same, and don't show by default
 * g_message() is higher and always shows.
 * g_warning() is for non fatal errors.
 * g_critical() is for serious errors (which, as a class, can optionally be set to terminate the progam).
 * g_error() is for programming errors and will automatically core dump - so don't use g_error().
 * There is also a special domain "all" - not sure if we want to use that.
 */
#define PROGRAM_NAME "ddcutil-service"
#define G_LOG_USE_STRUCTURED
#define G_LOG_DOMAIN PROGRAM_NAME  // set the log domain before including the glib headers.

#include <gio/gio.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <glob.h>
#include <unistd.h>
#include <spawn.h>

#include <ddcutil_c_api.h>
#include <ddcutil_status_codes.h>
#include <ddcutil_macros.h>


#define DDCUTIL_DBUS_INTERFACE_VERSION_STRING "1.0.1"
#define DDCUTIL_DBUS_DOMAIN "com.ddcutil.DdcutilService"

#if DDCUTIL_VMAJOR == 2 && DDCUTIL_VMINOR == 0 && DDCUTIL_VMICRO < 2
    #define LIBDDCUTIL_HAS_OPTION_ARGUMENTS
    #define LIBDDCUTIL_HAS_DDCA_GET_SLEEP_MULTIPLIER
#elif DDCUTIL_VMAJOR >= 2
    #define LIBDDCUTIL_HAS_CHANGES_CALLBACK
    #define LIBDDCUTIL_HAS_OPTION_ARGUMENTS
    #define LIBDDCUTIL_HAS_INDIVIDUAL_SLEEP_MULTIPLIER
    #define LIBDDCUTIL_HAS_DYNAMIC_SLEEP
#else
    #define LIBDDCUTIL_HAS_DDCA_GET_DEFAULT_SLEEP_MULTIPLIER
#endif

#undef XML_FROM_INTROSPECTED_DATA

static gboolean display_status_detection_enabled = FALSE;
static gboolean enable_signals = FALSE;
static gboolean prefer_polling = TRUE;
static gboolean lock_configuration = FALSE;

#define VERIFY_I2C

static int service_broken_error = -1;

#define MIN_POLL_SECONDS 10
#define DEFAULT_POLL_SECONDS 30

/**
 * If severice is doing it's own polling for events, how often to poll:
 */
static long poll_interval_micros = DEFAULT_POLL_SECONDS * 1000000;

#define MIN_POLL_CASCADE_INTERVAL_SECONDS 0.1
#define DEFAULT_POLL_CASCADE_INTERVAL_SECONDS 0.5
/**
* Each event in an event-cascade will be at least this far appart:
*/
static long poll_cascade_interval_micros = (long) (DEFAULT_POLL_CASCADE_INTERVAL_SECONDS * 1000000);

#if defined(LIBDDCUTIL_HAS_CHANGES_CALLBACK)
/**
 * Custom signal event data - used by the service's custom signal source
 */
typedef DDCA_Display_Status_Event Event_Data_Type;

#else

/**
 * Define our own event type enum which mirrors the future one in libddcutil 2.1
 */
typedef enum {
    DDCA_EVENT_DPMS_AWAKE,
    DDCA_EVENT_DPMS_ASLEEP,
    DDCA_EVENT_DISPLAY_CONNECTED,
    DDCA_EVENT_DISPLAY_DISCONNECTED,
    DDCA_EVENT_UNUSED1,
    DDCA_EVENT_UNUSED2,
} DDCA_Display_Event_Type;

/**
 * Define our own event data which mirrors the future one in libddcutil 2.1
 */
typedef struct {
    DDCA_Display_Event_Type event_type;
    DDCA_Display_Ref        dref;
} Event_Data_Type;;

#endif

/**
 * List of DDCA events numbers and names that can be iterated over (for return from a service property).
 */
static const int event_types[] = {
    DDCA_EVENT_DISPLAY_CONNECTED, DDCA_EVENT_DISPLAY_DISCONNECTED,
    DDCA_EVENT_DPMS_AWAKE, DDCA_EVENT_DPMS_ASLEEP,
};

/**
 * List of DDCA events names in the same order as the event_types array.
 */
static const char* event_type_names[] = {
    G_STRINGIFY(DDCA_EVENT_DISPLAY_CONNECTED), G_STRINGIFY(DDCA_EVENT_DISPLAY_DISCONNECTED),
    G_STRINGIFY(DDCA_EVENT_DPMS_AWAKE), G_STRINGIFY(DDCA_EVENT_DPMS_ASLEEP),
};

/**
 * \brief for a given event_type_num return its name
 * \param event_type_num
 * \return name or "unknown_event_type"
 */
static const char* get_event_type_name(int event_type_num) {
    for (int i = 0; i < sizeof(event_types) / sizeof(int); i++) {
        if (event_types[i] == event_type_num) {
            return event_type_names[i];
        }
    }
    return "unknown_event_type";
}

/**
 * Global data value - held for dispatch - accessed/updated atomically.
 */
static Event_Data_Type* signal_event_data = NULL;

/**
 * List of fields returned in by the Detect service method (for return from a service property).
 */
static const char* attributes_returned_from_detect[] = {
    "display_number", "usb_bus", "usb_device",
    "manufacturer_id", "model_name", "serial_number", "product_code",
    "edid_txt", "binary_serial_number",
    NULL
};

/**
 * Boolean flags that can be passed in the service method flags argument.
 */
typedef enum {
    EDID_PREFIX = 1,  // Indicates the EDID passed to the service is a unique prefix (substring) of the actual EDID.
} Flags_Enum_Type;

/**
 * Iterable definitions of Flags_Enum_Type values/names (for return from a service property).
 */
const int flag_options[] = {EDID_PREFIX,};
const char* flag_options_names[] = {G_STRINGIFY(EDID_PREFIX),};

/* ----------------------------------------------------------------------------------------------------
 * D-Bus interface definition in XML
 */

static GDBusNodeInfo* introspection_data = NULL;

/**
 * Introspection data for the service we are exporting
 * TODO At some point this could possibly be moved to a file, but maybe is handy to embed it here.
 * Uses GTK-Doc comment blocks, see https://dbus.freedesktop.org/doc/dbus-api-design.html#annotations
 * The @my_parameter anotation formats properly.  The #my_property:property and #my_sig::signal annotations
 * don't format at all, so I'm not using them.
 */
static const gchar introspection_xml[] = R"(

<node>
    <!--
        com.ddcutil.DdcutilInterface:
        @short_description: D-Bus service for libddcutil VESA DDC Monitor Virtual Control Panel

        ddcutil-service  is  D-Bus  service wrapper for libddcutil which implements the VESA
        DDC Monitor Control Command Set.  Most things that can be controlled using a monitor's
        on-screen display can be controlled by this service.


        For many of the methods a VDU can be specified by either passing the DDC display_number
        or DDC EDID. The EDID is the more stable identifier, it remains unchanged if the number
        of connected or poweredup VDUs changes, whereas the DDCA display numbers may be reallocated.

        As a convienience for passing EDIDs using the command line, methods that accept an EDID
        identifier also accept a flag value which will cause the EDID passed to be matched as
        a prefix of a possible EDID (so all 128 bytes need not be entered).

    -->
    <interface name='com.ddcutil.DdcutilInterface'>

    <!--
        Restart:
        @text_options: Text options to be passed to libddcutil ddca_init().
        @syslog_level: The libddcutil syslog level.
        @flags: For furture use.
        @error_status: A libddcutil DDCRC error status.  DDCA_OK (zero) if no errors have occured.
        @error_message: Text message for error_status.

        Restarts the service with the supplied parameters.

        If the service is configuration-locked, an com.ddcutil.DdcutilService.Error.ConfigurationLocked
        error is raised.
    -->
    <method name='Restart'>
        <arg name='text_options' type='s' direction='in'/>
        <arg name='syslog_level' type='u' direction='in'/>
        <arg name='flags' type='u' direction='in'/>
        <arg name='error_status' type='i' direction='out'/>
        <arg name='error_message' type='s' direction='out'/>
    </method>

    <!--
        Detect:
        @flags: If set to 1, any invalid VDUs will be included in the results.
        @number_of_displays: The number of VDUs detected (the length of @detected_displays).
        @detected_displays: An array of structures describing the VDUs.
        @error_status: A libddcutil DDCRC error status.  DDCA_OK (zero) if no errors have occured.
        @error_message: Text message for error_status.

        Issues a detect and returns the VDUs detected.

        The array @detected_displays will be of length @number_of_displays.

        Each element of @detected_displays array will contain the fields
        specified by the AttributesReturnedByDetect property.  The fields
        will include the libddcutil display-number and a base64-encoded EDID.
    -->
    <method name='Detect'>
        <arg name='flags' type='u' direction='in'/>
        <arg name='number_of_displays' type='i' direction='out'/>
        <arg name='detected_displays' type='a(iiisssqsu)' direction='out'/>
        <arg name='error_status' type='i' direction='out'/>
        <arg name='error_message' type='s' direction='out'/>
        </method>

    <!--
        GetVcp:
        @display_number: The libddcutil/ddcutil display number to query
        @edid_txt: The base-64 encoded EDID of the display
        @vcp_code: The VPC-code to query, for example, 16 (0x10) is brightness.
        @flags: If 1, the @edid_txt is matched as a unique prefex of the EDID.
        @vcp_current_value: The current numeric value as a unified 16 bit integer.
        @vcp_max_value: The maximum possible value, to allow for easy calculation of current/max.
        @vcp_formatted_value: A formatted version of the value including related info such as the max-value.
        @error_status: A libddcutil DDCRC error status.  DDCA_OK (zero) if no errors have occured.
        @error_message: Text message for error_status.

        Retrieve the value for a VCP-code for the specified VDU.

        For simplicity the @vcp_current_value returned will always be 16 bit integer (most
        VCP values are single byte 8-bit intergers, very few are two-byte 16-bit).

        The @vcp_formatted_value contains the current value along with any related info,
        such as the maximum value, its similar to the output of the ddcutil getvcp shell-command.
    -->
    <method name='GetVcp'>
        <arg name='display_number' type='i' direction='in'/>
        <arg name='edid_txt' type='s' direction='in'/>
        <arg name='vcp_code' type='y' direction='in'/>
        <arg name='flags' type='u' direction='in'/>
        <arg name='vcp_current_value' type='q' direction='out'/>
        <arg name='vcp_max_value' type='q' direction='out'/>
        <arg name='vcp_formatted_value' type='s' direction='out'/>
        <arg name='error_status' type='i' direction='out'/>
        <arg name='error_message' type='s' direction='out'/>
    </method>

    <!--
        GetMultipleVcp:
        @display_number: the libddcutil/ddcutil display number to query
        @edid_txt: the base-64 encoded EDID of the display
        @vcp_code: the VPC-code to query.
        @flags: If 1, the @edid_txt is matched as a unique prefex of the EDID.
        @vcp_current_value: An array of VCP-codes and values.
        @error_status: A libddcutil DDCRC error status.  DDCA_OK (zero) if no errors have occured.
        @error_message: Text message for error_status.

        Retrieves several different VCP values for the specified VDU. This is a convienience
        method provided to more efficiently utilise D-Bus.

        Each entry in @vcp_current_value array is a VCP-code along with its
        current, maximum and formatted values (the same as those returned by GetVcp).
    -->
    <method name='GetMultipleVcp'>
        <arg name='display_number' type='i' direction='in'/>
        <arg name='edid_txt' type='s' direction='in'/>
        <arg name='vcp_code' type='ay' direction='in'/>
        <arg name='flags' type='u' direction='in'/>
        <arg name='vcp_current_value' type='a(yqqs)' direction='out'/>
        <arg name='error_status' type='i' direction='out'/>
        <arg name='error_message' type='s' direction='out'/>
    </method>

    <!--
        SetVcp:
        @display_number: the libddcutil/ddcutil display number to alter
        @edid_txt: the base-64 encoded EDID of the display
        @vcp_code: the VPC-code to query.
        @vcp_new_value: the numeric value as a 16 bit integer.
        @flags: If 1, the @edid_txt is matched as a unique prefex of the EDID.
        @error_status: A libddcutil DDCRC error status.  DDCA_OK (zero) if no errors have occured.
        @error_message: Text message for error_status.

        Set the value for a VCP-code for the specified VDU.

        For simplicity the @vcp_new_value is always passed as a 16 bit integer (most
        VCP values are single byte 8-bit intergers, very few are two-byte 16-bit).
    -->
    <method name='SetVcp'>
        <arg name='display_number' type='i' direction='in'/>
        <arg name='edid_txt' type='s' direction='in'/>
        <arg name='vcp_code' type='y' direction='in'/>
        <arg name='vcp_new_value' type='q' direction='in'/>
        <arg name='flags' type='u' direction='in'/>
        <arg name='error_status' type='i' direction='out'/>
        <arg name='error_message' type='s' direction='out'/>
    </method>

    <!--
        GetVcpMetadata:
        @display_number: the libddcutil/ddcutil display number to query
        @edid_txt: the base-64 encoded EDID of the display
        @vcp_code: the VPC-code to query.
        @flags: If 1, the @edid_txt is matched as a unique prefex of the EDID.
        @feature_name: the feature name for the VCP-code
        @feature_description: the feature description, if any, of the VCP-code.
        @is_read_only: True if the feature is read-only.
        @is_write_only: True if the feature is write-only (for example, a code that turns the VDU off).
        @is_rw: True if the feature is readable and writable.
        @is_complex: True if the feature is complex (multi-byte).
        @is_continuous: True in the feature is a continuous value (it is not an enumeration).
        @error_status: A libddcutil DDCRC error status.  DDCA_OK (zero) if no errors have occured.
        @error_message: Text message for error_status.

        Retrieve the metadata for a VCP-code for the specified VDU.
    -->
    <method name='GetVcpMetadata'>
        <arg name='display_number' type='i' direction='in'/>
        <arg name='edid_txt' type='s' direction='in'/>
        <arg name='vcp_code' type='y' direction='in'/>
        <arg name='flags' type='u' direction='in'/>
        <arg name='feature_name' type='s' direction='out'/>
        <arg name='feature_description' type='s' direction='out'/>
        <arg name='is_read_only' type='b' direction='out'/>
        <arg name='is_write_only' type='b' direction='out'/>
        <arg name='is_rw' type='b' direction='out'/>
        <arg name='is_complex' type='b' direction='out'/>
        <arg name='is_continuous' type='b' direction='out'/>
        <arg name='error_status' type='i' direction='out'/>
        <arg name='error_message' type='s' direction='out'/>
    </method>

    <!--
        GetCapabilitiesString:
        @display_number: the libddcutil/ddcutil display number to query
        @edid_txt: the base-64 encoded EDID of the display
        @flags: If 1, the @edid_txt is matched as a unique prefex of the EDID.
        @capabilities_text: the capability string for the VDU.
        @error_status: A libddcutil DDCRC error status.  DDCA_OK (zero) if no errors have occured.
        @error_message: Text message for error_status.

        Retrieve the capabilities metadata for a VDU in a format similar to that output by
        the ddcutil detect shell-command (similar enough for parsing by common code).
    -->
    <method name='GetCapabilitiesString'>
        <arg name='display_number' type='i' direction='in'/>
        <arg name='edid_txt' type='s' direction='in'/>
        <arg name='flags' type='u' direction='in'/>
        <arg name='capabilities_text' type='s' direction='out'/>
        <arg name='error_status' type='i' direction='out'/>
        <arg name='error_message' type='s' direction='out'/>
    </method>

    <!--
        GetCapabilitiesMetadata:
        @display_number: the libddcutil/ddcutil display number to query
        @edid_txt: the base-64 encoded EDID of the display
        @flags: If 1, the @edid_txt is matched as a unique prefex of the EDID.
        @model_name: parsed model name string
        @mccs_major: MCCS major version number byte.
        @mccs_minor: MCCS minor version number byte.
        @commands: supported commands as a dictionary indexed by command number.
        @capabilities: supported VCP features as a dictionary indexed by VCP-code.
        @error_status: A libddcutil DDCRC error status.  DDCA_OK (zero) if no errors have occured.
        @error_message: Text message for error_status.

        Retrieve the capabilities metadata for a VDU in a parsed dictionary structure
        indexed by VCP code.

        The @capabilities out parameter is an array of dictionary entries.  Each entry consists
        of a VCP-code along with a struct containing the feature-name, feature-description,
        and an array of permitted-values. For features that have continuous values, the associated
        permitted-value array will be empty.  For non-continuous features, the permitted-value
        array will contain a dictionary entry for each permitted value, each entry containing
        a permiited-value and value-name.
    -->
    <method name='GetCapabilitiesMetadata'>
        <arg name='display_number' type='i' direction='in'/>
        <arg name='edid_txt' type='s' direction='in'/>
        <arg name='flags' type='u' direction='in'/>
        <arg name='model_name' type='s' direction='out'/>
        <arg name='mccs_major' type='y' direction='out'/>
        <arg name='mccs_minor' type='y' direction='out'/>
        <arg name='commands' type='a{ys}' direction='out'/>
        <arg name='capabilities' type='a{y(ssa{ys})}' direction='out'/>
        <arg name='error_status' type='i' direction='out'/>
        <arg name='error_message' type='s' direction='out'/>
    </method>

    <!--
        GetDisplayState:
        @display_number: the libddcutil/ddcutil display number to query
        @edid_txt: the base-64 encoded EDID of the display
        @flags: If 1, the @edid_txt is matched as a unique prefex of the EDID.
        @status: A libddcutil display status.
        @message: Text message for display status.

        Retrieve the libddcutil display state.

        Depending on the hardware and drivers, this method might return anything
        useful.

        For libddcutil prior to 2.1, the method will return a libddcutil
        @error_status of DDCRC_UNIMPLEMENTED.

    -->
    <method name='GetDisplayState'>
        <arg name='display_number' type='i' direction='in'/>
        <arg name='edid_txt' type='s' direction='in'/>
        <arg name='flags' type='u' direction='in'/>
        <arg name='status' type='i' direction='out'/>
        <arg name='message' type='s' direction='out'/>
    </method>

    <!--
        GetSleepMultiplier:
        @display_number: the libddcutil/ddcutil display number to query
        @edid_txt: the base-64 encoded EDID of the display
        @vcp_code: the VPC-code to query, for example, 16 (0x10) is brightness.
        @flags: If 1, the @edid_txt is matched as a unique prefex of the EDID.
        @current_multiplier: the sleep multiplier.
        @error_status: A libddcutil DDCRC error status.  DDCA_OK (zero) if no errors have occured.
        @error_message: Text message for error_status.

        Get the current libddcutil sleep multiplier for the specified VDU.

        In more recent versions of libddcutil this value is generally managed automatically.
    -->
    <method name='GetSleepMultiplier'>
        <arg name='display_number' type='i' direction='in'/>
        <arg name='edid_txt' type='s' direction='in'/>
        <arg name='flags' type='u' direction='in'/>
        <arg name='current_multiplier' type='d' direction='out'/>
        <arg name='error_status' type='i' direction='out'/>
        <arg name='error_message' type='s' direction='out'/>
    </method>

    <!--
        SetSleepMultiplier:
        @display_number: The libddcutil/ddcutil display number to query
        @edid_txt: The base-64 encoded EDID of the display
        @vcp_code: The VPC-code to query, for example, 16 (0x10) is brightness.
        @flags: If 1, the @edid_txt is matched as a unique prefex of the EDID.
        @new_multiplier: The sleep multiplier.
        @error_status: A libddcutil DDCRC error status.  DDCA_OK (zero) if no errors have occured.
        @error_message: Text message for error_status.

        Set the libddcutil sleep multiplier for the specified VDU.

        In more recent versions of libddcutil this is generally managed automatically,
        but this method is provided should manual control be ncessary (due to problem hardware).

        Prior to taking manual control of the sleep-multiplier, the DdcutilDynamicSleep property
        should be set to false to prevent the multiplier from being automatically retuned.

        If the service is configuration-locked, an com.ddcutil.DdcutilService.Error.ConfigurationLocked
        error is raised.
    -->
    <method name='SetSleepMultiplier'>
        <arg name='display_number' type='i' direction='in'/>
        <arg name='edid_txt' type='s' direction='in'/>
        <arg name='new_multiplier' type='d' direction='in'/>
        <arg name='flags' type='u' direction='in'/>
        <arg name='error_status' type='i' direction='out'/>
        <arg name='error_message' type='s' direction='out'/>
    </method>

    <!--
        ConnectedDisplaysChanged:
        @edid_txt: The base-64 encoded EDID of the display.
        @event_type: A value macthing one of those from the DisplayEventTypes property.
        @flags: For furture use.

        Where hardware and drivers support it, this signal will be raised if a displays
        connection status changes due to cabling, power, or DPMS.

        The hardware, cabling and drivers determines which of states listed by DisplayEventTypes property
        that can actually be signaled (the possibilities cannot be determined programatically).

        Requires the ServiceEmitSignals property to be set to true.
    -->
    <signal name='ConnectedDisplaysChanged'>
        <arg type='s' name='edid_txt'/>
        <arg type='i' name='event_type'/>
        <arg type='u' name='flags'/>
    </signal>


    <!--
        ServiceInitialized:
        @flags: For furture use.

        This signal is raised when the service is initialized.  It provides
        clients with a way to detect restarts and reinstate properties or
        other settings.
    -->
    <signal name='ServiceInitialized'>
        <arg type='u' name='flags'/>
    </signal>

    <!--
        AttributesReturnedByDetect:

        The text names of each of the fields in the array of structs returned by the Detect method.
    -->
    <property type='as' name='AttributesReturnedByDetect' access='read'/>

    <!--
        StatusValues:

        The list of libddcutil status values and their text names that might be returned
        in the @error_status out-parameter of most of the service methods.
    -->
    <property type='a{is}' name='StatusValues' access='read'/>

    <!--
        DdcutilVersion:

        The ddcutil version number for the linked libddcutil.
    -->
    <property type='s' name='DdcutilVersion' access='read'/>

    <!--
        DdcutilVerifySetVcp:

        Within libddcutil each setvcp is verified by internal check using a getvcp.
        This verfication step can be enabled or disabled by altering this property.

        Attempting to set this property when the service is configuration-locked
        will result in an com.ddcutil.DdcutilService.Error.ConfigurationLocked error
        being raised.
    -->
    <property type='b' name='DdcutilVerifySetVcp' access='readwrite'/>

    <!--
        DdcutilDynamicSleep:

        Enables/disables automatic adjustment of the sleep-multiplier. Before
        using the SetSleepMultiplier method, this property should be set to false
        to stop any automatic retuning of the multiplier.

        Attempting to set this property when the service is configuration-locked
        will result in an com.ddcutil.DdcutilService.Error.ConfigurationLocked error
        being raised.
    -->
    <property type='b' name='DdcutilDynamicSleep' access='readwrite'/>

    <!--
        DdcutilOutputLevel:

        Change the libddcutil diagnostic output-level.  See the libddcutil/ddcutil
        documentation for details.

        Attempting to set this property when the service is configuration-locked
        will result in an com.ddcutil.DdcutilService.Error.ConfigurationLocked error
        being raised.
    -->
    <property type='u' name='DdcutilOutputLevel' access='readwrite'/>

    <!--
        DisplayEventTypes:

        A list of the event types sent by the ConnectedDisplaysChanged signal along with
        their text names.  Events  are  included  for  display  connection/disconnection
        (hotplug),  DPMS-sleep,  and DPMS-wake.  If the list is empty, the GPU, GPU-driver,
        or libddcutil version doesn’t support display event detection.
    -->
    <property type='a{is}' name='DisplayEventTypes' access='read'/>

    <!--
        ServiceInterfaceVersion:

        The interface version of this service.  Providing the major number remains
        the same, the service remains backward compatibility with existing clients.
    -->
    <property type='s' name='ServiceInterfaceVersion' access='read'/>

    <!--
        ServiceInfoLogging:

        Enables/disables info and debug level logging within the service executable.
        (The service using glib logging.)

        Attempting to set this property when the service is configuration-locked
        will result in an com.ddcutil.DdcutilService.Error.ConfigurationLocked error
        being raised.
    -->
    <property type='b' name='ServiceInfoLogging' access='readwrite'/>

    <!--
        ServiceEmitSignals:

        Because VDU connectivity change detection involves some polling, this
        property can be used to disable it if it is unecessary.  For example, where
        the configuration of VDUs is fixed.

        Attempting to set this property when the service is configuration-locked
        will result in an com.ddcutil.DdcutilService.Error.ConfigurationLocked error
        being raised.
    -->
    <property type='b' name='ServiceEmitSignals' access='readwrite'/>

    <!--
        ServiceFlagOptions:

        The list of available @flags values that can be passed to the service methods.
        Not all options are applicable to all methods.
    -->
    <property type='a{is}' name='ServiceFlagOptions' access='read'/>

    <!--
        ServiceParametersLocked:
        Indicates whether the lock command line argument has been used to prevent
        configuration changes via method calls and property changes.
    -->
    <property type='b' name='ServiceParametersLocked' access='read'/>
    <!--
        ServicePollInterval:

        Query or set the display change detection poll-interval performed by the service
        (minimum 10 seconds, zero to disable polling).

        If libddcutil supports change detection and it works for hardware, drivers and cabling
        in use, internal polling by the service may be unecessary, in which case polling can be
        turned off by setting the interval to zero.

        Attempting to set this property when the service is configuration-locked
        will result in an com.ddcutil.DdcutilService.Error.ConfigurationLocked error
        being raised.
    -->
    <property type='u' name='ServicePollInterval' access='readwrite'/>

    <!--
        ServicePollCascadeInterval:

        Query  or  set  the display change detection poll-cascade-interval (minimum 0.1 seconds).
        When dealing with a cascade of events, for example, when several VDUs are set to DPMS sleep,
        polling occurs more frequently until the cascade is cleared.

        Attempting to set this property when the service is configuration-locked
        will result in an com.ddcutil.DdcutilService.Error.ConfigurationLocked error
        being raised.
    -->
    <property type='d' name='ServicePollCascadeInterval' access='readwrite'/>

  </interface>
</node>
)";

/* ----------------------------------------------------------------------------------------------------
 * GLib error message definitions.
 */

typedef enum
{
    DDCUTIL_SERVICE_CONFIGURATION_LOCKED,
    DDCUTIL_SERVICE_INVALID_POLL_SECONDS,
    DDCUTIL_SERVICE_INVALID_POLL_CASCADE_SECONDS,
    DDCUTIL_SERVICE_I2C_DEV_NO_MODULE,
    DDCUTIL_SERVICE_I2C_DEV_NO_PERMISSIONS,
    DDCUTIL_SERVICE_N_ERRORS  // Dummy placeholder for counting the number of entries
} DdcutilServiceError;

static const GDBusErrorEntry ddcutil_service_error_entries[] =
{
    { DDCUTIL_SERVICE_CONFIGURATION_LOCKED, "com.ddcutil.DdcutilService.Error.ConfigurationLocked" },
    { DDCUTIL_SERVICE_INVALID_POLL_SECONDS, "com.ddcutil.DdcutilService.Error.InvalidPollSeconds" },
    { DDCUTIL_SERVICE_INVALID_POLL_CASCADE_SECONDS, "com.ddcutil.DdcutilService.Error.InvalidPollCascadeSeconds" },
    { DDCUTIL_SERVICE_I2C_DEV_NO_MODULE, "com.ddcutil.DdcutilService.Error.I2cDevNoModule" },
    { DDCUTIL_SERVICE_I2C_DEV_NO_PERMISSIONS, "com.ddcutil.DdcutilService.Error.I2cDevNoPermissions" },
};

G_STATIC_ASSERT(G_N_ELEMENTS(ddcutil_service_error_entries) == DDCUTIL_SERVICE_N_ERRORS);  // Boilerplate

static GQuark service_error_quark;

static void init_service_error_quark(void)
{
    static gsize quark = 0;
    g_dbus_error_register_error_domain("ddcutil_service_error_quark",
                                       &quark,
                                       ddcutil_service_error_entries,
                                       G_N_ELEMENTS (ddcutil_service_error_entries));
    service_error_quark = quark;
}

/* ----------------------------------------------------------------------------------------------------
 */

/**
 * @brief Encode the EDID for easy/efficient unmarshalling on clients.
 * @param edid binary EDID
 * @return a relatively compact character string encoded edid
 */
static char* edid_encode(const uint8_t* edid) {
    return g_base64_encode(edid, 128); // Shorter than hex but not too much like line noise.
}

static char* server_executable = PROGRAM_NAME;

/**
 * \brief Duplicates the binary serial number extraction done by ddcutil
 * \param edid_bytes binary edid bytes
 * \return binary serial number
 */
static uint32_t edid_to_binary_serial_number(const uint8_t* edid_bytes) {
    const uint32_t binary_serial =
            edid_bytes[0x0c] |
            edid_bytes[0x0d] << 8 |
            edid_bytes[0x0e] << 16 |
            edid_bytes[0x0f] << 24;
    return binary_serial;
}

/**
 * @brief Create a new string with invalid utf-8 edited out (replaced with ?)
 * @param text suspect text
 * @return g_malloced text with invalid utf-8 edited out
 */
static gchar* sanitize_utf8(const char* text) {
    gchar* result = g_strdup(text);
    const char *ptr = result, *end = ptr + strlen(result);
    while (true) {
        const char* ptr2;
        g_utf8_validate(ptr, end - ptr, &ptr2);
        if (ptr2 == end)
            break;
        result[ptr2 - result] = '?'; // Sanitize invalid utf-8
        ptr = ptr2 + 1;
    }
    return result;
}

/**
 * @brief Enable logging of info and debug level messages for this service.
 * @param enable whether to log or not
 * @param overwrite whether to overwrite any existing setting
 * @return the new enabled state
 */
static bool enable_service_info_logging(bool enable, bool overwrite) {
    if (enable) {
        g_setenv("G_MESSAGES_DEBUG", G_LOG_DOMAIN, overwrite); // enable info+debug messages for our domain.
        return TRUE;
    }
    g_unsetenv("G_MESSAGES_DEBUG"); // disable info+debug messages for our domain.
    return FALSE;
}

/**
 * @brief Return whether the service is set to log info and debug messages.
 * @return enabled state
 */
static bool is_service_info_logging() {
    const char* value = g_getenv("G_MESSAGES_DEBUG");
    return value != NULL && strstr(value, G_LOG_DOMAIN) != NULL;
}

/**
 * @brief Obtain a text message for a DDCA status.
 * @param status the DDCA status
 * @return a g_malloced message
 */
static char* get_status_message(const DDCA_Status status) {
    const char* status_text = ddca_rc_name(status);
    char* message_text = NULL;
    if (status != 0) {
        DDCA_Error_Detail* error_detail = ddca_get_error_detail();
        if (error_detail != NULL && error_detail->detail != NULL) {
            message_text = g_strdup_printf("%s: %s", status_text, error_detail->detail);
        }
        free(error_detail);
    }
    if (message_text == NULL) {
        message_text = g_strdup(status_text);
    }
    return message_text;
}

/**
 * @brief Lookup DDCA_Display_Info for either a display_number or an encoded EDID.
 *
 * This is a supporting-function, it's used by functions that implement the
 * service's methods.  It does the donky work of looking up a display by
 * number or EDID.
 *
 * @param display_number display number
 * @param edid_encoded text encoded edid
 * @param dlist ddcutil list of displays (will need to be freed after use)
 * @param dinfo pointer into the list for the matched display
 * @param edid_is_prefix match edidby unique prefix
 * @return success status
 */
static DDCA_Status get_display_info(const int display_number, const char* edid_encoded,
                                    DDCA_Display_Info_List** dlist, DDCA_Display_Info** dinfo, bool edid_is_prefix) {
    *dinfo = NULL;
    DDCA_Status status = ddca_get_display_info_list2(0, dlist);

    if (status == DDCRC_OK) {
        for (int ndx = 0; ndx < (*dlist)->ct; ndx++) {
            if (display_number == (*dlist)->info[ndx].dispno) {
                *dinfo = &((*dlist)->info[ndx]);
                break;
            }
            gchar* dlist_edid_encoded = edid_encode((*dlist)->info[ndx].edid_bytes);
            if (edid_encoded != NULL) {
                const bool edit_matched =
                        edid_is_prefix
                            ? (strncmp(edid_encoded, dlist_edid_encoded, strlen(edid_encoded)) == 0)
                            : (strcmp(edid_encoded, dlist_edid_encoded) == 0);
                if (edit_matched) {
                    *dinfo = &((*dlist)->info[ndx]);
                    g_free(dlist_edid_encoded);
                    break;
                }
            }
            g_free(dlist_edid_encoded);
        }
        if (*dinfo == NULL) {
            g_warning("Bad display ID %d %-30s?", display_number, edid_encoded);
            status = DDCRC_INVALID_DISPLAY;
        }
    }
    return status;
}

extern char** environ;

/**
 * @brief Implement the DdcutilService Restart method
 *
 * Restarts the service. This function must terminate the current
 * process and fork a new one so that dca_init() can then be called
 * to apply libopts, syslog_level and opts.
 *
 * @param parameters containing the new libopts, syslog_level and opts (suu)
 * @param invocation the originating D-Bus call - returning a value to it before terminating the current process.
 */
static void restart(GVariant* parameters, GDBusMethodInvocation* invocation) {
    if (lock_configuration) {
        g_dbus_method_invocation_return_error(invocation, service_error_quark, DDCUTIL_SERVICE_CONFIGURATION_LOCKED,
            "Failed to restart the service: configuration locked by --lock command line argument.");
        return;
    }
    char* libopts;
    u_int32_t syslog_level;
    u_int32_t opts;
    g_variant_get(parameters, "(suu)", &libopts, &syslog_level, &opts);
    char* message_text = get_status_message(DDCRC_OK);
    g_message("DdcaInit syslog_level=%x opts=%x libopts=%s", syslog_level, opts, libopts);
    GVariant* result = g_variant_new("(is)", DDCRC_OK, message_text);
    g_dbus_method_invocation_return_value(invocation, result);

    gchar** argv;
#if defined(LIBDDCUTIL_HAS_OPTION_ARGUMENTS)
    gchar* args_str = "";
    if (syslog_level != 0) {
        args_str = g_strdup_printf("%s --ddca-syslog-level=%d", args_str, syslog_level);
    }
    if (opts != 0) {
        args_str = g_strdup_printf("%s --ddca-init-options=%d", args_str, opts);
    }
    args_str = g_strdup_printf("%s -- %s", args_str, libopts);
    argv = g_strsplit(args_str, " ", -1);
#else
  gchar *no_args = { NULL };
  argv = &no_args;
#endif

    // If running under dbus-daemon a respawn is not necessary except that
    // we want to pass arguments.
    const struct sigaction arg = {
        .sa_handler = SIG_IGN,
        .sa_flags = SA_NOCLDWAIT // Never wait for termination of a child process.
    };
    sigaction(SIGCHLD, &arg, NULL);

    pid_t spawn_pid;
    posix_spawn(&spawn_pid, server_executable, NULL, NULL, argv, environ);
    exit(0);
}

#if defined(LIBDDCUTIL_HAS_CHANGES_CALLBACK)
static void display_status_event_callback(DDCA_Display_Status_Event event);
#endif


/**
 * @brief Implements the DdcutilService Detect method
 *
 * Passes a list of display structs back to the invocation.
 *
 * @param parameters inbound parameters
 * @param invocation originating D-Bus method call
 */
static void detect(GVariant* parameters, GDBusMethodInvocation* invocation) {
    u_int32_t flags;
    g_variant_get(parameters, "(u)", &flags);

    g_info("Detect flags=%x", flags);

    const DDCA_Status detect_status = ddca_redetect_displays();
    char* detect_message_text = get_status_message(detect_status);

    DDCA_Display_Info_List* dlist = NULL;
    const DDCA_Status list_status = ddca_get_display_info_list2(flags != 0, &dlist);
    char* list_message_text = get_status_message(list_status);
    int vdu_count = 0;

    // see https://docs.gtk.org/glib/struct.VariantBuilder.html

    GVariantBuilder detected_displays_builder_instance; // Allocate on the stack for easier memory management.
    GVariantBuilder* detected_displays_builder = &detected_displays_builder_instance;

    g_variant_builder_init(detected_displays_builder, G_VARIANT_TYPE("a(iiisssqsu)"));

    if (list_status != DDCRC_OK) {
        g_warning("Detect failed to obtain VDU list - list-status=%d message=%s", list_status, list_message_text);
    }
    else {
        vdu_count = dlist->ct;
        for (int ndx = 0; ndx < vdu_count; ndx++) {
            const DDCA_Display_Info* vdu_info = &dlist->info[ndx];
            gchar* safe_mfg_id = sanitize_utf8(vdu_info->mfg_id);
            gchar* safe_model = sanitize_utf8(vdu_info->model_name); //"xxxxwww\xF0\xA4\xADiii" );
            gchar* safe_sn = sanitize_utf8(vdu_info->sn);
            gchar* edid_encoded = edid_encode(vdu_info->edid_bytes);
            g_info("Detected %s %s %s display_num=%d edid=%.30s...",
                   safe_mfg_id, safe_model, safe_sn, vdu_info->dispno, edid_encoded);
            g_variant_builder_add(
                detected_displays_builder,
                "(iiisssqsu)",
                vdu_info->dispno, vdu_info->usb_bus, vdu_info->usb_device,
                safe_mfg_id, safe_model, safe_sn,
                vdu_info->product_code,
                edid_encoded,
                edid_to_binary_serial_number(vdu_info->edid_bytes));
            g_free(safe_mfg_id);
            g_free(safe_model);
            g_free(safe_sn);
            g_free(edid_encoded);
        }
        ddca_free_display_info_list(dlist);
    }

    const int final_status = (detect_status != 0) ? detect_status : list_status;
    const char* final_message_text = (detect_status != 0) ? detect_message_text : list_message_text;

    GVariant* result = g_variant_new("(ia(iiisssqsu)is)",
                                     vdu_count, detected_displays_builder, final_status, final_message_text);

    g_dbus_method_invocation_return_value(invocation, result); // Think this frees the result.
    free(list_message_text);
    free(detect_message_text);
}

/**
 * @brief Implements the DdcutilService GetVcp method
 *
 * Passes a single VCP value to the invocation.
 *
 * @param parameters inbound parameters
 * @param invocation originating D-Bus method call
 */
static void get_vcp(GVariant* parameters, GDBusMethodInvocation* invocation) {
    int display_number;
    char* edid_encoded;
    uint8_t vcp_code;
    u_int32_t flags;

    g_variant_get(parameters, "(isyu)", &display_number, &edid_encoded, &vcp_code, &flags);

    g_info("GetVcp vcp_code=%d display_num=%d, edid=%.30s...", vcp_code, display_number, edid_encoded);

    uint16_t current_value = 0;
    uint16_t max_value = 0;
    char* formatted_value = NULL;

    DDCA_Display_Info_List* info_list = NULL;
    DDCA_Display_Info* vdu_info = NULL; // pointer into info_list
    DDCA_Status status = get_display_info(display_number, edid_encoded, &info_list, &vdu_info, flags & EDID_PREFIX);
    if (status == DDCRC_OK) {
        DDCA_Display_Handle disp_handle;
        status = ddca_open_display2(vdu_info->dref, 1, &disp_handle);
        if (status == DDCRC_OK) {
            static DDCA_Non_Table_Vcp_Value valrec;
            status = ddca_get_non_table_vcp_value(disp_handle, vcp_code, &valrec);
            if (status == DDCRC_OK) {
                current_value = valrec.sh << 8 | valrec.sl;
                max_value = valrec.mh << 8 | valrec.ml;
                status = ddca_format_non_table_vcp_value_by_dref(vcp_code, vdu_info->dref, &valrec, &formatted_value);
            }
            ddca_close_display(disp_handle);
        }
    }
    char* message_text = get_status_message(status);
    GVariant* result = g_variant_new(
        "(qqsis)", current_value, max_value, formatted_value ? formatted_value : "", status, message_text);
    g_dbus_method_invocation_return_value(invocation, result); // Think this frees the result
    ddca_free_display_info_list(info_list);
    if (formatted_value != NULL) {
        free(formatted_value);
    }
    free(edid_encoded);
    free(message_text);
}

/**
 * @brief Implements the DdcutilService GetMultipleVcp method
 *
 * Passes back an array of VCP values to the invocation.
 *
 * @param parameters inbound parameters
 * @param invocation originating D-Bus method call
 */
static void get_multiple_vcp(GVariant* parameters, GDBusMethodInvocation* invocation) {
    int display_number;
    char* edid_encoded;
    u_int32_t flags;

    GVariantIter* vcp_code_iter;
    g_variant_get(parameters, "(isayu)", &display_number, &edid_encoded, &vcp_code_iter, &flags);

    g_info("GetMultipleVcp display_num=%d, edid=%.30s...", display_number, edid_encoded);

    const int number_of_vcp_codes = g_variant_iter_n_children(vcp_code_iter);
    const u_int8_t vcp_codes[number_of_vcp_codes];
    for (int i = 0; g_variant_iter_loop(vcp_code_iter, "y", &vcp_codes[i]); i++) {
    }
    g_variant_iter_free(vcp_code_iter);

    GVariantBuilder value_array_builder_instance;
    GVariantBuilder* value_array_builder = &value_array_builder_instance;
    g_variant_builder_init(value_array_builder, G_VARIANT_TYPE("a(yqqs)"));

    DDCA_Display_Info_List* info_list = NULL;
    DDCA_Display_Info* vdu_info = NULL; // pointer into info_list
    DDCA_Status status = get_display_info(display_number, edid_encoded, &info_list, &vdu_info, flags & EDID_PREFIX);
    if (status == DDCRC_OK) {
        DDCA_Display_Handle disp_handle;
        status = ddca_open_display2(vdu_info->dref, 1, &disp_handle);
        if (status == DDCRC_OK) {
            for (int i = 0; i < number_of_vcp_codes; i++) {
                const u_int8_t vcp_code = vcp_codes[i];
                static DDCA_Non_Table_Vcp_Value valrec;
                status = ddca_get_non_table_vcp_value(disp_handle, vcp_code, &valrec);
                if (status == DDCRC_OK) {
                    const uint16_t current_value = valrec.sh << 8 | valrec.sl;
                    const uint16_t max_value = valrec.mh << 8 | valrec.ml;
                    char* formatted_value;
                    status = ddca_format_non_table_vcp_value_by_dref(vcp_code, vdu_info->dref, &valrec,
                                                                     &formatted_value);
                    g_variant_builder_add(value_array_builder, "(yqqs)",
                                          vcp_code, current_value, max_value, formatted_value);
                    free(formatted_value);
                }
                else {
                    // Probably just asleep or turned off
                    g_info("GetMultipleVcp failed for vcp_code=%d display_num=%d edid=%.30s...",
                           vcp_code, display_number, edid_encoded);
                }
            }
            ddca_close_display(disp_handle);
        }
    }
    char* message_text = get_status_message(status);
    GVariant* result = g_variant_new("(a(yqqs)is)", value_array_builder, status, message_text);
    g_dbus_method_invocation_return_value(invocation, result); // Think this frees the result
    ddca_free_display_info_list(info_list);
    g_free(edid_encoded);
    free(message_text);
}

/**
 * @brief Implements the DdcutilService SetVCP method
 * @param parameters inbound parameters
 * @param invocation originating D-Bus method call
 */
static void set_vcp(GVariant* parameters, GDBusMethodInvocation* invocation) {
    int display_number;
    char* edid_encoded;
    uint8_t vcp_code;
    uint16_t new_value;
    u_int32_t flags;

    g_variant_get(parameters, "(isyqu)", &display_number, &edid_encoded, &vcp_code, &new_value, &flags);

    g_info("SetVcp vcp_code=%d value=%d display_num=%d edid=%.30s...", vcp_code, new_value, display_number,
           edid_encoded);

    DDCA_Display_Info_List* info_list = NULL;
    DDCA_Display_Info* vdu_info = NULL; // pointer into info_list
    DDCA_Status status = get_display_info(display_number, edid_encoded, &info_list, &vdu_info, flags & EDID_PREFIX);
    if (status == DDCRC_OK) {
        DDCA_Display_Handle disp_handle;
        status = ddca_open_display2(vdu_info->dref, 1, &disp_handle);
        if (status == DDCRC_OK) {
            const uint8_t low_byte = new_value & 0x00ff;
            const uint8_t high_byte = new_value >> 8;
            status = ddca_set_non_table_vcp_value(disp_handle, vcp_code, high_byte, low_byte);
            ddca_close_display(disp_handle);
        }
    }
    if (status != 0) {
        // Probably just asleep or turned off
        g_info("SetVcp failed for vcp_code=%d value=%d display_num=%d edid=%.30s...",
               vcp_code, new_value, display_number, edid_encoded);
    }
    char* message_text = get_status_message(status);
    GVariant* result = g_variant_new("(is)", status, message_text);
    g_dbus_method_invocation_return_value(invocation, result); // Think this frees the result
    ddca_free_display_info_list(info_list);
    g_free(edid_encoded);
    free(message_text);
}

/**
 * @brief Implements the DdcutilService GetCapabilitesString method
 *
 * Returns the raw capabilties string to the invocation.
 *
 * @param parameters inbound parameters
 * @param invocation originating D-Bus method call
 */
static void get_capabilities_string(GVariant* parameters, GDBusMethodInvocation* invocation) {
    int display_number;
    char* edid_encoded;
    char* caps_text = NULL;
    u_int32_t flags;

    g_variant_get(parameters, "(isu)", &display_number, &edid_encoded, &flags);

    g_info("GetCapabilitiesString display_num=%d, edid=%.30s...", display_number, edid_encoded);

    DDCA_Display_Info_List* info_list = NULL;
    DDCA_Display_Info* vdu_info = NULL; // pointer into info_list
    DDCA_Display_Handle disp_handle;
    DDCA_Status status = get_display_info(display_number, edid_encoded, &info_list, &vdu_info, flags & EDID_PREFIX);

    if (status == DDCRC_OK) {
        status = ddca_open_display2(vdu_info->dref, 1, &disp_handle);
        if (status == DDCRC_OK) {
            status = ddca_get_capabilities_string(disp_handle, &caps_text);
            ddca_close_display(disp_handle);
        }
    }
    char* message_text = get_status_message(status);
    GVariant* result = g_variant_new("(sis)",
                                     caps_text == NULL ? "" : caps_text,
                                     status, message_text);
    g_dbus_method_invocation_return_value(invocation, result); // Think this frees the result
    ddca_free_display_info_list(info_list);
    free(caps_text);
    g_free(edid_encoded);
    free(message_text);
}

/**
 * @brief Implements the DdcutilService GetCapabilitiesMetadata method
 *
 * Passes back a structure of parsed features to the invocation.
 *
 * @param parameters inbound parameters
 * @param invocation originating D-Bus method call
 */
static void get_capabilities_metadata(GVariant* parameters, GDBusMethodInvocation* invocation) {
    int display_number;
    char* edid_encoded;
    char* caps_text = NULL;
    u_int32_t flags;

    g_variant_get(parameters, "(isu)", &display_number, &edid_encoded, &flags);

    g_info("GetCapabilitiesMetadata display_num=%d, edid=%.30s...", display_number, edid_encoded);

    DDCA_Display_Info_List* info_list = NULL;
    DDCA_Display_Info* vdu_info = NULL; // pointer into info_list
    DDCA_Display_Handle disp_handle;
    DDCA_Capabilities* parsed_capabilities_ptr = NULL;
    DDCA_Status status = get_display_info(display_number, edid_encoded, &info_list, &vdu_info, flags & EDID_PREFIX);

    uint8_t mccs_version_major = 0, mccs_version_minor = 0;
    char* vdu_model = "model";

    GVariantBuilder command_dict_builder_instance; // Allocate on the stack for easier memory management.
    GVariantBuilder* command_dict_builder = &command_dict_builder_instance;
    g_variant_builder_init(command_dict_builder, G_VARIANT_TYPE("a{ys}"));

    GVariantBuilder feature_dict_builder_instance; // Allocate on the stack for easier memory management.
    GVariantBuilder* feature_dict_builder = &feature_dict_builder_instance;
    g_variant_builder_init(feature_dict_builder, G_VARIANT_TYPE("a{y(ssa{ys})}"));

    if (status == DDCRC_OK) {
        vdu_model = vdu_info->model_name;
        status = ddca_open_display2(vdu_info->dref, 1, &disp_handle);
        if (status == DDCRC_OK) {
            status = ddca_get_capabilities_string(disp_handle, &caps_text);
            if (status == DDCRC_OK) {
                status = ddca_parse_capabilities_string(caps_text, &parsed_capabilities_ptr);

                if (status == DDCRC_OK) {
                    const DDCA_Cap_Vcp* vcp_feature_array = parsed_capabilities_ptr->vcp_codes;

                    g_debug("vcp_code_ct=%d", parsed_capabilities_ptr->vcp_code_ct);

                    mccs_version_major = parsed_capabilities_ptr->version_spec.major;
                    mccs_version_minor = parsed_capabilities_ptr->version_spec.minor;
                    for (int command_idx = 0; command_idx < parsed_capabilities_ptr->cmd_ct; command_idx++) {
                        char* command_desc = g_strdup_printf("desc of %d",
                                                             parsed_capabilities_ptr->cmd_codes[command_idx]);
                        g_debug("CommandDef %x %s ", parsed_capabilities_ptr->cmd_codes[command_idx], command_desc);
                        g_variant_builder_add(
                            command_dict_builder, "{ys}", parsed_capabilities_ptr->cmd_codes[command_idx],
                            command_desc);
                        g_free(command_desc); // TODO is this OK, or are we freeing too early?
                    }

                    for (int feature_idx = 0; feature_idx < parsed_capabilities_ptr->vcp_code_ct; feature_idx++) {
                        const DDCA_Cap_Vcp* feature_def = vcp_feature_array + feature_idx;
                        DDCA_Feature_Metadata* metadata_ptr;

                        // TODO valgrind complains
                        status = ddca_get_feature_metadata_by_dh(feature_def->feature_code, disp_handle, true,
                                                                 &metadata_ptr);
                        if (status == DDCRC_OK) {
                            g_debug("FeatureDef: %x %s %s",
                                    metadata_ptr->feature_code, metadata_ptr->feature_name, metadata_ptr->feature_desc);
                            GVariantBuilder value_dict_builder_instance;
                            // Allocate on the stack for easier memory management.
                            GVariantBuilder* value_dict_builder = &value_dict_builder_instance;
                            g_variant_builder_init(value_dict_builder, G_VARIANT_TYPE("a{ys}"));
                            for (int value_idx = 0; value_idx < feature_def->value_ct; value_idx++) {
                                const u_int8_t value_code = feature_def->values[value_idx];
                                char* value_name = "";
                                if (metadata_ptr->sl_values != NULL) {
                                    for (const DDCA_Feature_Value_Entry* fve = metadata_ptr->sl_values;
                                         fve->value_code != 0; fve++) {
                                        if (fve->value_code == value_code) {
                                            g_debug("  ValueDef match feature %x value %d %s",
                                                    feature_def->feature_code, fve->value_code, fve->value_name);
                                            g_variant_builder_add(value_dict_builder, "{ys}",
                                                fve->value_code, fve->value_name);
                                            value_name = fve->value_name;
                                        }
                                    }
                                }
                                g_debug("  ValueDef feature %x value %d %s",
                                        feature_def->feature_code, value_code, value_name);

                                g_variant_builder_add(value_dict_builder, "{ys}", value_code, value_name);
                            }
                            g_variant_builder_add(
                                feature_dict_builder,
                                "{y(ssa{ys})}",
                                metadata_ptr->feature_code,
                                metadata_ptr->feature_name,
                                metadata_ptr->feature_desc == NULL ? "" : metadata_ptr->feature_desc,
                                value_dict_builder);
                            ddca_free_feature_metadata(metadata_ptr);
                        }
                        else {
                            g_warning("%x %s", feature_def->feature_code, get_status_message(status));
                        }
                    }
                }
            }
            ddca_close_display(disp_handle);
        }
    }

    char* message_text = NULL;
    if (parsed_capabilities_ptr != NULL && parsed_capabilities_ptr->msg_ct > 0) {
        char* messages_null_terminated[parsed_capabilities_ptr->msg_ct + 1];
        for (int i = 0; i <= parsed_capabilities_ptr->msg_ct; i++) {
            messages_null_terminated[i] = parsed_capabilities_ptr->messages[i];
        }
        messages_null_terminated[parsed_capabilities_ptr->msg_ct] = NULL;
        message_text = g_strjoinv("; ", messages_null_terminated);
    }
    else {
        message_text = get_status_message(status);
    }

    GVariant* result = g_variant_new("(syya{ys}a{y(ssa{ys})}is)",
                                     vdu_model,
                                     mccs_version_major,
                                     mccs_version_minor,
                                     command_dict_builder,
                                     feature_dict_builder,
                                     status, message_text);
    g_dbus_method_invocation_return_value(invocation, result); // Think this frees the result
    ddca_free_display_info_list(info_list);
    ddca_free_parsed_capabilities(parsed_capabilities_ptr);
    free(caps_text);
    g_free(edid_encoded);
    free(message_text);
}

/**
 * @brief Implements the DdcutilService GetVcpMetadata method
 *
 * Passes back metadata concerning a specific display's VCP code, such as the data-type.
 *
 * @param parameters inbound parameters
 * @param invocation originating D-Bus method call
 */
static void get_vcp_metadata(GVariant* parameters, GDBusMethodInvocation* invocation) {
    int display_number;
    char* edid_encoded;
    uint8_t vcp_code;
    u_int32_t flags;

    g_variant_get(parameters, "(isyu)", &display_number, &edid_encoded, &vcp_code, &flags);
    g_info("GetVcpMetadata display_num=%d, edid=%.30s...vcp_code=%d", display_number, edid_encoded, vcp_code);

    DDCA_Display_Info_List* info_list = NULL;
    DDCA_Display_Info* vdu_info = NULL; // pointer into info_list
    DDCA_Status status = get_display_info(display_number, edid_encoded, &info_list, &vdu_info, flags & EDID_PREFIX);
    char* feature_name = "";
    char* feature_description = "";
    bool is_read_only = false;
    bool is_write_only = false;
    bool is_rw = false;
    bool is_complex = false;
    bool is_continuous = false;
    DDCA_Feature_Metadata* metadata_ptr = NULL;
    if (status == DDCRC_OK) {
        DDCA_Display_Handle disp_handle;
        status = ddca_open_display2(vdu_info->dref, 1, &disp_handle);
        if (status == DDCRC_OK) {
            status = ddca_get_feature_metadata_by_dh(vcp_code, disp_handle, true, &metadata_ptr);
            if (status == DDCRC_OK) {
                if (metadata_ptr->feature_name != NULL) {
                    feature_name = metadata_ptr->feature_name;
                }
                if (metadata_ptr->feature_desc != NULL) {
                    feature_description = metadata_ptr->feature_desc;
                }
                // if (metadata_ptr->sl_values != NULL) {  // TODO - not used, do we need it?
                //   for (DDCA_Feature_Value_Entry *sl_ptr = metadata_ptr->sl_values; sl_ptr->value_code != 0; sl_ptr++)
                //   {}
                // }
                is_read_only = metadata_ptr->feature_flags & DDCA_RO;
                is_write_only = metadata_ptr->feature_flags & DDCA_WO;
                is_rw = metadata_ptr->feature_flags & DDCA_RW;
                is_complex = metadata_ptr->feature_flags & (DDCA_COMPLEX_CONT | DDCA_COMPLEX_NC);
                is_continuous = metadata_ptr->feature_flags & DDCA_CONT;
                ddca_free_feature_metadata(metadata_ptr);
            }
            ddca_close_display(disp_handle);
        }
    }
    char* message_text = get_status_message(status);
    GVariant* result = g_variant_new("(ssbbbbbis)",
                                     feature_name, feature_description,
                                     is_read_only, is_write_only, is_rw, is_complex, is_continuous,
                                     status, status == DDCRC_OK ? "OK" : message_text);
    g_dbus_method_invocation_return_value(invocation, result); // Think this frees the result
    ddca_free_display_info_list(info_list);
    ddca_free_feature_metadata(metadata_ptr);
    g_free(edid_encoded);
    free(message_text);
}

/**
 * @brief Implements the DdcutilService GetDisplayState method
 *
 * Passes Connection and DPMS state back to the invocation in the status and message.
 *
 * @param parameters inbound parameters
 * @param invocation originating D-Bus method call
 */
static void get_display_state(GVariant* parameters, GDBusMethodInvocation* invocation) {
    int display_number;
    char* edid_encoded;
    u_int32_t flags;

    g_variant_get(parameters, "(isu)", &display_number, &edid_encoded, &flags);

    g_info("GetDisplayState display_num=%d, edid=%.30s...", display_number, edid_encoded);

    DDCA_Display_Info_List* info_list = NULL;
    DDCA_Display_Info* vdu_info = NULL; // pointer into info_list
    DDCA_Status status = get_display_info(display_number, edid_encoded, &info_list, &vdu_info, flags & EDID_PREFIX);
    if (status == DDCRC_OK) {
#if defined(LIBDDCUTIL_HAS_CHANGES_CALLBACK)
        status = ddca_validate_display_ref(vdu_info->dref, TRUE);
#else
    status = DDCRC_UNIMPLEMENTED;
#endif
    }
    char* message_text = get_status_message(status);
    GVariant* result = g_variant_new("(is)", status, message_text);
    g_dbus_method_invocation_return_value(invocation, result); // Think this frees the result
    ddca_free_display_info_list(info_list);
    free(edid_encoded);
    free(message_text);
}


/**
 * @brief Implements the DdcutilService GetSleepMultiplier method
 *
 * Passes back a specific display's sleep multiplier.
 *
 * @param parameters inbound parameters
 * @param invocation originating D-Bus method call
 */
static void get_sleep_multiplier(GVariant* parameters, GDBusMethodInvocation* invocation) {
    int display_number;
    char* edid_encoded;
    u_int32_t flags;
    DDCA_Status status = 0;

    g_variant_get(parameters, "(isu)", &display_number, &edid_encoded, &flags);

    g_info("GetSleepMultiplier display_num=%d, edid=%.30s...", display_number, edid_encoded);

    double multiplier;
#if defined(LIBDDCUTIL_HAS_DDCA_GET_SLEEP_MULTIPLIER)
    multiplier = ddca_get_sleep_multiplier();
#elif defined(LIBDDCUTIL_HAS_DDCA_GET_DEFAULT_SLEEP_MULTIPLIER)
    multiplier = ddca_get_default_sleep_multiplier();
#elif defined(LIBDDCUTIL_HAS_INDIVIDUAL_SLEEP_MULTIPLIER)
    DDCA_Display_Info_List* info_list = NULL;
    DDCA_Display_Info* vdu_info = NULL; // pointer into info_list
    status = get_display_info(display_number, edid_encoded, &info_list, &vdu_info, flags & EDID_PREFIX);
    if (status == DDCRC_OK) {
        status = ddca_get_current_display_sleep_multiplier(vdu_info->dref, &multiplier);
    }
    ddca_free_display_info_list(info_list);
#endif
    char* message_text = get_status_message(status);
    GVariant* result = g_variant_new("(dis)", multiplier, status, message_text);
    g_dbus_method_invocation_return_value(invocation, result); // Think this frees the result
    free(edid_encoded);
    free(message_text);
}

/**
 * @brief Implements the DdcutilService SetSleepMultiplier method
 *
 * Sets a specific display's sleep multiplier.
 *
 * @param parameters inbound parameters
 * @param invocation originating D-Bus method call
 */
static void set_sleep_multiplier(GVariant* parameters, GDBusMethodInvocation* invocation) {
    int display_number;
    char* edid_encoded;
    u_int32_t flags;
    double new_multiplier;
    DDCA_Status status = 0;

    if(lock_configuration) {
        g_dbus_method_invocation_return_error(invocation, service_error_quark, DDCUTIL_SERVICE_CONFIGURATION_LOCKED,
            "Failed to set_sleep_multiplier: configuration locked by --lock command line argument.");
        return;
    }

    g_variant_get(parameters, "(isdu)", &display_number, &edid_encoded, &new_multiplier, &flags);

    g_info("SetSleepMultiplier value=%f display_num=%d edid=%.30s...",
           new_multiplier, display_number, edid_encoded);

#if defined(LIBDDCUTIL_HAS_DDCA_GET_SLEEP_MULTIPLIER)
    ddca_set_sleep_multiplier(new_multiplier);
#elif defined(LIBDDCUTIL_HAS_DDCA_GET_DEFAULT_SLEEP_MULTIPLIER)
    ddca_set_default_sleep_multiplier(new_multiplier);
#elif defined(LIBDDCUTIL_HAS_INDIVIDUAL_SLEEP_MULTIPLIER)
    DDCA_Display_Info_List* info_list = NULL;
    DDCA_Display_Info* vdu_info = NULL; // pointer into info_list
    status = get_display_info(display_number, edid_encoded, &info_list, &vdu_info, flags & EDID_PREFIX);
    if (status == DDCRC_OK) {
        status = ddca_set_display_sleep_multiplier(vdu_info->dref, new_multiplier);
    }
    ddca_free_display_info_list(info_list);
#endif
    char* message_text = get_status_message(status);
    GVariant* result = g_variant_new("(is)", status, message_text);
    g_dbus_method_invocation_return_value(invocation, result); // Think this frees the result
    g_free(edid_encoded);
    free(message_text);
}

/**
 * \brief Verify that the kernel module appears to be loaded and devices are accessable.
 *
 * Sets the global service_broken_error to the g_dbus error code, or -1 if no errors.
 *
 * \return true if everything is OK
 */
static bool verify_i2c_dev() {
#if defined(VERIFY_I2C)
    g_message("Verifying libddcutil and i2c-dev dependencies (i2c-dev kernel module and device permissions)...");

    service_broken_error = -1;  // Assume OK

    // First just check if detect is finding anything - if it is, i2c-dev must be OK
    const DDCA_Status detect_status = ddca_redetect_displays(); // Do not call too frequently, delays the main-loop
    if (detect_status == DDCRC_OK) {
        DDCA_Display_Info_List* dlist = NULL;
        const DDCA_Status list_status = ddca_get_display_info_list2(1, &dlist);
        if (list_status == DDCRC_OK) {
            const int vdu_count = dlist->ct;
            ddca_free_display_info_list(dlist);
            if (vdu_count > 0) {
                g_message("Detected VDU-count=%d - skipping i2c-dev verification", vdu_count);
                return TRUE;
            }
            // May or may not be a problem with i2c-dev - might be normal
            g_message("Failed to detect any VDUs - will check i2c-dev");
        }
    }

    // Check if i2c-dev devices exist and are r/w accessible
    int rw_count = 0;
    glob_t matches;
    if (glob("/dev/i2c-*", 0, NULL, &matches) == 0) {
        const unsigned long dev_count = matches.gl_pathc;
        for (unsigned long i = 0; i < dev_count && rw_count <= 1000; i++) {  // Boilerplate limit of 1000
            if (access(matches.gl_pathv[i], R_OK|W_OK) == F_OK) {
                g_debug("Device %s is R/W accessible", matches.gl_pathv[i]);
                rw_count++;
            }
            else {
                g_debug("Device %s is not R/W accessible", matches.gl_pathv[i]);
            }
        }
        if (rw_count > 0) {
            g_message("Found %d i2c-dev devices that are R/W accessible - good!", rw_count);
        }
        else {
            g_warning("Found %lu i2c-dev devices, but none are accessible: missing permissions (udev-rule/group)?",
                dev_count);
            service_broken_error = DDCUTIL_SERVICE_I2C_DEV_NO_PERMISSIONS;
        }
    }
    if (matches.gl_pathc == 0) {
        g_warning("No devices matching /dev/i2c-* in /dev: is the i2c-dev kernel module loaded?");
        service_broken_error = DDCUTIL_SERVICE_I2C_DEV_NO_MODULE;
    }
    globfree(&matches);
    return rw_count > 0;
#else
    return TRUE;
#endif
}

/**
 * @brief Handles DdcutilService D-Bus method-calls by passing them to implementating functions.
 *
 * This handler is registered with glib's D-Bus main loop to handle DdcutilService
 * method calls.
 *
 * The handler only acts for the DdcutilService DdcutilInterface, so the only
 * parameter of much interest is the method_name.
 *
 * @param connection
 * @param sender
 * @param object_path
 * @param interface_name
 * @param method_name the text name used for vectoring
 * @param parameters
 * @param invocation
 * @param user_data
 */
static void handle_method_call(GDBusConnection* connection, const gchar* sender, const gchar* object_path,
                               const gchar* interface_name, const gchar* method_name, GVariant* parameters,
                               GDBusMethodInvocation* invocation, gpointer user_data) {

    if (service_broken_error != -1) {
        g_message("Service currently broken, checking again...");
        verify_i2c_dev();  // See if things are now fixed
        if (service_broken_error != -1) { // Still broken
            const char *message;
            switch (service_broken_error) {
                case DDCUTIL_SERVICE_I2C_DEV_NO_MODULE:
                    message = "The i2c-dev kernel module does not appear to be loaded.";
                break;
                case DDCUTIL_SERVICE_I2C_DEV_NO_PERMISSIONS:
                    message = "The i2c-dev devices /dev/i2c-* are not R/W accessible "
                              "(possible missing udev-rule or group membership).";
                break;
                default:
                    message = ddcutil_service_error_entries[service_broken_error].dbus_error_name;
                break;
            }
            g_dbus_method_invocation_return_error(invocation, service_error_quark, service_broken_error,
                "%s", message);
            return;
        }
    }

    if (g_strcmp0(method_name, "Detect") == 0) {
        detect(parameters, invocation);
    }
    else if (g_strcmp0(method_name, "GetVcp") == 0) {
        get_vcp(parameters, invocation);
    }
    else if (g_strcmp0(method_name, "GetMultipleVcp") == 0) {
        get_multiple_vcp(parameters, invocation);
    }
    else if (g_strcmp0(method_name, "SetVcp") == 0) {
        set_vcp(parameters, invocation);
    }
    else if (g_strcmp0(method_name, "GetDisplayState") == 0) {
        get_display_state(parameters, invocation);
    }
    else if (g_strcmp0(method_name, "GetVcpMetadata") == 0) {
        get_vcp_metadata(parameters, invocation);
    }
    else if (g_strcmp0(method_name, "GetSleepMultiplier") == 0) {
        get_sleep_multiplier(parameters, invocation);
    }
    else if (g_strcmp0(method_name, "SetSleepMultiplier") == 0) {
        set_sleep_multiplier(parameters, invocation);
    }
    else if (g_strcmp0(method_name, "GetCapabilitiesString") == 0) {
        get_capabilities_string(parameters, invocation);
    }
    else if (g_strcmp0(method_name, "GetCapabilitiesMetadata") == 0) {
        get_capabilities_metadata(parameters, invocation);
    }
    else if (g_strcmp0(method_name, "Restart") == 0) {
        restart(parameters, invocation);
    }
}

/**
 * @brief Handles calls to DdcutilService D-Bus org.freedesktop.DBus.Properties.Get.
 *
 * This handler is registered with glib's D-Bus main loop to handle DdcutilService
 * get-property calls.
 *
 * The handler only acts for the DdcutilService DdcutilInterface, so the only
 * parameter of much interest is the property_name.
 *
 * \brief
 * @param connection
 * @param sender
 * @param object_path
 * @param interface_name
 * @param property_name property name to obtain the value for
 * @param error
 * @param user_data
 * @return value of the property
 */
static GVariant* handle_get_property(GDBusConnection* connection, const gchar* sender, const gchar* object_path,
                                     const gchar* interface_name, const gchar* property_name, GError** error,
                                     gpointer user_data) {
    GVariant* ret = NULL;
    if (g_strcmp0(property_name, "DdcutilVersion") == 0) {
        ret = g_variant_new_string(ddca_ddcutil_extended_version_string());
    }
    else if (g_strcmp0(property_name, "ServiceInterfaceVersion") == 0) {
        ret = g_variant_new_string(DDCUTIL_DBUS_INTERFACE_VERSION_STRING);
    }
    else if (g_strcmp0(property_name, "DdcutilVerifySetVcp") == 0) {
        ret = g_variant_new_boolean(ddca_is_verify_enabled());
    }
    else if (g_strcmp0(property_name, "DdcutilDynamicSleep") == 0) {
#if defined(HAS_DYNAMIC_SLEEP)
        if (strcmp(ddca_ddcutil_version_string(), "2.0.0") != 0) {
            ret = g_variant_new_boolean(ddca_is_dynamic_sleep_enabled());
        }
#else
        ret = g_variant_new_boolean(FALSE);
#endif
    }
    else if (g_strcmp0(property_name, "AttributesReturnedByDetect") == 0) {
        GVariantBuilder* builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
        for (int i = 0; attributes_returned_from_detect[i] != NULL; i++) {
            g_variant_builder_add(builder, "s", attributes_returned_from_detect[i]);
        }
        GVariant* value = g_variant_new("as", builder);
        g_variant_builder_unref(builder);
        ret = value;
    }
    else if (g_strcmp0(property_name, "StatusValues") == 0) {
        GVariantBuilder* builder = g_variant_builder_new(G_VARIANT_TYPE("a{is}"));
        g_variant_builder_add(builder, "{is}", DDCRC_OK, ddca_rc_name(DDCRC_OK));
        for (int i = RCRANGE_DDC_START + 1; ddca_rc_name(-i) != NULL; i++) {
            g_variant_builder_add(builder, "{is}", -i, ddca_rc_name(-i));
        }
        GVariant* value = g_variant_new("a{is}", builder);
        g_variant_builder_unref(builder);
        ret = value;
    }
    else if (g_strcmp0(property_name, "DisplayEventTypes") == 0) {
        GVariantBuilder* builder = g_variant_builder_new(G_VARIANT_TYPE("a{is}"));
        if (display_status_detection_enabled) {
            for (int i = 0; i < sizeof(event_types) / sizeof(int); i++) {
                g_variant_builder_add(builder, "{is}", event_types[i], event_type_names[i]);
            }
        }
        GVariant* value = g_variant_new("a{is}", builder);
        g_variant_builder_unref(builder);
        ret = value;
    }
    else if (g_strcmp0(property_name, "DdcutilOutputLevel") == 0) {
        ret = g_variant_new_uint32(ddca_get_output_level());
    }
    else if (g_strcmp0(property_name, "ServiceInfoLogging") == 0) {
        ret = g_variant_new_boolean(is_service_info_logging());
    }
    else if (g_strcmp0(property_name, "ServiceEmitSignals") == 0) {
        ret = g_variant_new_boolean(enable_signals);
    }
    else if (g_strcmp0(property_name, "ServiceFlagOptions") == 0) {
        GVariantBuilder* builder = g_variant_builder_new(G_VARIANT_TYPE("a{is}"));
        for (int i = 0; i < sizeof(flag_options) / sizeof(int); i++) {
            g_variant_builder_add(builder, "{is}", flag_options[i], flag_options_names[i]);
        }
        GVariant* value = g_variant_new("a{is}", builder);
        g_variant_builder_unref(builder);
        ret = value;
    }
    else if (g_strcmp0(property_name, "ServiceParametersLocked") == 0) {
        ret = g_variant_new_boolean(lock_configuration);
    }
    else if (g_strcmp0(property_name, "ServicePollInterval") == 0) {
        ret = g_variant_new_uint32(poll_interval_micros / 1000000);
    }
    else if (g_strcmp0(property_name, "ServicePollCascadeInterval") == 0) {
        ret = g_variant_new_double(poll_cascade_interval_micros / 1000000.0);
    }
    return ret;
}

/**
 * @brief Handles calls to DdcutilService D-Bus org.freedesktop.DBus.Properties.Set.
 *
 * This handler is registered with glib's D-Bus main-loop to handle DdcutilService
 * set-property calls.
 *
 * The handler only acts for the DdcutilService DdcutilInterface, so the only
 * parameter of much interest is the property_name.
 *
 * \brief
 * @param connection
 * @param sender
 * @param object_path
 * @param interface_name
 * @param property_name property to set
 * @param value
 * @param error
 * @param user_data
 * @return TRUE if successfully set
 */
static gboolean handle_set_property(GDBusConnection* connection, const gchar* sender, const gchar* object_path,
                                    const gchar* interface_name, const gchar* property_name, GVariant* value,
                                    GError** error, gpointer user_data) {
    if (lock_configuration) {
        g_set_error (error,
             service_error_quark,
             DDCUTIL_SERVICE_CONFIGURATION_LOCKED,
             "Failed to set %s property: configuration locked by --lock command line argument.",
             property_name);
        g_warning("%s", (*error)->message);
        return FALSE;
    }
    if (g_strcmp0(property_name, "DdcutilVerifySetVcp") == 0) {
        ddca_enable_verify(g_variant_get_boolean(value));
    }
    else if (g_strcmp0(property_name, "DdcutilDynamicSleep") == 0) {
#if defined(HAS_DYNAMIC_SLEEP)
        ddca_enable_dynamic_sleep(g_variant_get_boolean(value));
#else
        g_warning("Dynamic sleep not supported by this version of libddcutil");
#endif
    }
    else if (g_strcmp0(property_name, "DdcutilOutputLevel") == 0) {
        ddca_set_output_level(g_variant_get_uint32(value));
        g_message("New output_level=%x", ddca_get_output_level());
    }
    else if (g_strcmp0(property_name, "ServiceInfoLogging") == 0) {
        const bool enabled = enable_service_info_logging(g_variant_get_boolean(value), TRUE);
        g_message("ServiceInfoLogging %s", enabled ? "enabled" : "disabled");
    }
    else if (g_strcmp0(property_name, "ServiceEmitSignals") == 0) {
        enable_signals = g_variant_get_boolean(value);
        g_message("ServiceEmitSignals set property to %s", enable_signals ? "enabled" : "disabled");
        if (prefer_polling) {
            g_message("ServiceEmitSignals using ddcutil-service polling");
        }
        else {
#if defined(LIBDDCUTIL_HAS_CHANGES_CALLBACK)
            if (enable_signals) {
                g_message("ServiceEmitSignals using libddcutil change detection");
                const int status = ddca_start_watch_displays(DDCA_EVENT_CLASS_ALL);
                if (status != DDCRC_OK) {
                    char* message_text = get_status_message(status);
                    g_message("ServiceEmitSignals ddca_start_watch_displays %d %s", status, message_text);
                    free(message_text);
                }
            }
            else {
                const int status = ddca_stop_watch_displays(DDCA_EVENT_CLASS_ALL);
                if (status != DDCRC_OK) {
                    char* message_text = get_status_message(status);
                    g_message("ServiceEmitSignals ddca_stop_watch_displays %d %s", status, message_text);
                    free(message_text);
                }
            }
#endif
        }
    }
    else if (g_strcmp0(property_name, "ServicePollInterval") == 0) {
        const uint secs = g_variant_get_uint32(value);
        if (secs == 0) {
            poll_interval_micros = 0;
            g_message("ServicePollInterval changed to zero, polling is now disabled.");
        }
        else if (secs < MIN_POLL_SECONDS) {
            g_set_error (error,
             service_error_quark,
             DDCUTIL_SERVICE_INVALID_POLL_SECONDS,
             "Invalid polling interval %u, interval must be at least %d seconds", secs, MIN_POLL_SECONDS);
            g_warning("%s", (*error)->message);
            return FALSE;
        }
        else {
            g_message("ServicePollInterval changed to %u seconds", secs);
            poll_interval_micros = secs * 1000000;
        }
    }
    else if (g_strcmp0(property_name, "ServicePollCascadeInterval") == 0) {
        const double secs = g_variant_get_double(value);
        if (secs < MIN_POLL_CASCADE_INTERVAL_SECONDS || secs < poll_interval_micros / 1000000) {
            g_set_error (error,
             service_error_quark,
             DDCUTIL_SERVICE_INVALID_POLL_CASCADE_SECONDS,
             "Invalid poll cascade interval %5.3f, valid range is %5.3f to %5.3f",
             secs, MIN_POLL_CASCADE_INTERVAL_SECONDS, poll_interval_micros / 1000000.0);
            g_warning("%s", (*error)->message);
            return FALSE;
        }
        else {
            g_message("ServicePollCascadeInterval changed to %f seconds", secs);
            poll_cascade_interval_micros = (long) (secs * 1000000);
        }
    }
    return *error == NULL;
}


/*
 * Internal polling implementation of detecting changes.
 *
 * The function poll_for_changes() should be called from the GMainLoop custom
 * event source.  It will set the global signal_event_data when it detects
 * an event (just like the libddcutil implementation).
 *
 * Only detects connect/disconnect events.
 */

static long next_poll_time = 0;

static GList* poll_list = NULL; // List of currently detected edids

typedef struct {
    gchar* edid_encoded;
    gboolean connected;
    gboolean has_dpms;
    gboolean dpms_awake;
} Poll_List_Item;

static gint pollcmp(gconstpointer item_ptr, gconstpointer target) {
    const gchar* target_str = (char *)target;
    const Poll_List_Item* item = (Poll_List_Item *)item_ptr;
    return strcmp(target_str, item->edid_encoded);
}

static bool poll_dpms_awake(const DDCA_Display_Info* vdu_info) {
    DDCA_Display_Handle disp_handle;
    DDCA_Status status = ddca_open_display2(vdu_info->dref, 1, &disp_handle);
    if (status == DDCRC_OK) {
        static DDCA_Non_Table_Vcp_Value valrec;
        status = ddca_get_non_table_vcp_value(disp_handle, 0xd6, &valrec);
        ddca_close_display(disp_handle);
        if (status == DDCRC_OK) {
            const uint16_t current_value = valrec.sh << 8 | valrec.sl;
            // g_debug("Poll check-dpms value=%d %s", current_value, current_value <= 1 ? "awake" : "asleep");
            return current_value <= 1;
        }
    }
    g_debug("Poll check-dpms failed %s - assume asleep", ddca_rc_name(status));
    return FALSE;  // Guessing the VDU has gone into DPMS where it cannot respond.
}

static bool poll_for_changes() {
    const long now_in_micros = g_get_monotonic_time();
    bool event_is_ready = FALSE;
    if (now_in_micros >= next_poll_time) {
        g_debug("Poll for display connection changes");
        const DDCA_Status detect_status = ddca_redetect_displays(); // Do not call too frequently, delays the main-loop
        if (detect_status == DDCRC_OK) {
            DDCA_Display_Info_List* dlist;
            const DDCA_Status info_status = ddca_get_display_info_list2(1, &dlist);
            if (info_status == DDCRC_OK) {

                // Mark all past displays as disconnected.
                for (const GList* ptr = poll_list; ptr != NULL; ptr = ptr->next) {
                    ((Poll_List_Item *)(ptr->data))->connected = FALSE;
                }

                // Check all displays, mark existing ones as connected, add new ones.
                for (int ndx = 0; ndx < dlist->ct && !event_is_ready; ndx++) {
                    const DDCA_Display_Info* vdu_info = &dlist->info[ndx];
                    gchar* edid_encoded = edid_encode(vdu_info->edid_bytes);
                    const GList* ptr = g_list_find_custom(poll_list, edid_encoded, pollcmp);
                    if (ptr != NULL) {  // Found it, mark it as connected
                        Poll_List_Item* vdu_poll_data = (Poll_List_Item *)(ptr->data);
                        const gboolean previous_dpms_awake = vdu_poll_data->dpms_awake;
                        vdu_poll_data->connected = TRUE;
                        g_debug("Poll check: existing-connection disp=%d %.30s...", ndx + 1, edid_encoded);
                        if (vdu_poll_data->has_dpms) {
                            vdu_poll_data->dpms_awake = poll_dpms_awake(vdu_info);
                            if (previous_dpms_awake != vdu_poll_data->dpms_awake) {
                                g_message("Poll signal event - dpms changed to %s %d %.30s...",
                                    vdu_poll_data->dpms_awake ? "awake" : "asleep", ndx + 1, edid_encoded);
                                Event_Data_Type* event = g_malloc(sizeof(Event_Data_Type));
                                event->event_type =
                                    vdu_poll_data->dpms_awake ? DDCA_EVENT_DPMS_AWAKE : DDCA_EVENT_DPMS_ASLEEP;
                                event->dref = vdu_info->dref;
                                g_atomic_pointer_set(&signal_event_data, event);
                                event_is_ready = TRUE; // Only one event on each poll - terminate loop
                            }
                        }
                        g_free(edid_encoded);  // Already in list - no longer needed
                    }
                    else {  // Not in the list, add it
                        Poll_List_Item* vdu_poll_data = g_malloc(sizeof(Poll_List_Item));
                        vdu_poll_data->edid_encoded = edid_encoded;
                        vdu_poll_data->connected = TRUE;
                        vdu_poll_data->has_dpms = FALSE;
                        vdu_poll_data->dpms_awake = TRUE;
                        DDCA_Feature_Metadata *meta_0xd6;
                        if (ddca_get_feature_metadata_by_dref(0xd6, vdu_info->dref, FALSE, &meta_0xd6) == DDCRC_OK) {
                            ddca_free_feature_metadata(meta_0xd6);
                            vdu_poll_data->has_dpms = TRUE;
                            vdu_poll_data->dpms_awake = poll_dpms_awake(vdu_info);
                        }
                        poll_list = g_list_append(poll_list, vdu_poll_data);
                        g_debug("Poll check: new-connection disp=%d %.30s... has_dpms=%d awake=%d ",
                            ndx + 1, edid_encoded, vdu_poll_data->has_dpms, vdu_poll_data->dpms_awake);
                        if (next_poll_time) {  // Not on first time through
                            g_message("Poll signal event - connected %d %.30s...", ndx + 1, edid_encoded);
                            Event_Data_Type* event = g_malloc(sizeof(Event_Data_Type));
                            event->event_type = DDCA_EVENT_DISPLAY_CONNECTED;
                            g_atomic_pointer_set(&signal_event_data, event);
                            event_is_ready = TRUE; // Only one event on each poll - terminate loop
                        }
                    }
                }
                // Check if any displays are still marked as disconnected
                for (GList* ptr = poll_list; ptr != NULL && !event_is_ready;) {
                    GList* next = ptr->next;
                    Poll_List_Item* vdu_poll_data = ptr->data;
                    if (!vdu_poll_data->connected) {
                        g_message("Poll signal event - disconnected %.30s...\n", vdu_poll_data->edid_encoded);
                        Event_Data_Type* event = g_malloc(sizeof(Event_Data_Type));
                        event->event_type = DDCA_EVENT_DISPLAY_DISCONNECTED;
                        g_atomic_pointer_set(&signal_event_data, event);
                        g_free(vdu_poll_data->edid_encoded);
                        g_free(vdu_poll_data);
                        poll_list = g_list_delete_link(poll_list, ptr);
                        event_is_ready = TRUE; // Only one event on each poll - terminate loop
                    }
                    ptr = next;
                }
                ddca_free_display_info_list(dlist);
            }
        }
        next_poll_time = now_in_micros + (event_is_ready ? poll_cascade_interval_micros : poll_interval_micros);
    }
    return event_is_ready;
}

/*
 * Our GDBus service connection - Will be set the the running connection when the bus is aquired.
 */
static GDBusConnection* dbus_connection = NULL;

/*
 * GDBUS service handler table - passed on registraction of the service
 */
static const GDBusInterfaceVTable interface_vtable = {handle_method_call, handle_get_property, handle_set_property};

/*
 * The following code is a implementation of a GMainLoop custom event-source, a GSource.
 * It defines a polled source that handles ddcutil displays-changed data and sends
 * signals to the D-Bus client.
 */

/**
 * \brief GSource custom source structure including custom data (if any),
 * this gets passed around as context.
 */
typedef struct {
    GSource source;
    gchar chg_data[129]; // do we actually have any data - no, maybe later.
} Chg_SignalSource_t;

/**
 * @brief registered with main-loop as a custom prepare event function.
 *
 * The GMainLoop calls this function, the function decides the length of the
 * next timeout_millis and mostly returns FALSE.  If an event happens to be ready the
 * function can return TRUE and the main-loop will act to process it.
 *
 * @param source input source, not of much interest for this implementation
 * @param timeout_millis output parameter setting the timeout for next call
 * @return
 */
static gboolean chg_signal_prepare(GSource* source, gint* timeout_millis) {
    // g_debug("prepare");
    *timeout_millis = 5000; // This doesn't appear to be strict, after an event it doesn't seem to apply for a while???
    if (!enable_signals) {
        return FALSE;
    }

    if (poll_interval_micros > 0) {
        poll_for_changes();
    }

    if (dbus_connection == NULL || g_atomic_pointer_get(&signal_event_data) == NULL) {
        return FALSE;
    }
    g_debug("chg signal_event ready type=%d name=%s", signal_event_data->event_type,
            get_event_type_name(signal_event_data->event_type));
    *timeout_millis = 0; // Not sure we want to do this
    return TRUE;
}

/**
 * @brief registered with GMainLoop as a custom check event function.
 *
 * Called by the GMainLoop on timeout to see if an event is ready.
 *
 * @param source
 * @return
 */
static gboolean chg_signal_check(GSource* source) {
    if (dbus_connection != NULL && g_atomic_pointer_get(&signal_event_data) != NULL) {
        return TRUE;
    }
    return FALSE;
}

/**
 * @brief registered with GMainLoop to dispatch ConnectedDisplaysChanged signals.
 *
 * Called by the GMainLoop if the check function reports that an event
 * is ready.  This function should emit the signal to the D-Bus client.
 *
 * @param source
 * @param callback
 * @param user_data
 * @return
 */
static gboolean chg_signal_dispatch(GSource* source, GSourceFunc callback, gpointer user_data) {
    GError* local_error = NULL;
    if (dbus_connection == NULL) {
        g_warning("chg_signal_dispatch: null D-Bus connection");
        return TRUE;
    }
    Event_Data_Type* event_ptr = g_atomic_pointer_get(&signal_event_data);
    g_info("chg_signal_dispatch: processing event, obtained %s", event_ptr == NULL ? "NULL event data" : "event data");
    if (g_atomic_pointer_compare_and_exchange(&signal_event_data, signal_event_data, NULL)) {
        g_debug("chg_signal_dispatch: cleared event data, ready for next event.");
    }
    else {
        g_warning("chg_signal_dispatch: failed to clear event data, maybe new event data was delivered?");
    }
    gchar* edid_encoded;
    int int_event_type = DDCA_EVENT_DISPLAY_DISCONNECTED;
    if (event_ptr != NULL) {
        int_event_type = event_ptr->event_type;
        switch (event_ptr->event_type) {
            case DDCA_EVENT_DPMS_AWAKE:
            case DDCA_EVENT_DPMS_ASLEEP: ;  // Add semi-colon to resolve OpenSUSE 15.5 compile error
                DDCA_Display_Info* dinfo;
                const DDCA_Status status = ddca_get_display_info(event_ptr->dref, &dinfo);
                if (status == DDCRC_OK) {
                    edid_encoded = edid_encode(dinfo->edid_bytes);
                    ddca_free_display_info(dinfo);
                    break;
                }
            // Fall through
            default:
                edid_encoded = g_strdup("");
                break;
        }
        // TODO Should these be passed in the callback - at least log for now
        // const int io_mode = event_ptr->io_path.io_mode;
        // const int io_path = event_ptr->io_path.path.hiddev_devno;  // Union of ints
        // g_info("chg_signal_dispatch: origin io_mode=%s io_path=%d",
        // (io_mode == DDCA_IO_I2C) ? "I2C" : "USB", io_path);
        g_free(event_ptr);
    }
    else {
        // Not sure if this can ever be reached - maybe if a concurrency issue causes the event_ptr to be NULL.
        edid_encoded = g_strdup("");
        g_warning("chg_signal_dispatch: unexpected null event data, assume DDCA_EVENT_DISCONNECTED");
    }

    if (!g_dbus_connection_emit_signal(dbus_connection,
                                       NULL,
                                       "/com/ddcutil/DdcutilObject",
                                       "com.ddcutil.DdcutilInterface",
                                       "ConnectedDisplaysChanged",
                                       g_variant_new("(siu)", edid_encoded, int_event_type, 0),
                                       &local_error)) {
        g_warning("Signal ConnectedDisplaysChanged: failed %s", local_error != NULL ? local_error->message : "");
        g_free(local_error);
    }
    else {
        g_debug("Signal ConnectedDisplaysChanged: succeeded");
    }
    g_free(edid_encoded);
    return TRUE;
}

/**
 * \brief Defines the three functions that implement our GMainLoop custom event source.
 */
static GSourceFuncs chg_source_funcs = {chg_signal_prepare, chg_signal_check, chg_signal_dispatch};

/**
 * \brief install our custom event source in the GMainLoop
 * \param loop the targeted main loop
 */
static void enable_custom_source(GMainLoop* loop) {
    g_message("Enabling custom g_main_loop event source");
    GMainContext* loop_context = g_main_loop_get_context(loop);
    GSource* source = g_source_new(&chg_source_funcs, sizeof(Chg_SignalSource_t));
    g_source_attach(source, loop_context);
    g_source_unref(source);
    display_status_detection_enabled = TRUE;
}

#if defined(LIBDDCUTIL_HAS_CHANGES_CALLBACK)
/**
 * \brief called by libddcutil when a display status change occurs
 * \param event libddcutil event
 */
static void display_status_event_callback(DDCA_Display_Status_Event event) {
    g_debug("DDCA event triggered display_status_event_callback");
    Event_Data_Type* event_copy = g_malloc(sizeof(Event_Data_Type));
    *event_copy = event;
    g_atomic_pointer_set(&signal_event_data, event_copy);  // Save for processing by our GMainLoop custom source
}
#endif

/**
 * @brief registered callback for when GD-Bus is ready to accept service registrations.
 *
 * When called this function registers the DdcutilService service allong with the
 * interface callback functions that will pass client-requests onto implementing
 * functions.
 *
 * @param connection
 * @param name
 * @param user_data
 */
static void on_bus_acquired(GDBusConnection* connection, const gchar* name, gpointer user_data) {
    const char* object_path = "/com/ddcutil/DdcutilObject";
    dbus_connection = connection;
    const guint registration_id =
            g_dbus_connection_register_object(connection,
                                              object_path,
                                              introspection_data->interfaces[0],
                                              &interface_vtable, NULL, /* user_data */
                                              NULL, /* user_data_free_func */
                                              NULL); /* GError** */
    g_assert(registration_id > 0);
    g_message("Registered %s", object_path);

    // Setup any timers here - if needed.
}

/**
 * @brief registered callback for when the DdcutilService successfully obtains a registration
 *
 * Registration may not succeed if the service is already running.
 *
 * @param connection
 * @param name
 * @param user_data
 */
static void on_name_acquired(GDBusConnection* connection, const gchar* name, gpointer user_data) {
    g_message("Name acquired %s", name);
    GError* error = NULL;
    g_message("Emitting ServiceInitialized signal");
    if (!g_dbus_connection_emit_signal(dbus_connection,
                                       NULL,
                                       "/com/ddcutil/DdcutilObject",
                                       "com.ddcutil.DdcutilInterface",
                                       "ServiceInitialized",
                                       g_variant_new("(u)", 0),
                                       &error)) {
        g_warning("Signal ServiceInitialized: failed %s", error != NULL ? error->message : "");
        g_free(error);
    }
}

/**
 * @brief registered callback for when the DdcutilService cannot be registered.
 *
 * The most likely reason this method will be called is if there is another
 * process already running the service.
 *
 * @param connection
 * @param name
 * @param user_data
 */
static void on_name_lost(GDBusConnection* connection, const gchar* name, gpointer user_data) {
    g_critical("Exiting: lost registration - is another instance already registered?");
    exit(1);
}

/**
 * @brief Setup the service and start the glib main loop.
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, char* argv[]) {
    openlog("ddcutil-service", LOG_PERROR, 0);

    server_executable = argv[0];
    g_message("Running %s (%s)", server_executable, PROGRAM_NAME);
    g_set_prgname(PROGRAM_NAME);

    gboolean version_request = FALSE;     // WARNING gboolean is int sized, do not substitute bool or
    gboolean introspect_request = FALSE;  // g_option_context_parse will overrun
    gboolean log_info = FALSE;            // TODO should all bool be changed to gboolean for safety?
    gboolean enable_display_status_events = FALSE;
#if defined(LIBDDCUTIL_HAS_CHANGES_CALLBACK)
    gboolean prefer_dma = FALSE;
#endif
    int poll_seconds = -1;  // -1 flags no argument supplied
    double poll_cascade_interval_seconds = 0.0;

#if defined(LIBDDCUTIL_HAS_OPTION_ARGUMENTS)
    gint ddca_syslog_level = DDCA_SYSLOG_NOTICE;
    gint ddca_init_options = 0; // DDCA_INIT_OPTIONS_CLIENT_OPENED_SYSLOG
#endif

    // Use the glib command line parser...
    const GOptionEntry entries[] = {
        {
            "version", 'v', 0, G_OPTION_ARG_NONE, &version_request,
            "print ddcutil version, com.ddcutil.DdcUtilInterface version, and exit", NULL
        },
        {
            "introspect", 'x', 0, G_OPTION_ARG_NONE, &introspect_request,
            "print introspection xml and exit", NULL
        },
#if defined(LIBDDCUTIL_HAS_CHANGES_CALLBACK)
        {
            "prefer-polling", 'p', 0, G_OPTION_ARG_NONE, &prefer_polling,
            "prefer polling for detecting display connection events", NULL
        },
        {
            "prefer-dma", 'd', 0, G_OPTION_ARG_NONE, &prefer_dma,
            "prefer libddcutil DMA-lookups for detecting display connection events", NULL
        },
#endif
        {
            "poll-interval", 't', 0, G_OPTION_ARG_INT, &poll_seconds,
            "polling interval in seconds, 10 minimum, 0 to disable polling", NULL
        },
        {
            "poll-cascade-interval", 'c', 0, G_OPTION_ARG_DOUBLE, &poll_cascade_interval_seconds,
            "polling minimum interval between cascading events in seconds, 0.1 minimum", NULL
        },
        {
            "emit-signals", 'e', 0, G_OPTION_ARG_NONE, &enable_display_status_events,
            "enable the D-Bus ConnectedDisplaysChanged signal and associated change monitoring", NULL
        },
        {
            "lock", 'l', 0, G_OPTION_ARG_NONE, &lock_configuration,
            "lock configuration, make properties and sleep-multipliers read only, disable the restart method", NULL
        },
        {
            "log-info", 'i', 0, G_OPTION_ARG_NONE, &log_info,
            "log service info and debug messages", NULL
        },
#if defined(LIBDDCUTIL_HAS_OPTION_ARGUMENTS)
        {
            "ddca-syslog-level", 's', 0, G_OPTION_ARG_INT, &ddca_syslog_level,
            "0=Never|3=Error|6=Warning|9=Notice|12=Info|18=Debug", NULL
        },
        {
            "ddca-init-options", 'i', 0, G_OPTION_ARG_INT, &ddca_init_options,
            "1=Disable-Config-File", NULL
        },
#endif
        {NULL}
    };

    GError* error = NULL;
    GOptionContext* context = g_option_context_new("-- [LIBDDCUTIL-OPTION?]");
    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_print("option parsing failed: %s\n", error->message);
        exit(1);
    }

    init_service_error_quark();

    if (version_request) {
        g_print("ddcutil %s com.ddcutil.DdcUtilInterface %s\n",
                ddca_ddcutil_extended_version_string(), DDCUTIL_DBUS_INTERFACE_VERSION_STRING);
        exit(1);
    }

    if (log_info) {
        enable_service_info_logging(TRUE, FALSE);
    }
    g_message("%s %s, libddcutil %s", PROGRAM_NAME,
              DDCUTIL_DBUS_INTERFACE_VERSION_STRING, ddca_ddcutil_extended_version_string());
    g_message("ServiceInfoLogging %s", is_service_info_logging() ? "enabled" : "disabled");

    if (lock_configuration) {
        g_message("All properties and sleep-multipliers are read only (--lock passed)");
    }

    /* Build introspection data structures from XML.
     */
    introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
    g_assert(introspection_data != NULL);

    if (introspect_request) {
#if defined(XML_FROM_INTROSPECTED_DATA)
        GString *formatted_xml = g_string_new("");
        g_dbus_node_info_generate_xml(introspection_data, 4, formatted_xml); // Create XML from the registered service
#else
        GString* formatted_xml = g_string_new(introspection_xml);
#endif
        g_print("%s\n", formatted_xml->str);
        exit(1);
    }

    verify_i2c_dev();

#if defined(LIBDDCUTIL_HAS_OPTION_ARGUMENTS)
    // Handle ddcutil ddc_init() arguments
    char* argv_null_terminated[argc];
    for (int i = 0; i < argc; i++) {
        argv_null_terminated[i] = argv[i + 2];
    }
    argv_null_terminated[argc - 1] = NULL;
    char* arg_string = g_strjoinv(" ", argv_null_terminated);
    ddca_init_options |= DDCA_INIT_OPTIONS_CLIENT_OPENED_SYSLOG;
    g_message("Calling ddca_init %d %d '%s'", ddca_syslog_level, ddca_init_options, arg_string);
    ddca_init(arg_string, ddca_syslog_level, ddca_init_options);
#endif

    const guint owner_id = g_bus_own_name(
        G_BUS_TYPE_SESSION,
        DDCUTIL_DBUS_DOMAIN,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost, NULL,
        NULL);

    GMainLoop* main_loop = g_main_loop_new(NULL, FALSE);

    if (!enable_display_status_events) {
        g_message("ConnectedDisplaysChanged signals and connectivity monitoring are disabled.");
        poll_interval_micros = 0;  // Disable internal polling
    }
    else {

#if defined(LIBDDCUTIL_HAS_CHANGES_CALLBACK)
        prefer_polling = !prefer_dma;
        if (prefer_polling) {
            g_message("ConnectedDisplaysChanged signal - prefering polling for change detection");
        }
        else {
            const int rstatus = ddca_register_display_status_callback(display_status_event_callback);
            if (rstatus != DDCRC_OK) {
                char* message_text = get_status_message(rstatus);
                g_message("libddcutil ddca_register_display_status_callback failed (status=%d - %s)", rstatus,
                          message_text);
                free(message_text);
                g_warning("libddcutil change detection unavailable for this GPU");
            }
            else {
                const int status = ddca_start_watch_displays(DDCA_EVENT_CLASS_ALL);
                if (status == DDCRC_OK) {
                    g_message("Enabled ConnectedDisplaysChanged signal - using libddcutil change detection");
                    poll_interval_micros = 0;  // Disable internal polling
                }
                else {
                    char* message_text = get_status_message(status);
                    g_message("libddcutil ddca_start_watch_displays failed - non-DRM GPU? (status=%d - %s)", status,
                              message_text);
                    free(message_text);
                    g_warning("libddcutil change detection unavailable for this GPU");
                }
            }
        }
#endif

        if (poll_interval_micros > 0) {  // Means polling hasn't been disabled by other options
            if (poll_seconds != -1) {
                // Command line override of polling interval
                if (poll_seconds == 0) {
                    poll_interval_micros = 0;  // Disable
                    g_message("ConnectedDisplaysChanged - polling disabled by --poll-interval 0");
                }
                else if (poll_seconds < MIN_POLL_SECONDS) {
                    g_print("Polling interval parameter must be at least %d seconds.", MIN_POLL_SECONDS);
                    exit(1);
                }
                else {
                    poll_interval_micros = poll_seconds * 1000000;
                }
            }
            g_message("Enabled ConnectedDisplaysChanged signal - using internal polling every %ld seconds",
                poll_interval_micros / 1000000);

            if (poll_cascade_interval_seconds > 0) {
                // Command line override of polling interval
                if (poll_cascade_interval_seconds < MIN_POLL_CASCADE_INTERVAL_SECONDS) {
                    g_print("Polling cascade interval parameter must be at least %5.3f seconds.",
                        MIN_POLL_CASCADE_INTERVAL_SECONDS);
                    exit(1);
                }
                else {
                    poll_cascade_interval_micros = (long) (poll_cascade_interval_seconds * 1000000);
                }
            }
            g_message("Set ConnectedDisplaysChanged event cascade interval to %5.3f seconds",
                poll_cascade_interval_micros / 1000000.0);
        }

        enable_custom_source(main_loop);  // May do nothing - but a client may enable events or polling later
    }

    g_main_loop_run(main_loop);
    g_bus_unown_name(owner_id);
    g_dbus_node_info_unref(introspection_data);
    return 0;
}
