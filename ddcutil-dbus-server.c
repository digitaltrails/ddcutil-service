#include <gio/gio.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <stdlib.h>

#include <ddcutil_c_api.h>
#include <ddcutil_status_codes.h>
#include <assert.h>
#include <string.h>

/* ----------------------------------------------------------------------------------------------------
 * Code structure based on https://github.com/bratsche/glib/blob/master/gio/tests/gdbus-example-server.c
 */

static GDBusNodeInfo *introspection_data = NULL;

/* Introspection data for the service we are exporting */
static const gchar introspection_xml[] =

    "<node>"
    "  <interface name='com.ddcutil.libddcutil.DdcutilInterface'>"

    "    <method name='Detect'>"
    "      <arg name='number_of_displays' type='i' direction='out'/>"
    "      <arg name='display_properties' type='aa{sv}' direction='out'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"

    "    <method name='GetVcp'>"
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
    "      <arg name='display_number' type='i' direction='in'/>"
    "      <arg name='edid_hex' type='s' direction='in'/>"
    "      <arg name='vcp_code' type='y' direction='in'/>"
    "      <arg name='vcp_new_value' type='q' direction='in'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"
    
    "    <method name='GetVcpMetadata'>"
    "      <arg name='display_number' type='i' direction='in'/>"
    "      <arg name='edid_hex' type='s' direction='in'/>"
    "      <arg name='vcp_code' type='y' direction='in'/>"
    "      <arg name='feature_name' type='s' direction='out'/>"
    "      <arg name='feature_description' type='s' direction='out'/>"
    "      <arg name='is_read_only' type='b' direction='out'/>"
    "      <arg name='is_write_only' type='b' direction='out'/>"
    "      <arg name='is_rw' type='b' direction='out'/>"
    "      <arg name='is_complex' type='b' direction='out'/>"
    "      <arg name='is_continuous' type='b' direction='out'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"
    
    "    <method name='GetCapabilitiesString'>"
    "      <arg name='display_number' type='i' direction='in'/>"
    "      <arg name='edid_hex' type='s' direction='in'/>"
    "      <arg name='capabilities_text' type='s' direction='out'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"
    
    "    <method name='GetCapabilitiesMetadata'>"
    "      <arg name='display_number' type='i' direction='in'/>"
    "      <arg name='edid_hex' type='s' direction='in'/>"
    "      <arg name='model' type='s' direction='out'/>"
    "      <arg name='mccs_major' type='y' direction='out'/>"
    "      <arg name='mccs_minor' type='y' direction='out'/>"
    "      <arg name='commands' type='a{ys}' direction='out'/>"
    "      <arg name='capabilities' type='a{y(ssa{ys})}' direction='out'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"

    "    <property type='d' name='Verify' access='readwrite'/>"
    "    <property type='d' name='SleepMultiplier' access='readwrite'/>"
    "    <property type='s' name='DdcutilVersionString' access='read'/>"
    
    "  </interface>"
    "</node>";

/* ----------------------------------------------------------------------------------------------------
 */

static char *edid_to_hex(const uint8_t *edid) {
  _Thread_local static char hex_edid[512];
  gchar *ptr = &hex_edid[0];
  for (int i = 0; i < 128; i++) {
    ptr += sprintf(ptr, "%02X", edid[i]);
  }
  return hex_edid;
}

