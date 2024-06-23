/* ----------------------------------------------------------------------------------------------------
 * ddcutil-client.c
 * -----------------
 * A D-Bus client for ddcutil-service
 *
 *
 * Copyright (C) 2024, Michael Hamilton
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

#include <gio/gio.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

/**
 * Status numbers that might be returned from main()
 * (as opposed to status numbers returned by dbus or libddcutil)
 */
typedef enum cmd_status_values {
    COMPLETED_WITHOUT_ERROR = 0,
    SERVICE_ERROR = 1,
    DBUS_ERROR = 2,
    SYNTAX_ERROR = 3,
} cmd_status_t;

/* ----------------------------------------------------------------------------------------------------
 * Bit flags that can be passed in the service method flags argument.
 */
typedef enum {
    EDID_PREFIX = 1,        // Indicates the EDID passed to the service is a unique prefix (substr) of the actual EDID.
    RETURN_RAW_VALUES = 2,  // GetVcp GetMultipleVcp
    NO_VERIFY = 4,          // SetVcp

} Service_Flags_Enum_t;


static char *const DBUS_BUS_NAME = "com.ddcutil.DdcutilService";
static char *const DBUS_OBJECT_PATH = "/com/ddcutil/DdcutilObject";
static char *const DBUS_INTERFACE_NAME = "com.ddcutil.DdcutilInterface";

static GDBusConnection *connection = NULL;

static char *boolean_value(int bool) {
    return bool ? "true" : "false";
}

/**
 * @brief Output the dbus error to stderr and translated the status code to cmd_status_t
 * @param operation_name operation name for including on the output
 * @param error dbus error structure
 * @return cmd_status_t
 */
static cmd_status_t handle_dbus_error(const char *operation_name, GError *error) {
    if (error) {
        g_printerr("ERROR: DBUS Error calling %s: %s\n", operation_name, error->message);
        g_error_free(error);
        return DBUS_ERROR;
    }
    return COMPLETED_WITHOUT_ERROR;
}

/**
 * @brief Output a ddcutil status to stdout.
 * @param operation_name operation name for including on the output
 * @param ddcutil_status return code from ddcutil
 * @param error_message message from ddcutil (or interpretation there of)
 */
static void report_ddcutil_status(const char *operation_name, gint ddcutil_status, const gchar *error_message) {
    g_print("operation: %s\n", operation_name);
    g_print("error_status: %d\n", ddcutil_status);
    g_print("error message: %s\n", error_message);
}

/**
 * @brief Implement setvcp
 * @param connection dbus connection to ddcutil-service
 * @param display_number ddcutil display number
 * @param edid_base64 base64 encoded EDID or EDID-prefix
 * @param vcp_code DDC VCP-code
 * @param vcp_new_value DDC VCP-value
 * @return COMPLETED_WITHOUT_ERROR, SERVICE_ERROR or DBUS_ERROR
 */
