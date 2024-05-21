/**
 * ddcutil-client: Example C Client
 * --------------------------------
 * This code is content entirely created by AI (Chat GPT-4o). Such content is provided for informational purposes only
 * and should not be relied upon for any specific purpose without verification of its accuracy or completeness.
 *
 * Compilation:  cc $(pkg-config --cflags --libs gio-2.0) -o ddcutil-client  ddcutil-client.c
 * Usage: ./ddcutil-client  <method> <display_number or edid> <vcp_code> [vcp_new_value]
 * Examples:
 *    ./ddcutil-client GetVcp 1 16       # Get display 1 VCP code decimal 16 (=0x brightness)
 *    ./ddcutil-client SetVcp 1 16 100   # Set display 1 VCP code decimal 16 (=0x brightness) to brightness 100.
 *    ./ddcutil-client GetVcp edid:blahblahblah 1 16     # Using an edid instead of display number.
 */
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void call_set_vcp(GDBusConnection *connection, int display_number, const char *edid_txt, guint8 vcp_code, guint16 vcp_new_value) {
    GError *error = NULL;
    GVariant *result;

    result = g_dbus_connection_call_sync(connection,
                                         "com.ddcutil.DdcutilService",  // Bus name
                                         "/com/ddcutil/DdcutilObject",  // Object path
                                         "com.ddcutil.DdcutilInterface",  // Interface name
                                         "SetVcp",  // Method name
                                         g_variant_new("(isyqu)", display_number, edid_txt, vcp_code, vcp_new_value),
                                         G_VARIANT_TYPE("(is)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);

    if (error) {
        g_printerr("Error calling SetVcp: %s\n", error->message);
        g_error_free(error);
    } else {
        g_print("SetVcp called successfully.\n");
        gint error_status;
        const gchar *error_message;
        g_variant_get(result, "(is)", &error_status, &error_message);
        g_print("Error status: %d, Error message: %s\n", error_status, error_message);
        g_variant_unref(result);
    }
}

static void call_get_vcp(GDBusConnection *connection, int display_number, const char *edid_txt, guint8 vcp_code) {
    GError *error = NULL;
    GVariant *result;

    result = g_dbus_connection_call_sync(connection,
                                         "com.ddcutil.DdcutilService",  // Bus name
                                         "/com/ddcutil/DdcutilObject",  // Object path
                                         "com.ddcutil.DdcutilInterface",  // Interface name
                                         "GetVcp",  // Method name
                                         g_variant_new("(isyu)", display_number, edid_txt, vcp_code, 0),
                                         G_VARIANT_TYPE("(qqsis)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);

    if (error) {
        g_printerr("Error calling GetVcp: %s\n", error->message);
        g_error_free(error);
    } else {
        g_print("GetVcp called successfully.\n");
        guint16 vcp_current_value, vcp_max_value;
        const gchar *vcp_formatted_value, *error_message;
        gint error_status;
        g_variant_get(result, "(qqsis)", &vcp_current_value, &vcp_max_value, &vcp_formatted_value, &error_status, &error_message);
        g_print("VCP current value: %d, VCP max value: %d, Formatted value: %s\n", vcp_current_value, vcp_max_value, vcp_formatted_value);
        g_print("Error status: %d, Error message: %s\n", error_status, error_message);
        g_variant_unref(result);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        g_printerr("Usage: %s <method> <display_number or edid> <vcp_code> [vcp_new_value]\n", argv[0]);
        return 1;
    }

    const char *method = argv[1];
    const char *display_or_edid = argv[2];
    const char *vcp_code_str = argv[3];
    const char *vcp_new_value_str = argc > 4 ? argv[4] : NULL;

    int display_number = -1;
    const char *edid_txt = "";
    guint8 vcp_code = (guint8)atoi(vcp_code_str);
    guint16 vcp_new_value = vcp_new_value_str ? (guint16)atoi(vcp_new_value_str) : 0;

    if (strcmp(display_or_edid, "edid:") == 0) {
        edid_txt = display_or_edid + 5;
    } else {
        display_number = atoi(display_or_edid);
    }

    GError *error = NULL;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!connection) {
        g_printerr("Error connecting to session bus: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    if (strcmp(method, "SetVcp") == 0) {
        if (vcp_new_value_str == NULL) {
            g_printerr("Usage for SetVcp: %s SetVcp <display_number or edid> <vcp_code> <vcp_new_value>\n", argv[0]);
            return 1;
        }
        call_set_vcp(connection, display_number, edid_txt, vcp_code, vcp_new_value);
    } else if (strcmp(method, "GetVcp") == 0) {
        call_get_vcp(connection, display_number, edid_txt, vcp_code);
    } else {
        g_printerr("Unknown method: %s\n", method);
        return 1;
    }

    g_object_unref(connection);
    return 0;
}