static char *get_status_message(const DDCA_Status status) {
  const char *status_text = ddca_rc_name(status);
  char *message_text = NULL;
  if (status != 0) {
    DDCA_Error_Detail *error_detail = ddca_get_error_detail();
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

static DDCA_Status get_display_info(const int display_number, const char *hex_edid,
                            DDCA_Display_Info_List **dlist, DDCA_Display_Info **dinfo) {
    DDCA_Status status = ddca_get_display_info_list2(0, dlist);
    if (status == 0) {
      for (int ndx = 0; ndx < (*dlist)->ct; ndx++) {
        if (display_number == (*dlist)->info[ndx].dispno) {
          *dinfo = &((*dlist)->info[ndx]);
          break;
        }
        if (hex_edid != NULL && strcmp(hex_edid, edid_to_hex((*dlist)->info[ndx].edid_bytes)) == 0) {
          *dinfo = &((*dlist)->info[ndx]);
          break;
        }
      }
      if (*dinfo == NULL) {
        g_printf("Bad display ID %d %s?\n", display_number, hex_edid);
        status = DDCRC_INVALID_DISPLAY;
      }
    }
    return status;
}

static void detect(GDBusMethodInvocation* invocation) {
  DDCA_Display_Info_List *dlist = NULL;
  const DDCA_Status status = ddca_get_display_info_list2(0, &dlist);
  char *message_text = get_status_message(status);
  g_printf("DdcDetect ddca_get_display_info_list2() done. dlist=%p %s\n", dlist, message_text);
  // see https://docs.gtk.org/glib/struct.VariantBuilder.html
  GVariantBuilder vdu_array_builder_instance;  // Allocate on the stack for easier memory management.
  GVariantBuilder *vdu_array_builder = &vdu_array_builder_instance;
  
  g_variant_builder_init(vdu_array_builder, G_VARIANT_TYPE("aa{sv}"));

  for (int ndx = 0; ndx < dlist->ct; ndx++) {
    g_printf("ndx=%d dlist->ct=%d\n", ndx, dlist->ct);
    g_variant_builder_open(vdu_array_builder, G_VARIANT_TYPE("a{sv}"));  // Open sub-container
    g_variant_builder_add(vdu_array_builder, "{sv}", "display_number", g_variant_new("i", dlist->info[ndx].dispno));
    g_variant_builder_add(vdu_array_builder, "{sv}", "usb_bus", g_variant_new("i", dlist->info[ndx].usb_bus));
    g_variant_builder_add(vdu_array_builder, "{sv}", "usb_device", g_variant_new("i", dlist->info[ndx].usb_device));
    g_variant_builder_add(vdu_array_builder, "{sv}", "manufacturer_id", g_variant_new("s", dlist->info[ndx].mfg_id));
    g_variant_builder_add(vdu_array_builder, "{sv}", "model_name", g_variant_new("s", dlist->info[ndx].model_name));
    g_variant_builder_add(vdu_array_builder, "{sv}", "serial_number", g_variant_new("s", dlist->info[ndx].sn));
    g_variant_builder_add(vdu_array_builder, "{sv}", "product_code", g_variant_new("q", dlist->info[ndx].product_code));
    g_variant_builder_add(vdu_array_builder, "{sv}", "edid_hex",
                          g_variant_new("s", edid_to_hex(dlist->info[ndx].edid_bytes)));  // might be dodgy
    g_variant_builder_close(vdu_array_builder);  // Close sub-container
  }

  GVariant *result = g_variant_new("(iaa{sv}is)", dlist->ct, vdu_array_builder, status, message_text);

  g_dbus_method_invocation_return_value(invocation, result);
  ddca_free_display_info_list(dlist);
  free(message_text);
  g_free(result);
}

static void get_vcp(GVariant* parameters, GDBusMethodInvocation* invocation) {
  int display_number;
  char *hex_edid;
  uint8_t vcp_code;

  g_variant_get(parameters, "(isy)", &display_number, &hex_edid, &vcp_code);
  g_printf("GetFeatureValue display_num=%d, edid=%s\n", display_number, hex_edid);

  uint16_t current_value = 0;
  uint16_t max_value = 0;
  char *formatted_value = NULL;

  DDCA_Display_Info_List *info_list = NULL;
  DDCA_Display_Info *vdu_info = NULL;
  DDCA_Status status = get_display_info(display_number, hex_edid, &info_list, &vdu_info);
  if (status == 0) {
    DDCA_Display_Handle disp_handle;
    ddca_open_display2(vdu_info->dref, 1, &disp_handle);
    g_printf("GetFeatureValue opened display %d\n", display_number);
    static DDCA_Non_Table_Vcp_Value valrec;
    status = ddca_get_non_table_vcp_value(disp_handle, vcp_code, &valrec);
    if (status == 0) {
      current_value = valrec.sh << 8 | valrec.sl;
      max_value = valrec.mh << 8 | valrec.ml;
      status = ddca_format_non_table_vcp_value_by_dref(vcp_code, vdu_info->dref, &valrec, &formatted_value);
    }
    ddca_close_display(disp_handle);
  }
  char *message_text = get_status_message(status);
  g_printf("status=%d message=%s\n", status, message_text);
  GVariant *result = g_variant_new("(qqsis)", current_value, max_value, formatted_value, status, message_text);
  g_dbus_method_invocation_return_value(invocation, result);
  ddca_free_display_info_list(info_list);
  free(formatted_value);
  free(message_text);
  g_free(result);
}

static void set_vcp(GVariant* parameters, GDBusMethodInvocation* invocation) {
  int display_number;
  char *hex_edid;
  uint8_t vcp_code;
  uint16_t new_value;

  g_variant_get(parameters, "(isyq)", &display_number, &hex_edid, &vcp_code, &new_value);
  g_printf("SetFeatureValue display_num=%d, edid=%s\nvcp_code=%d value=%d\n",
         display_number, hex_edid, vcp_code, new_value);

  DDCA_Display_Info_List *info_list = NULL;
  DDCA_Display_Info *vdu_info = NULL;
  DDCA_Status status = get_display_info(display_number, hex_edid, &info_list, &vdu_info);
  if (status == 0) {
    DDCA_Display_Handle disp_handle;
    status = ddca_open_display2(vdu_info->dref, 1, &disp_handle);
    if (status == 0) {
      const uint8_t low_byte = new_value & 0x00ff;
      const uint8_t high_byte = new_value >> 8;
      status = ddca_set_non_table_vcp_value(disp_handle, vcp_code, high_byte, low_byte);
      ddca_close_display(disp_handle);
    }
  }
  char *message_text = get_status_message(status);
  GVariant *result = g_variant_new("(is)", status, message_text);
  g_dbus_method_invocation_return_value(invocation, result);
  ddca_free_display_info_list(info_list);
  free(message_text);
  g_free (result);
}

static void get_capabilities_string(GVariant* parameters, GDBusMethodInvocation* invocation) {
  int display_number;
  char *hex_edid;
  char *caps_text = NULL;

  g_variant_get(parameters, "(is)", &display_number, &hex_edid);
  g_printf("GetCapabilities display_num=%d, edid=%s\n", display_number, hex_edid);

  DDCA_Display_Info_List *info_list = NULL;
  DDCA_Display_Info *vdu_info = NULL;
  DDCA_Display_Handle disp_handle;
  DDCA_Status status = get_display_info(display_number, hex_edid, &info_list, &vdu_info);

  if (status == 0) {
    status = ddca_open_display2(vdu_info->dref, 1, &disp_handle);
    if (status == 0) {
      status = ddca_get_capabilities_string(disp_handle, &caps_text);
    }
    ddca_close_display(disp_handle);
  }
  char *message_text = get_status_message(status);
  GVariant *result = g_variant_new("(sis)",
                                   caps_text == NULL ? "" : caps_text,
                                   status, message_text);
  g_dbus_method_invocation_return_value(invocation, result);
  ddca_free_display_info_list(info_list);
  free(caps_text);
  free(message_text);
  g_free(result);
}

static void get_capabilities_metadata(GVariant* parameters, GDBusMethodInvocation* invocation) {
  int display_number;
  char *hex_edid;
  char *caps_text = NULL;

  g_variant_get(parameters, "(is)", &display_number, &hex_edid);
  g_printf("GetCapabilitiesMetadata display_num=%d, edid=%s\n", display_number, hex_edid);

  DDCA_Display_Info_List *info_list = NULL;
  DDCA_Display_Info *vdu_info = NULL;
  DDCA_Display_Handle disp_handle;
  DDCA_Capabilities *parsed_capabilities_ptr;
  DDCA_Status status = get_display_info(display_number, hex_edid, &info_list, &vdu_info);

  uint8_t mccs_version_major = 0, mccs_version_minor = 0;
  char * vdu_model = "model";

  GVariantBuilder command_dict_builder_instance;  // Allocate on the stack for easier memory management.
  GVariantBuilder *command_dict_builder = &command_dict_builder_instance;
  g_variant_builder_init(command_dict_builder, G_VARIANT_TYPE("a{ys}"));

  GVariantBuilder feature_dict_builder_instance;  // Allocate on the stack for easier memory management.
  GVariantBuilder *feature_dict_builder = &feature_dict_builder_instance;
  g_variant_builder_init(feature_dict_builder, G_VARIANT_TYPE("a{y(ssa{ys})}"));

  if (status == 0) {
    vdu_model = vdu_info->model_name;
    status = ddca_open_display2(vdu_info->dref, 1, &disp_handle);
    if (status == 0) {
      status = ddca_get_capabilities_string(disp_handle, &caps_text);
      if (status == 0) {
        status = ddca_parse_capabilities_string(caps_text, &parsed_capabilities_ptr);

        if (status == 0) {
          DDCA_Cap_Vcp *vcp_feature_array = parsed_capabilities_ptr->vcp_codes;
          g_printf("vcp_code_ct=%d\n", parsed_capabilities_ptr->vcp_code_ct);

          mccs_version_major = parsed_capabilities_ptr->version_spec.major;
          mccs_version_minor = parsed_capabilities_ptr->version_spec.minor;
          for (int command_idx = 0; command_idx < parsed_capabilities_ptr->cmd_ct; command_idx++) {
            const char *command_desc = g_strdup_printf("desc of %d", parsed_capabilities_ptr->cmd_codes[command_idx]);
            //ddca_cmd_code_name();
            g_printf("CommandDef %x %s \n", parsed_capabilities_ptr->cmd_codes[command_idx], command_desc);
            g_variant_builder_add(command_dict_builder, "{ys}", parsed_capabilities_ptr->cmd_codes[command_idx], command_desc);
          }

          for (int feature_idx = 0; feature_idx < parsed_capabilities_ptr->vcp_code_ct; feature_idx++) {
            const DDCA_Cap_Vcp feature_def = vcp_feature_array[feature_idx];
            DDCA_Feature_Metadata *metadata_ptr;

            status = ddca_get_feature_metadata_by_dh(feature_def.feature_code, disp_handle, true, &metadata_ptr);
            if (status == 0) {
              g_printf("FeatureDef: %x %s %s\n",
                       metadata_ptr->feature_code, metadata_ptr->feature_name, metadata_ptr->feature_desc);
              GVariantBuilder value_dict_builder_instance;  // Allocate on the stack for easier memory management.
              GVariantBuilder *value_dict_builder = &value_dict_builder_instance;
              g_variant_builder_init(value_dict_builder, G_VARIANT_TYPE("a{ys}"));
              for (int value_idx = 0; value_idx < feature_def.value_ct; value_idx++) {
                if (metadata_ptr->sl_values != NULL) {
                  for (DDCA_Feature_Value_Entry *sl_ptr = metadata_ptr->sl_values; sl_ptr->value_code != 0; sl_ptr++) {
                    if (sl_ptr->value_code == feature_def.values[value_idx]) {
                      g_printf("  ValueDef feature %x value %d %s\n",
                               feature_def.feature_code, sl_ptr->value_code, sl_ptr->value_name);
                      g_variant_builder_add(value_dict_builder, "{ys}", sl_ptr->value_code, sl_ptr->value_name);
                    }
                  }
                }
              }
              g_variant_builder_add(
                feature_dict_builder,
                "{y(ssa{ys})}",
                metadata_ptr->feature_code,
                metadata_ptr->feature_name,
                metadata_ptr->feature_desc == NULL ? "" : metadata_ptr->feature_desc,
                value_dict_builder);
              free(metadata_ptr);
            }
            else {
              g_printf("%x %s\n", feature_def.feature_code, get_status_message(status));
            }
          }
        }
      }
    }
    ddca_close_display(disp_handle);
  }

  char *message_text = get_status_message(status);
  GVariant *result = g_variant_new("(syya{ys}a{y(ssa{ys})}is)",
                                   vdu_model,
                                   mccs_version_major,
                                   mccs_version_minor,
                                   command_dict_builder,
                                   feature_dict_builder,
                                   status, message_text);
  g_dbus_method_invocation_return_value(invocation, result);
  ddca_free_display_info_list(info_list);
  ddca_free_parsed_capabilities(parsed_capabilities_ptr);
  free(caps_text);
  free(message_text);
  g_free(result);
}

static void get_vcp_metadata(GVariant* parameters, GDBusMethodInvocation* invocation) {
  int display_number;
  char *hex_edid;
  uint8_t vcp_code;

  g_variant_get(parameters, "(isy)", &display_number, &hex_edid, &vcp_code);
  g_printf("GetFeatureMetadata display_num=%d, edid=%s\nvcp_code=%d\n", display_number, hex_edid, vcp_code);

  DDCA_Display_Info_List *info_list = NULL;
  DDCA_Display_Info *vdu_info = NULL;
  DDCA_Status status = get_display_info(display_number, hex_edid, &info_list, &vdu_info);
  char *feature_name = "";
  char *feature_description= "";
  bool is_read_only = false;
  bool is_write_only = false;
  bool is_rw = false;
  bool is_complex = false;
  bool is_continuous = false;
  DDCA_Feature_Metadata *metadata_ptr = NULL;
  if (status == 0) {
    DDCA_Display_Handle disp_handle;
    status = ddca_open_display2(vdu_info->dref, 1, &disp_handle);
    if (status == 0) {
      status = ddca_get_feature_metadata_by_dh(vcp_code, disp_handle, 0, &metadata_ptr);
      if (status == 0) {
        if (metadata_ptr->feature_name != NULL) {
          feature_name = metadata_ptr->feature_name;
        }
        if (metadata_ptr->feature_desc != NULL) {
          feature_description = metadata_ptr->feature_desc;
        }
        if (metadata_ptr->sl_values != NULL) {
          for (DDCA_Feature_Value_Entry *sl_ptr = metadata_ptr->sl_values; sl_ptr->value_code != 0; sl_ptr++) {
            g_printf("%d %s\n", sl_ptr->value_code, sl_ptr->value_name);
          }
        }
        is_read_only = metadata_ptr->feature_flags & DDCA_RO;
        is_write_only = metadata_ptr->feature_flags & DDCA_WO;
        is_rw = metadata_ptr->feature_flags & DDCA_RW;
        is_complex = metadata_ptr->feature_flags & (DDCA_COMPLEX_CONT | DDCA_COMPLEX_NC);
        is_continuous = metadata_ptr->feature_flags & DDCA_CONT;
        ddca_close_display(disp_handle);
      }
    }
  }
  char *message_text = get_status_message(status);
  GVariant *result = g_variant_new("(ssbbbbbis)",
                                   feature_name, feature_description,
                                   is_read_only, is_write_only, is_rw, is_complex, is_continuous,
                                   status, status == 0 ? "OK" : message_text);
  g_dbus_method_invocation_return_value(invocation, result);
  ddca_free_display_info_list(info_list);
  free(metadata_ptr);
  free(message_text);
  g_free(result);
}

static void handle_method_call(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                               const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                               GDBusMethodInvocation *invocation, gpointer user_data) {
  if (g_strcmp0(method_name, "Detect") == 0) {
    detect(invocation);
  } else if (g_strcmp0(method_name, "GetVcp") == 0) {
    get_vcp(parameters, invocation);
  } else if (g_strcmp0(method_name, "SetVcp") == 0) {
    set_vcp(parameters, invocation);
  } else if (g_strcmp0(method_name, "GetVcpMetadata") == 0) {
    get_vcp_metadata(parameters, invocation);
  } else if (g_strcmp0(method_name, "GetCapabilitiesString") == 0) {
    get_capabilities_string(parameters, invocation);
  } else if (g_strcmp0(method_name, "GetCapabilitiesMetadata") == 0) {
    get_capabilities_metadata(parameters, invocation);
  }
}

static GVariant *handle_get_property(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                     const gchar *interface_name, const gchar *property_name, GError **error,
                                     gpointer user_data) {
  GVariant *ret = NULL;
  if (g_strcmp0(property_name, "DdcutilVersionString") == 0) {
    ret = g_variant_new_string(g_strdup(ddca_ddcutil_extended_version_string()));
  }
  else if (g_strcmp0(property_name, "Verify") == 0) {
    ret = g_variant_new_boolean(ddca_is_verify_enabled());
  } 
  else if (g_strcmp0(property_name, "SleepMultiplier") == 0) {
    ret = g_variant_new_double(ddca_get_sleep_multiplier());
  }
  return ret;
}

static gboolean handle_set_property(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                    const gchar *interface_name, const gchar *property_name, GVariant *value,
                                    GError **error, gpointer user_data) {
  if (g_strcmp0(property_name, "Verify") == 0) {
    ddca_enable_verify(g_variant_get_boolean(value));
  }
  else if (g_strcmp0(property_name, "SleepMultiplier") == 0) {
    ddca_set_sleep_multiplier(g_variant_get_double(value));
  } 
  return *error == NULL;
}

/* GDBUS service handlers */
static const GDBusInterfaceVTable interface_vtable = {handle_method_call, handle_get_property, handle_set_property};

static void on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  const char* object_path = "/com/ddcutil/libddcutil/DdcutilObject";
  const guint registration_id =
    g_dbus_connection_register_object(connection,
                                      object_path,
                                      introspection_data->interfaces[0],
                                      &interface_vtable, NULL, /* user_data */
                                      NULL,                    /* user_data_free_func */
                                      NULL);                   /* GError** */
  g_assert(registration_id > 0);
  g_print("Registered %s\n", object_path);
}

static void on_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  g_print("Name acquired %s\n", name);
}

static void on_name_lost(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  g_print("Lost registration - is another instance already registered?\n");
  exit(1);
}

int main(int argc, char *argv[]) {

  /* Build introspection data structures from XML.
   */
  introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
  g_assert(introspection_data != NULL);

  const guint owner_id = g_bus_own_name(
    G_BUS_TYPE_SESSION,
    "com.ddcutil.libddcutil.DdcutilServer",
    G_BUS_NAME_OWNER_FLAGS_NONE,
    on_bus_acquired,
    on_name_acquired,
    on_name_lost, NULL,
    NULL);

  GMainLoop *loop = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(loop);
  g_bus_unown_name(owner_id);
  g_dbus_node_info_unref(introspection_data);
  return 0;
}
