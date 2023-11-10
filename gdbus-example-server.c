#include <gio/gio.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <stdlib.h>

#include "ddcutil_c_api.h"
#include "ddcutil_status_codes.h"
#include <assert.h>
#include <string.h>

/* ----------------------------------------------------------------------------------------------------
 */

static GDBusNodeInfo *introspection_data = NULL;

/* Introspection data for the service we are exporting */
static const gchar introspection_xml[] =

    "<node>"
    "  <interface name='org.gtk.GDBus.TestInterface'>"
    "    <annotation name='org.gtk.GDBus.Annotation' value='OnInterface'/>"
    "    <annotation name='org.gtk.GDBus.Annotation' value='AlsoOnInterface'/>"

    "    <method name='DdcDetect'>"
    "      <annotation name='org.gtk.GDBus.Annotation' value='OnMethod'/>"
    "      <arg name='number_of_displays' type='i' direction='out'/>"
    "      <arg name='display_properties' type='aa{sv}' direction='out'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"

    "    <method name='GetVcp'>"
    "      <annotation name='org.gtk.GDBus.Annotation' value='OnMethod'/>"
    "      <arg name='display_number' type='i' direction='in'/>"
    "      <arg name='edid_hex' type='s' direction='in'/>"
    "      <arg name='vcp_code' type='y' direction='in'/>"
    "      <arg name='vcp_current_value' type='q' direction='out'/>"
    "      <arg name='vcp_max_value' type='q' direction='out'/>"
    "      <arg name='vcp_formatted_value' type='s' direction='out'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"

    "    <method name='SetVcp'>"
    "      <annotation name='org.gtk.GDBus.Annotation' value='OnMethod'/>"
    "      <arg name='display_number' type='i' direction='in'/>"
    "      <arg name='edid_hex' type='s' direction='in'/>"
    "      <arg name='vcp_code' type='y' direction='in'/>"
    "      <arg name='vcp_new_value' type='q' direction='in'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"

    "    <property type='s' name='FluxCapicitorName' access='read'>"
    "      <annotation name='org.gtk.GDBus.Annotation' value='OnProperty'>"
    "        <annotation name='org.gtk.GDBus.Annotation' "
    "value='OnAnnotation_YesThisIsCrazy'/>"
    "      </annotation>"
    "    </property>"
    "    <property type='s' name='Title' access='readwrite'/>"
    "    <property type='s' name='ReadingAlwaysThrowsError' access='read'/>"
    "    <property type='s' name='WritingAlwaysThrowsError' "
    "access='readwrite'/>"
    "    <property type='s' name='OnlyWritable' access='write'/>"
    "    <property type='s' name='Foo' access='read'/>"
    "    <property type='s' name='Bar' access='read'/>"
    "  </interface>"
    "</node>";

/* ----------------------------------------------------------------------------------------------------
 */

char *edid_to_hex(uint8_t *edid) {
  static char hex_edid[512];
  gchar *ptr = &hex_edid[0];
  for (int i = 0; i < 128; i++) {
    ptr += sprintf(ptr, "%02X", edid[i]);
  }
  return hex_edid;
}

char *get_status_message(DDCA_Status status) {
  char *message_text = "OK";
  if (status != 0) {
    const char *status_text = ddca_rc_name(status);
    const char *detail_text = ddca_get_error_detail()->detail;
    message_text = g_malloc(strlen(status_text) + (detail_text == NULL ? 0 : strlen(detail_text)) + 3);
    strcat(message_text, status_text);
    strcat(message_text, ": ");
    strcat(message_text, detail_text);
  }
  return message_text;
}

DDCA_Status get_dref(const int display_number, const char *hex_edid, DDCA_Display_Ref **dref) {
    printf("display_num=%d, edid=%s\n", display_number, hex_edid);
    DDCA_Display_Info_List *dlist = NULL;
    DDCA_Status status = ddca_get_display_info_list2(0, &dlist);
    if (status == 0) {
      for (int ndx = 0; ndx < dlist->ct; ndx++) {
        if (display_number == dlist->info[ndx].dispno) {
          *dref = dlist->info[ndx].dref;
          break;
        } else if (hex_edid != NULL && strcmp(hex_edid, edid_to_hex(dlist->info[ndx].edid_bytes)) == 0) {
          *dref = dlist->info[ndx].dref;
          break;
        }
      }
      if (*dref == NULL) {
        printf("Bad display ID?\n");
        status = DDCRC_INVALID_DISPLAY;
      }
    }
    return status;
}


