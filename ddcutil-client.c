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

/**
 * Compilation:  cc $(pkg-config --cflags --libs gio-2.0 glib-2.0) -o ddcutil-client  ddcutil-client.c
 * Usage: ./ddcutil-client --display <display_number> getvcp <vcp_code>
 *        ./ddcutil-client --edid <edid> getvcp <vcp_code>
 *        ./ddcutil-client --display <display_number> setvcp <vcp_code> <vcp_new_value>
 *        ./ddcutil-client --edid <edid> setvcp <vcp_code> <vcp_new_value>
 *        ./ddcutil-client detect
 */
#include <gio/gio.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

typedef enum op_status_values {
    COMPLETED_WITHOUT_ERROR = 0,
    SERVICE_ERROR = 1,
    DBUS_ERROR = 2,
    SYNTAX_ERROR = 3,
} op_status_t;

static char *const DBUS_BUS_NAME = "com.ddcutil.DdcutilService";

static char *const DBUS_OBJECT_PATH = "/com/ddcutil/DdcutilObject";

static char *const DBUS_INTERFACE_NAME = "com.ddcutil.DdcutilInterface";

static op_status_t handle_dbus_error(const char *operation_name, GError *error) {
    if (error) {
        g_printerr("DBUS Error calling %s: %s\n", operation_name, error->message);
        g_error_free(error);
        return DBUS_ERROR;
    }
    return COMPLETED_WITHOUT_ERROR;
}

static void print_call_status(const char *operation_name, gint ddcutil_status, const gchar *error_message) {
    g_print("operation: %s\n", operation_name);
    g_print("error_status: %d\n", ddcutil_status);
    g_print("error message: %s\n", error_message);
}