static cmd_status_t call_set_vcp(GDBusConnection *connection,
                                 int display_number, const char *edid_base64, guint8 vcp_code, guint16 vcp_new_value) {
    const char *operation_name = "SetVcp";
    GError *error = NULL;
    GVariant *result;
    const guint flags = strlen(edid_base64) == 0 ? 0 : 1;

    result = g_dbus_connection_call_sync(connection,
                                         DBUS_BUS_NAME,
                                         DBUS_OBJECT_PATH,
                                         DBUS_INTERFACE_NAME,
                                         operation_name,  // Method name
                                         g_variant_new("(isyqu)",
                                                       display_number, edid_base64, vcp_code, vcp_new_value, flags),
                                         G_VARIANT_TYPE("(is)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);

    cmd_status_t dbus_status = handle_dbus_error(operation_name, error);
    if (dbus_status != COMPLETED_WITHOUT_ERROR) {
        return dbus_status;
    }

    gint ddcutil_status;
    const gchar *error_message;
    g_variant_get(result, "(is)", &ddcutil_status, &error_message);
    report_ddcutil_status(operation_name, ddcutil_status, error_message);
    g_variant_unref(result);
    return ddcutil_status == 0 ? COMPLETED_WITHOUT_ERROR : SERVICE_ERROR;
}

/**
 * @brief Implement getvcp, outputs result to stdout
 * @param connection dbus connection to ddcutil-service
 * @param display_number ddcutil display number
 * @param edid_base64 base64 encoded EDID or EDID-prefix
 * @param vcp_code DDC VCP-code
 * @param flags
 * @return COMPLETED_WITHOUT_ERROR, SERVICE_ERROR or DBUS_ERROR
 */
static cmd_status_t call_get_vcp(
        GDBusConnection *connection, int display_number, const char *edid_base64, guint8 vcp_code,
        Service_Flags_Enum_t flags) {
    const char *operation_name = "GetVcp";
    GError *error = NULL;
    GVariant *result;
    flags |= (strlen(edid_base64) == 0 ? 0 : EDID_PREFIX);  // Allow EDID prefix matches

    result = g_dbus_connection_call_sync(connection,
                                         DBUS_BUS_NAME,
                                         DBUS_OBJECT_PATH,
                                         DBUS_INTERFACE_NAME,
                                         operation_name,  // Method name
                                         g_variant_new("(isyu)", display_number, edid_base64, vcp_code, flags),
                                         G_VARIANT_TYPE("(qqsis)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);

    cmd_status_t dbus_status = handle_dbus_error(operation_name, error);
    if (dbus_status != COMPLETED_WITHOUT_ERROR) {
        return dbus_status;
    }

    guint16 vcp_current_value, vcp_max_value;
    const gchar *vcp_formatted_value, *error_message;
    gint ddcutil_status;
    g_variant_get(result, "(qqsis)",
                  &vcp_current_value, &vcp_max_value, &vcp_formatted_value, &ddcutil_status, &error_message);
    report_ddcutil_status(operation_name, ddcutil_status, error_message);
    g_print("snc_16bit: %s\n", boolean_value(flags & RETURN_RAW_VALUES));
    g_print("vcp_current_value: %d\n", vcp_current_value);
    g_print("vcp_max_value: %d\n", vcp_max_value);
    g_print("formatted_value: %s\n", vcp_formatted_value);
    g_variant_unref(result);
    return ddcutil_status == 0 ? COMPLETED_WITHOUT_ERROR : SERVICE_ERROR;
}

/**
 * @brief Implement getvcp, outputs result to stdout
 * @param connection dbus connection to ddcutil-service
 * @param display_number ddcutil display number
 * @param edid_base64 base64 encoded EDID or EDID-prefix
 * @param vcp_code DDC VCP-code
 * @param flags
 * @return COMPLETED_WITHOUT_ERROR, SERVICE_ERROR or DBUS_ERROR
 */
static cmd_status_t call_get_vcp_metadata(
        GDBusConnection *connection, int display_number, const char *edid_base64, guint8 vcp_code) {
    const char *operation_name = "GetVcpMetadata";
    GError *error = NULL;
    GVariant *result;
    const int flags = (strlen(edid_base64) == 0 ? 0 : EDID_PREFIX);  // Allow EDID prefix matches

    result = g_dbus_connection_call_sync(connection,
                                         DBUS_BUS_NAME,
                                         DBUS_OBJECT_PATH,
                                         DBUS_INTERFACE_NAME,
                                         operation_name,  // Method name
                                         g_variant_new("(isyu)", display_number, edid_base64, vcp_code, flags),
                                         G_VARIANT_TYPE("(ssbbbbbis)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);

    cmd_status_t dbus_status = handle_dbus_error(operation_name, error);
    if (dbus_status != COMPLETED_WITHOUT_ERROR) {
        return dbus_status;
    }

    gchar *feature_name;
    gchar *feature_description;
    gboolean is_read_only;
    gboolean is_write_only;
    gboolean is_rw;
    gboolean is_complex;
    gboolean is_continuous;

    const gchar *error_message;
    gint ddcutil_status;
    g_variant_get(result, "(ssbbbbbis)",
                  &feature_name, &feature_description,
                  &is_read_only, &is_write_only, &is_rw, &is_complex, &is_continuous,
                  &ddcutil_status, &error_message);
    report_ddcutil_status(operation_name, ddcutil_status, error_message);
    g_print("vcp_code: 0x%x\n", vcp_code);
    g_print("feature_name: %s\n", feature_name);
    g_print("feature_description: %s\n", feature_description);
    g_print("is_read_only: %s\n", boolean_value(is_read_only));
    g_print("is_write_only: %s\n", boolean_value(is_write_only));
    g_print("is_rw: %s\n", boolean_value(is_rw));
    g_print("is_complex: %s\n", boolean_value(is_complex));
    g_print("is_continuous: %s\n", boolean_value(is_continuous));
    g_variant_unref(result);
    return ddcutil_status == 0 ? COMPLETED_WITHOUT_ERROR : SERVICE_ERROR;
}

/**
 * @brief Implements capabilities, outputs indented capabilities structure with value-text to stdout
 * @param connection dbus connection to ddcutil-service
 * @param display_number ddcutil display number
 * @param edid_base64 base64 encoded EDID or EDID-prefix
 * @return COMPLETED_WITHOUT_ERROR, SERVICE_ERROR or DBUS_ERROR
 */
static cmd_status_t call_capabilities_metadata(
        GDBusConnection *connection, int display_number, const char *edid_base64) {
    const char *operation_name = "GetCapabilitiesMetadata";
    GError *error = NULL;
    GVariant *result;
    guint flags = strlen(edid_base64) == 0 ? 0 : 1;

    result = g_dbus_connection_call_sync(connection,
                                         DBUS_BUS_NAME,
                                         DBUS_OBJECT_PATH,
                                         DBUS_INTERFACE_NAME,
                                         operation_name,  // Method name
                                         g_variant_new("(isu)", display_number, edid_base64, flags),
                                         G_VARIANT_TYPE("(syya{ys}a{y(ssa{ys})}is)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);

    cmd_status_t dbus_status = handle_dbus_error(operation_name, error);
    if (dbus_status != COMPLETED_WITHOUT_ERROR) {
        return dbus_status;
    }

    const gchar *model_name;
    guint8 mccs_major, mccs_minor;
    GVariantIter *commands_iter, *capabilities_iter;
    gint32 ddcutil_status;
    const gchar *error_message;

    g_variant_get(result, "(syya{ys}a{y(ssa{ys})}is)",
                  &model_name, &mccs_major, &mccs_minor, &commands_iter, &capabilities_iter, &ddcutil_status,
                  &error_message);

    g_print("model_name: %s\n", model_name);
    g_print("mccs_major: %d\n", mccs_major);
    g_print("mccs_minor: %d\n", mccs_minor);

    g_print("commands:\n");
    guint8 cmd_key;
    const gchar *cmd_value;
    while (g_variant_iter_loop(commands_iter, "{y&s}", &cmd_key, &cmd_value)) {
        g_print("  command: 0x%x, Description: %s\n", cmd_key, cmd_value);
    }
    g_variant_iter_free(commands_iter);

    g_print("capabilities:\n");
    guint8 cap_key;
    const gchar *cap_name, *cap_description;
    GVariantIter *cap_commands_iter = NULL;
    while (g_variant_iter_loop(capabilities_iter, "{y(ssa{ys})}", &cap_key, &cap_name, &cap_description,
                               &cap_commands_iter)) {
        g_print("  capability: 0x%x, Name: %s, Description: %s\n", cap_key, cap_name, cap_description);

        guint8 subcmd_key;
        const gchar *subcmd_value;
        if (cap_commands_iter) {
            while (g_variant_iter_loop(cap_commands_iter, "{y&s}", &subcmd_key, &subcmd_value)) {
                g_print("    value: 0x%x, Description: %s\n", subcmd_key, subcmd_value);
            }
            g_variant_iter_free(cap_commands_iter);
            cap_commands_iter = NULL;
        }
    }
    g_variant_iter_free(capabilities_iter);


    return ddcutil_status == 0 ? COMPLETED_WITHOUT_ERROR : SERVICE_ERROR;
}

/**
 * @brief Implements terse capabilities, outputs bracket-structured capabilities structure to stdout
 * @param connection dbus connection to ddcutil-service
 * @param display_number ddcutil display number
 * @param edid_base64 base64 encoded EDID or EDID-prefix
 * @return COMPLETED_WITHOUT_ERROR, SERVICE_ERROR or DBUS_ERROR
 */
static cmd_status_t call_capabilities(GDBusConnection *connection, int display_number, const char *edid_txt) {
    const char *operation_name = "GetCapabilitiesString";
    GError *error = NULL;
    GVariant *result;
    const guint flags = strlen(edid_txt) == 0 ? 0 : 1;

    result = g_dbus_connection_call_sync(connection,
                                         DBUS_BUS_NAME,
                                         DBUS_OBJECT_PATH,
                                         DBUS_INTERFACE_NAME,
                                         operation_name,  // Method name
                                         g_variant_new("(isu)", display_number, edid_txt, flags),
                                         G_VARIANT_TYPE("(sis)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);

    cmd_status_t dbus_status = handle_dbus_error(operation_name, error);
    if (dbus_status != COMPLETED_WITHOUT_ERROR) {
        return dbus_status;
    }

    const gchar *capabilties_text, *error_message;
    gint ddcutil_status;
    g_variant_get(result, "(sis)", &capabilties_text, &ddcutil_status, &error_message);
    report_ddcutil_status(operation_name, ddcutil_status, error_message);
    g_print("%s\n", capabilties_text);
    g_variant_unref(result);
    return ddcutil_status == 0 ? COMPLETED_WITHOUT_ERROR : SERVICE_ERROR;
}

/**
 * @brief Implements detect, outputs detected VDUs to stdout
 * @param connection dbus connection to ddcutil-service
 * @return COMPLETED_WITHOUT_ERROR, SERVICE_ERROR or DBUS_ERROR
 */
static cmd_status_t call_detect(GDBusConnection *connection) {
    const char *operation_name = "Detect";
    GError *error = NULL;
    GVariant *result;

    result = g_dbus_connection_call_sync(connection,
                                         DBUS_BUS_NAME,
                                         DBUS_OBJECT_PATH,
                                         DBUS_INTERFACE_NAME,
                                         operation_name,  // Method name
                                         g_variant_new("(u)", 0),
                                         G_VARIANT_TYPE("(ia(iiisssqsu)is)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);

    cmd_status_t dbus_status = handle_dbus_error(operation_name, error);
    if (dbus_status != COMPLETED_WITHOUT_ERROR) {
        return dbus_status;
    }

    GVariantIter *array_iter;
    GVariant *array_element;
    gint32 number_of_displays;
    gint32 ddcutil_status;
    const gchar *error_message;

    g_variant_get(result, "(ia(iiisssqsu)is)", &number_of_displays, &array_iter, &ddcutil_status, &error_message);

    report_ddcutil_status(operation_name, ddcutil_status, error_message);

    g_print("number_of_displays: %d\n", number_of_displays);

    while ((array_element = g_variant_iter_next_value(array_iter)) != NULL) {
        gint32 display_number, usb_bus_number, usb_device_number;
        gchar *manufacturer, *product_name, *serial_number;
        guint16 product_code;
        gchar *edid_base64_encoded;
        guint32 binary_serial_number;

        g_variant_get(array_element, "(iiisssqsu)",
                      &display_number, &usb_bus_number, &usb_device_number,
                      &manufacturer, &product_name, &serial_number, &product_code,
                      &edid_base64_encoded, &binary_serial_number);

        g_print("display: %d\n", display_number);
        if (usb_bus_number || usb_device_number) {
            g_print("  usb_bus_number: %d\n", usb_bus_number);
            g_print("  usb_device_number: %d\n", usb_device_number);
        }
        g_print("  manufacturer: %s\n", manufacturer);
        g_print("  product_name: %s\n", product_name);
        g_print("  serial_number: %s\n", serial_number);
        g_print("  product_code: 0x%x\n", product_code);
        g_print("  edid_base64_encoded: %s\n", edid_base64_encoded);
        g_print("  binary_serial_number: 0x%x\n", binary_serial_number);
        g_variant_unref(array_element);
    }

    g_variant_iter_free(array_iter);
    g_variant_unref(result);
    return ddcutil_status == 0 ? COMPLETED_WITHOUT_ERROR : SERVICE_ERROR;
}

static cmd_status_t print_property(GDBusConnection *connection, char *property_name) {
    GVariant *result;
    GVariant *property_value;
    GError *error = NULL;
    result = g_dbus_connection_call_sync(connection,
                                         DBUS_BUS_NAME,
                                         DBUS_OBJECT_PATH,
                                         "org.freedesktop.DBus.Properties",
                                         "Get",
                                         g_variant_new("(ss)", DBUS_INTERFACE_NAME, property_name),
                                         G_VARIANT_TYPE("(v)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);
    if (error == NULL) {
        g_variant_get(result, "(v)", &property_value);
        gchar *property_str = g_variant_print(property_value, TRUE);
        g_print("%s: %s\n", property_name, property_str);
        g_free(property_str);
        g_variant_unref(property_value);
        g_variant_unref(result);
    }
    return handle_dbus_error("print_property", error);
}

static cmd_status_t set_property(GDBusConnection *connection, char *property_name, GVariant *property_value) {
    GError *error = NULL;
    //GVariant *value = g_variant_new_int32(property_value);
    GVariant *result = g_dbus_connection_call_sync(
            connection,
            DBUS_BUS_NAME,
            DBUS_OBJECT_PATH,
            "org.freedesktop.DBus.Properties",
            "Set",
            g_variant_new("(ssv)", DBUS_INTERFACE_NAME, property_name, property_value),
            G_VARIANT_TYPE("()"),
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            NULL,
            &error
    );
    if (error == NULL) {
        g_variant_unref(property_value);
        g_variant_unref(result);
    }
    return handle_dbus_error("set_property", error);
}

/**
 * Parse a command line int argument using strtol
 * @param input_str the command line argument
 * @param base base, such as 10 for decimal or 16 for hex
 * @param status COMPLETED_WITHOUT_ERROR or SYNTAX_ERROR
 * @return value from wrapped strtol conversion
 */
static int parse_int(char *input_str, int base, cmd_status_t *status) {
    char *end_ptr;
    errno = 0;
    int result = (int) strtol(input_str, &end_ptr, base);
    *status = (input_str == end_ptr || errno != 0) ? SYNTAX_ERROR : COMPLETED_WITHOUT_ERROR;
    return result;
}

/**
 * Parse an validate the display number string and base64 encoded edid (combo)
 * @param display_number_ptr display number, may be -1 if not supplied
 * @param edid_base64 EDID, may be the empty string
 * @return COMPLETED_WITHOUT_ERROR or SYNTAX_ERROR
 */
static cmd_status_t parse_display_and_edid(gint *display_number_ptr, char *edid_base64) {
    if (strlen(edid_base64) > 0 && strlen(edid_base64) < 12) {
        g_printerr("ERROR: Invalid EDID. It must be at least 12 characters long.\n");
        return SYNTAX_ERROR;
    }
    if (*display_number_ptr != -1 && strlen(edid_base64) > 0) {
        g_printerr("ERROR: Pass only one of Display Number or EDID, not both.\n");
        return SYNTAX_ERROR;
    }
    if (strlen(edid_base64) > 0) { // Using EDID
        g_print("edid_base64_encoded: %s\n", edid_base64);
    } else { // Using Display Number, default to 1 if none passed
        if (*display_number_ptr == -1) {
            *display_number_ptr  = 1;
        }
        g_print("display_number: %d\n", *display_number_ptr);
    }
    return COMPLETED_WITHOUT_ERROR;
}

/**
 * @brief Return the service property name for the command line option that gets/sets the property.
 * @param option_name command line option name, may be long or short form.
 * @return the service property name corresponding to option_namw, or NULL if no match was found.
 */
static char *get_service_property_name(const gchar *option_name) {
    if (g_str_equal(option_name, "--info-logging") || g_str_equal(option_name, "-i")) {
        return "ServiceInfoLogging";
    } else if (g_str_equal(option_name, "--signal") || g_str_equal(option_name, "-s")) {
        return "ServiceEmitConnectivitySignals";
    }
    return NULL;
}

/**
 * @brief GOptionEntry get/set callback for handling get/set boolean options on service properties
 * @param option_name command line option name, might be the long or short name
 * @param value new value, if setting, NULL if getting
 * @param data not used (option group related)
 * @param error not used
 * @return TRUE if handled without error, otherwise FAlSE
 */
static gboolean handle_boolean_prop(const gchar *option_name, const gchar *value, gpointer data, GError **error) {
    char *service_property_name = get_service_property_name(option_name);
    if (service_property_name == NULL) {
        g_printerr("ERROR: unrecognised option name %s\n", option_name);
        return FALSE;
    }
    if (value != NULL) {
        g_print("Set service property: %s\n", service_property_name);
        if (g_str_equal(value, "on") || g_str_equal(value, "true") || g_str_equal(value, "enable")) {
            set_property(connection, service_property_name, g_variant_new_boolean(TRUE));
        } else if (g_str_equal(value, "off") || g_str_equal(value, "false") || g_str_equal(value, "disable")) {
            set_property(connection, service_property_name, g_variant_new_boolean(FALSE));
        } else {
            g_printerr("ERROR: invalid value for %s, should be on/off true/false enable/disable.\n", option_name);
        }
    }
    print_property(connection, service_property_name);
    return TRUE;
}

int main(int argc, char *argv[]) {
    GError *error = NULL;
    GOptionContext *context;
    gint display_number = -1;
    gchar *edid_txt = "";
    gchar **remaining_args = NULL;
    gint raw = 0;
    gint version = 0;
    gint ddversion = 0;
    gint locked = 0;
    gint status_values = 0;
    gint display_event_types = 0;
    gint service_flag_options = 0;

    connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!connection) {
        g_printerr("ERROR: Error connecting to session bus: %s\n", error->message);
        g_error_free(error);
        return DBUS_ERROR;
    }

    GOptionEntry entries[] = {
            {"display",          'd', 0,                          G_OPTION_ARG_INT,          &display_number,       "Display number"},
            {"edid",             'e', 0,                          G_OPTION_ARG_STRING,       &edid_txt,             "EDID"},
            {"raw",              'r', 0,                          G_OPTION_ARG_NONE,         &raw,                  "getvcp returns SNC-features as raw 16-bit values"},
            {"version",          'v', 0,                          G_OPTION_ARG_NONE,         &version,              "query the service interface version"},
            {"ddcutil-version",  'V', 0,                          G_OPTION_ARG_NONE,         &ddversion,            "query the ddcutil/libddcutil version"},
            {"info-logging",     'i', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK,     &handle_boolean_prop,  "query or set info logging"},
            {"signal",           's', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK,     &handle_boolean_prop,  "query or set service hotplug signals"},
            {"locked",           'l', 0,                          G_OPTION_ARG_NONE,         &locked,               "query if service parameters are locked"},
            {"status-values",    't', 0,                          G_OPTION_ARG_NONE,         &status_values,        "list all ddcutil status values"},
            {"display-events",   'p', 0,                          G_OPTION_ARG_NONE,         &display_event_types,  "list all display event types"},
            {"service-flags",    'f', 0,                          G_OPTION_ARG_NONE,         &service_flag_options, "list all service flag options"},
            {G_OPTION_REMAINING, 0,   0,                          G_OPTION_ARG_STRING_ARRAY, &remaining_args},
            {NULL}
    };

    context = g_option_context_new(
            "[detect | capabilities | capabilities-terse | getvcp 0xNN | setvcp 0xNN n]");
    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("ERROR: Error parsing options: %s\n", error->message);
        g_error_free(error);
        return SYNTAX_ERROR;
    }

    cmd_status_t exit_status;
    if (version != 0) {
        exit_status = print_property(connection, "ServiceInterfaceVersion");
    }
    if (ddversion != 0) {
        exit_status = print_property(connection, "DdcutilVersion");
    }
    if (locked != 0) {
        exit_status = print_property(connection, "ServiceParametersLocked");
    }
    if (status_values != 0) {
        exit_status = print_property(connection, "StatusValues");
    }
    if (display_event_types != 0) {
        exit_status = print_property(connection, "DisplayEventTypes");
    }
    if (service_flag_options != 0) {
        exit_status = print_property(connection, "ServiceFlagOptions");
    }
    if (remaining_args != NULL) {
        gchar *method = remaining_args[0];

        if (!remaining_args || !remaining_args[0]) {
            g_printerr("ERROR: You must provide a method (detect, getvcp, or setvcp) and appropriate arguments.\n");
            exit_status = SYNTAX_ERROR;
        } else if (g_strcmp0(method, "detect") == 0) {
            exit_status = call_detect(connection);
        } else if (g_strcmp0(method, "setvcp") == 0) {
            exit_status = parse_display_and_edid(&display_number, edid_txt);
            if (exit_status == COMPLETED_WITHOUT_ERROR) {
                if (!remaining_args[1] || !remaining_args[2]) {
                    g_printerr("ERROR: You must provide a VCP code and a new value for setvcp.\n");
                    exit_status = SYNTAX_ERROR;
                } else {
                    guint8 vcp_code = (guint8) parse_int(remaining_args[1], 16, &exit_status);
                    if (exit_status != 0) {
                        g_printerr("ERROR: Invalid VCP code. It must be in hex format (e.g. 0x10).\n");
                    } else {
                        guint16 vcp_new_value = (guint16) parse_int(remaining_args[2], 10, &exit_status);
                        if (exit_status != 0) {
                            g_printerr("ERROR: Invalid new value. It must be in decimal (e.g. 80).\n");
                        } else {
                            exit_status = call_set_vcp(connection, display_number, edid_txt, vcp_code, vcp_new_value);
                        }
                    }
                }
            }
        } else if (g_strcmp0(method, "getvcp") == 0) {
            exit_status = parse_display_and_edid(&display_number, edid_txt);
            if (exit_status == COMPLETED_WITHOUT_ERROR) {
                if (!remaining_args[1]) {
                    g_printerr("ERROR: You must provide a VCP code for getvcp.\n");
                    exit_status = SYNTAX_ERROR;
                } else {
                    guint8 vcp_code = (guint8) parse_int(remaining_args[1], 16, &exit_status);
                    if (exit_status != 0) {
                        g_printerr("ERROR: Invalid VCP code. It must be in hex format (e.g. 0x10).\n");
                    } else {
                        exit_status = call_get_vcp(connection, display_number, edid_txt, vcp_code,
                                                   raw ? RETURN_RAW_VALUES : 0);
                    }
                }
            }
        } else if (g_strcmp0(method, "getvcp-metadata") == 0) {
            int display_number = -1;
            exit_status = parse_display_and_edid(&display_number, edid_txt);
            if (exit_status == COMPLETED_WITHOUT_ERROR) {
                if (!remaining_args[1]) {
                    g_printerr("ERROR: You must provide a VCP code for getvcp-metadata.\n");
                    exit_status = SYNTAX_ERROR;
                } else {
                    guint8 vcp_code = (guint8) parse_int(remaining_args[1], 16, &exit_status);
                    if (exit_status != 0) {
                        g_printerr("ERROR: Invalid VCP code. It must be in hex format (e.g. 0x10).\n");
                    } else {
                        exit_status = call_get_vcp_metadata(connection, display_number, edid_txt, vcp_code);
                    }
                }
            }
        } else if (g_strcmp0(method, "capabilities") == 0) {
            int display_number = -1;
            exit_status = parse_display_and_edid(&display_number, edid_txt);
            if (exit_status == COMPLETED_WITHOUT_ERROR) {
                exit_status = call_capabilities_metadata(connection, display_number, edid_txt);
            }
        } else if (g_strcmp0(method, "capabilities-terse") == 0) {
            int display_number = -1;
            exit_status = parse_display_and_edid(&display_number, edid_txt);
            if (exit_status == COMPLETED_WITHOUT_ERROR) {
                exit_status = call_capabilities(connection, display_number, edid_txt);
            }
        } else {
            g_printerr("ERROR: Unknown command: %s\n", method);
            exit_status = SYNTAX_ERROR;
        }
    }
    g_object_unref(connection);
    return exit_status;
}