static void handle_method_call(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                               const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                               GDBusMethodInvocation *invocation, gpointer user_data) {

  if (g_strcmp0(method_name, "DdcDetect") == 0) {  // =======================
    
    DDCA_Display_Info_List *dlist = NULL;
    const DDCA_Status status = ddca_get_display_info_list2(0, &dlist);
    const char *message_text = get_status_message(status);
    printf("   ddca_get_display_info_list2() done. dlist=%p %s\n", dlist, message_text);

    
    GVariantBuilder *vdu_array_builder = g_variant_builder_new(G_VARIANT_TYPE("aa{sv}"));

    for (int ndx = 0; ndx < dlist->ct; ndx++) {
      printf("ndx=%d dlist->ct=%d\n", ndx, dlist->ct);
      GVariantBuilder *vdu_properties_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_add(vdu_properties_builder, "{sv}", "display_number", g_variant_new("i", dlist->info[ndx].dispno));
      g_variant_builder_add(vdu_properties_builder, "{sv}", "usb_bus", g_variant_new("i", dlist->info[ndx].usb_bus));
      g_variant_builder_add(vdu_properties_builder, "{sv}", "usb_device", g_variant_new("i", dlist->info[ndx].usb_device));
      g_variant_builder_add(vdu_properties_builder, "{sv}", "manufacturer_id", g_variant_new("s", dlist->info[ndx].mfg_id));
      g_variant_builder_add(vdu_properties_builder, "{sv}", "model_name", g_variant_new("s", dlist->info[ndx].model_name));
      g_variant_builder_add(vdu_properties_builder, "{sv}", "serial_number", g_variant_new("s", dlist->info[ndx].sn));
      g_variant_builder_add(vdu_properties_builder, "{sv}", "product_code", g_variant_new("q", dlist->info[ndx].product_code));
      g_variant_builder_add(
        vdu_properties_builder, "{sv}", "edid_hex", g_variant_new("s", edid_to_hex(dlist->info[ndx].edid_bytes)));
      g_variant_builder_add(vdu_array_builder, "a{sv}", vdu_properties_builder);
    }

    GVariant *result = g_variant_new("(iaa{sv}is)", dlist->ct, vdu_array_builder, status, message_text);

    g_dbus_method_invocation_return_value(invocation, result);
    // g_free (result);
    
  } else if (g_strcmp0(method_name, "GetVcp") == 0) {  // =======================
    int display_number;
    char *hex_edid;
    uint8_t vcp_code;
    int status = 0;
    char *message_text = "";
    
    g_variant_get(parameters, "(isy)", &display_number, &hex_edid, &vcp_code);
    printf("display_num=%d, edid=%s\n", display_number, hex_edid);

    uint16_t current_value = 0;
    uint16_t max_value = 0;
    char *formatted_value = "";
    
    DDCA_Display_Ref *dref = NULL;
    status = get_dref(display_number, hex_edid, &dref);
    if (status == 0) {
      DDCA_Display_Handle disp_handle;
      ddca_open_display2(dref, 1, &disp_handle);
      printf("getvcp opened display %d\n", display_number);
      static DDCA_Non_Table_Vcp_Value valrec;
      status = ddca_get_non_table_vcp_value(disp_handle, vcp_code, &valrec);
      if (status == 0) {
        current_value = valrec.sh << 8 | valrec.sl;
        max_value = valrec.mh << 8 | valrec.ml;
        status = ddca_format_non_table_vcp_value_by_dref(vcp_code, dref, &valrec, &formatted_value);
      }
      message_text = get_status_message(status);
      ddca_close_display(disp_handle);
      printf("getvcp closed display %d\n", display_number);
    }
    
    printf("status=%d\n", status);
    GVariant *result = g_variant_new("(qqsis)", current_value, max_value, formatted_value, status, message_text);
    g_dbus_method_invocation_return_value(invocation, result);
    // g_free (result);
    
  } else if (g_strcmp0(method_name, "SetVcp") == 0) {  // =======================
    int display_number;
    char *hex_edid;
    uint8_t vcp_code;
    uint16_t new_value;
    int status = 0;
    char *message_text = "";
    
    g_variant_get(parameters, "(isyq)", &display_number, &hex_edid, &vcp_code, &new_value);
    printf("display_num=%d, edid=%s\nvcp_code=%d value=%d\n", display_number, hex_edid, vcp_code, new_value);
    
    DDCA_Display_Ref *dref = NULL;
    status = get_dref(display_number, hex_edid, &dref);
    if (status == 0) {
      DDCA_Display_Handle disp_handle;
      status = ddca_open_display2(dref, 1, &disp_handle);
      if (status == 0) {
        printf("setvcp opened display %d\n", display_number);      
        uint8_t low_byte = new_value & 0x00ff; 
        uint8_t high_byte = new_value >> 8;
        status = ddca_set_non_table_vcp_value(disp_handle, vcp_code, high_byte, low_byte);
        ddca_close_display(disp_handle);
        printf("setvcp closed display %d\n", display_number);
      }
      message_text = get_status_message(status);
    }
    
    GVariant *result = g_variant_new("(is)", status, status == 0 ? "OK" : message_text);
    g_dbus_method_invocation_return_value(invocation, result);
    // g_free (result);
  }
}