static op_status_t call_set_vcp(GDBusConnection *connection,
                                int display_number, const char *edid_txt, guint8 vcp_code, guint16 vcp_new_value) {
    const char * operation_name = "SetVcp";
    GError *error = NULL;
    GVariant *result;

    result = g_dbus_connection_call_sync(connection,
                                         DBUS_BUS_NAME,
                                         DBUS_OBJECT_PATH,
                                         DBUS_INTERFACE_NAME,
                                         operation_name,  // Method name
                                         g_variant_new("(isyqu)", display_number, edid_txt, vcp_code, vcp_new_value),
                                         G_VARIANT_TYPE("(is)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);
    
    op_status_t dbus_status = handle_dbus_error(operation_name, error);
    if (dbus_status != COMPLETED_WITHOUT_ERROR) {
        return dbus_status;
    }

    gint ddcutil_status;
    const gchar *error_message;
    g_variant_get(result, "(is)", &ddcutil_status, &error_message);
    print_call_status(operation_name, ddcutil_status, error_message);
    g_variant_unref(result);
    return ddcutil_status == 0 ? COMPLETED_WITHOUT_ERROR : SERVICE_ERROR;
}

static op_status_t call_get_vcp(
        GDBusConnection *connection, int display_number, const char *edid_txt, guint8 vcp_code) {
    const char * operation_name = "GetVcp";
    GError *error = NULL;
    GVariant *result;

    result = g_dbus_connection_call_sync(connection,
                                         DBUS_BUS_NAME,
                                         DBUS_OBJECT_PATH,
                                         DBUS_INTERFACE_NAME,
                                         operation_name,  // Method name
                                         g_variant_new("(isyu)", display_number, edid_txt, vcp_code, 0),
                                         G_VARIANT_TYPE("(qqsis)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);

    op_status_t dbus_status = handle_dbus_error(operation_name, error);
    if (dbus_status != COMPLETED_WITHOUT_ERROR) {
        return dbus_status;
    }

    guint16 vcp_current_value, vcp_max_value;
    const gchar *vcp_formatted_value, *error_message;
    gint ddcutil_status;
    g_variant_get(result, "(qqsis)",
                  &vcp_current_value, &vcp_max_value, &vcp_formatted_value, &ddcutil_status, &error_message);
    print_call_status(operation_name, ddcutil_status, error_message);
    g_print("vcp_current_value: %d\n", vcp_current_value);
    g_print("vcp_max_value: %d\n", vcp_max_value);
    g_print("formatted_value: %s\n", vcp_formatted_value);
    g_variant_unref(result);
    return ddcutil_status == 0 ? COMPLETED_WITHOUT_ERROR : SERVICE_ERROR;
}

static op_status_t call_capabilities_metadata(GDBusConnection *connection, int display_number, const char *edid_txt) {
    const char * operation_name = "GetCapabilitiesMetadata";
    GError *error = NULL;
    GVariant *result;

    result = g_dbus_connection_call_sync(connection,
                                         DBUS_BUS_NAME,
                                         DBUS_OBJECT_PATH,
                                         DBUS_INTERFACE_NAME,
                                         operation_name,  // Method name
                                         g_variant_new("(isu)", display_number, edid_txt, 0),
                                         G_VARIANT_TYPE("(syya{ys}a{y(ssa{ys})}is)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);

    op_status_t dbus_status = handle_dbus_error(operation_name, error);
    if (dbus_status != COMPLETED_WITHOUT_ERROR) {
        return dbus_status;
    }

    const gchar *model_name;
    guint8 mccs_major, mccs_minor;
    GVariantIter *commands_iter, *capabilities_iter;
    gint32 ddcutil_status;
    const gchar *error_message;

    g_variant_get(result, "(syya{ys}a{y(ssa{ys})}is)",
                  &model_name, &mccs_major, &mccs_minor, &commands_iter, &capabilities_iter, &ddcutil_status, &error_message);

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
    while (g_variant_iter_loop(capabilities_iter, "{y(ssa{ys})}", &cap_key, &cap_name, &cap_description, &cap_commands_iter)) {
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

static op_status_t call_capabilities(GDBusConnection *connection, int display_number, const char *edid_txt) {
    const char * operation_name = "GetCapabilitiesString";
    GError *error = NULL;
    GVariant *result;

    result = g_dbus_connection_call_sync(connection,
                                         DBUS_BUS_NAME,
                                         DBUS_OBJECT_PATH,
                                         DBUS_INTERFACE_NAME,
                                         operation_name,  // Method name
                                         g_variant_new("(isu)", display_number, edid_txt, 0),
                                         G_VARIANT_TYPE("(sis)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);

    op_status_t dbus_status = handle_dbus_error(operation_name, error);
    if (dbus_status != COMPLETED_WITHOUT_ERROR) {
        return dbus_status;
    }

    const gchar *capabilties_text, *error_message;
    gint ddcutil_status;
    g_variant_get(result, "(sis)", &capabilties_text, &ddcutil_status, &error_message);
    print_call_status(operation_name, ddcutil_status, error_message);
    g_print("%s\n", capabilties_text);
    g_variant_unref(result);
    return ddcutil_status == 0 ? COMPLETED_WITHOUT_ERROR : SERVICE_ERROR;
}

static op_status_t call_detect(GDBusConnection *connection) {
    const char * operation_name = "Detect";
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

    op_status_t dbus_status = handle_dbus_error(operation_name, error);
    if (dbus_status != COMPLETED_WITHOUT_ERROR) {
        return dbus_status;
    }

    GVariantIter *array_iter;
    GVariant *array_element;
    gint32 number_of_displays;
    gint32 ddcutil_status;
    const gchar *error_message;

    g_variant_get(result, "(ia(iiisssqsu)is)", &number_of_displays, &array_iter, &ddcutil_status, &error_message);

    print_call_status(operation_name, ddcutil_status, error_message);

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

static int parse_int(char *input_str, int base, op_status_t *status) {
    char *end_ptr;
    errno = 0;
    int result = (int) strtol(input_str, &end_ptr, base);
    if (input_str == end_ptr || errno != 0) {
        *status = SYNTAX_ERROR;
    }
    return result;
}

static op_status_t parse_display_and_edid(char *display_number_str, char *edid_txt, int *display_number) {
    op_status_t exit_status = COMPLETED_WITHOUT_ERROR;
    if (display_number_str == NULL && strlen(edid_txt) > 0) {
        *display_number = -1;
    }
    else {
        errno = 0;
        int parsed_display_number = display_number_str != NULL ? parse_int(display_number_str, 10, &exit_status) : 1;
        if (exit_status != 0) {
            g_printerr("Invalid Display Number. It must be in decimal format (e.g., 1).\n");
        }
        *display_number = parsed_display_number;
    }
    return exit_status;
}

int main(int argc, char *argv[]) {
    GError *error = NULL;
    GOptionContext *context;
    gchar *display_number_str = NULL;
    gchar *edid_txt = "";
    gchar **remaining_args = NULL;

    GOptionEntry entries[] = {
            {"display", 'd', 0, G_OPTION_ARG_STRING, &display_number_str, "Display number", "DISPLAY_NUMBER"},
            {"edid", 'e', 0, G_OPTION_ARG_STRING, &edid_txt, "EDID", "EDID"},
            {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &remaining_args, NULL, NULL},
            {NULL}
    };

    context = g_option_context_new("detect | capabilities | getvcp 0xNN | setvcp 0xNN n");
    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("Error parsing options: %s\n", error->message);
        g_error_free(error);
        return SYNTAX_ERROR;
    }

    if (!remaining_args || !remaining_args[0]) {
        g_printerr("You must provide a method (detect, getvcp, or setvcp) and appropriate arguments.\n");
        return SYNTAX_ERROR;
    }

    gchar *method = remaining_args[0];

    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!connection) {
        g_printerr("Error connecting to session bus: %s\n", error->message);
        g_error_free(error);
        return DBUS_ERROR;
    }

    op_status_t exit_status;

    if (g_strcmp0(method, "detect") == 0) {
        exit_status = call_detect(connection);
    } else if (g_strcmp0(method, "setvcp") == 0) {
        int display_number = -1;
        exit_status = parse_display_and_edid(display_number_str, edid_txt, &display_number);
        if (exit_status == COMPLETED_WITHOUT_ERROR) {
            if (!remaining_args[1] || !remaining_args[2]) {
                g_printerr("You must provide a VCP code and a new value for setvcp.\n");
                exit_status = SYNTAX_ERROR;
            }
            else {
                guint8 vcp_code = (guint8) parse_int(remaining_args[1], 16, &exit_status);
                if (exit_status != 0) {
                    g_printerr("Invalid VCP code. It must be in hex format (e.g. 0x10).\n");
                } else {
                    guint16 vcp_new_value = (guint16) parse_int(remaining_args[2], 10, &exit_status);
                    if (exit_status != 0) {
                        g_printerr("Invalid new value. It must be in decimal (e.g. 80).\n");
                    } else {
                        exit_status = call_set_vcp(connection, display_number, edid_txt, vcp_code, vcp_new_value);
                    }
                }
            }
        }
    } else if (g_strcmp0(method, "getvcp") == 0) {
        int display_number = -1;
        exit_status = parse_display_and_edid(display_number_str, edid_txt, &display_number);
        if (exit_status != COMPLETED_WITHOUT_ERROR) {
            if (!remaining_args[1]) {
                g_printerr("You must provide a VCP code for getvcp.\n");
                exit_status = SYNTAX_ERROR;
            } else {
                guint8 vcp_code = (guint8) parse_int(remaining_args[1], 16, &exit_status);
                if (exit_status != 0) {
                    g_printerr("Invalid VCP code. It must be in hex format (e.g. 0x10).\n");
                } else {
                    exit_status = call_get_vcp(connection, display_number, edid_txt, vcp_code);
                }
            }
        }
    } else if (g_strcmp0(method, "capabilities") == 0) {
        int display_number = -1;
        exit_status = parse_display_and_edid(display_number_str, edid_txt, &display_number);
        if (exit_status == COMPLETED_WITHOUT_ERROR) {
            exit_status = call_capabilities_metadata(connection, display_number, edid_txt);
        }
    } else if (g_strcmp0(method, "capabilities-terse") == 0) {
        int display_number = -1;
        exit_status = parse_display_and_edid(display_number_str, edid_txt, &display_number);
        if (exit_status == COMPLETED_WITHOUT_ERROR) {
            exit_status = call_capabilities(connection, display_number, edid_txt);
        }
    } else {
        g_printerr("Unknown command: %s\n", method);
        exit_status = SYNTAX_ERROR;
    }

    g_object_unref(connection);
    return exit_status;
}