static gchar *_global_title = NULL;

static gboolean swap_a_and_b = FALSE;

static GVariant *handle_get_property(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                     const gchar *interface_name, const gchar *property_name, GError **error,
                                     gpointer user_data) {
  GVariant *ret;

  ret = NULL;
  if (g_strcmp0(property_name, "FluxCapicitorName") == 0) {
    ret = g_variant_new_string("DeLorean");
  } else if (g_strcmp0(property_name, "Title") == 0) {
    if (_global_title == NULL)
      _global_title = g_strdup("Back To C!");
    ret = g_variant_new_string(_global_title);
  } else if (g_strcmp0(property_name, "ReadingAlwaysThrowsError") == 0) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Hello %s. I thought I said reading this property "
                "always results in an error. kthxbye",
                sender);
  } else if (g_strcmp0(property_name, "WritingAlwaysThrowsError") == 0) {
    ret = g_variant_new_string("There's no home like home");
  } else if (g_strcmp0(property_name, "Foo") == 0) {
    ret = g_variant_new_string(swap_a_and_b ? "Tock" : "Tick");
  } else if (g_strcmp0(property_name, "Bar") == 0) {
    ret = g_variant_new_string(swap_a_and_b ? "Tick" : "Tock");
  }

  return ret;
}

static gboolean handle_set_property(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                    const gchar *interface_name, const gchar *property_name, GVariant *value,
                                    GError **error, gpointer user_data) {
  if (g_strcmp0(property_name, "Title") == 0) {
    if (g_strcmp0(_global_title, g_variant_get_string(value, NULL)) != 0) {
      GVariantBuilder *builder;
      GError *local_error;

      g_free(_global_title);
      _global_title = g_variant_dup_string(value, NULL);

      local_error = NULL;
      builder = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
      g_variant_builder_add(builder, "{sv}", "Title", g_variant_new_string(_global_title));
      g_dbus_connection_emit_signal(connection, NULL, object_path, "org.freedesktop.DBus.Properties",
                                    "PropertiesChanged", g_variant_new("(sa{sv}as)", interface_name, builder, NULL),
                                    &local_error);
      g_assert_no_error(local_error);
    }
  } else if (g_strcmp0(property_name, "ReadingAlwaysThrowsError") == 0) {
    /* do nothing - they can't read it after all! */
  } else if (g_strcmp0(property_name, "WritingAlwaysThrowsError") == 0) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Hello AGAIN %s. I thought I said writing this property "
                "always results in an error. kthxbye",
                sender);
  }

  return *error == NULL;
}

/* for now */
static const GDBusInterfaceVTable interface_vtable = {handle_method_call, handle_get_property, handle_set_property};

/* ----------------------------------------------------------------------------------------------------
 */

static void on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  guint registration_id;

  registration_id =
      g_dbus_connection_register_object(connection, "/org/gtk/GDBus/TestObject", introspection_data->interfaces[0],
                                        &interface_vtable, NULL, /* user_data */
                                        NULL,                    /* user_data_free_func */
                                        NULL);                   /* GError** */
  g_assert(registration_id > 0);
}

static void on_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {}

static void on_name_lost(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  g_print("Lost\n");
  exit(1);
}

int main(int argc, char *argv[]) {
  guint owner_id;
  GMainLoop *loop;

  /* We are lazy here - we don't want to manually provide
   * the introspection data structures - so we just build
   * them from XML.
   */
  introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
  g_assert(introspection_data != NULL);

  owner_id = g_bus_own_name(G_BUS_TYPE_SESSION, "org.gtk.GDBus.TestServer", G_BUS_NAME_OWNER_FLAGS_NONE,
                            on_bus_acquired, on_name_acquired, on_name_lost, NULL, NULL);

  loop = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(loop);

  g_bus_unown_name(owner_id);

  g_dbus_node_info_unref(introspection_data);

  return 0;
}
