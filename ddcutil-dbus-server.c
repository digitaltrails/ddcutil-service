#include <gio/gio.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <assert.h>


#include <ddcutil_c_api.h>
#include <ddcutil_status_codes.h>
#include <ddcutil_macros.h>


/* ----------------------------------------------------------------------------------------------------
 * ddcutil-dbus-server.c
 * ---------------------
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

#define DDCUTIL_SERVICE_VERSION_STRING "1.0.0"

/*
 * Demonstrations of two ways of raising d-bus signals:
 */
#define COMPILE_DEMO_SIGNAL_CONNECTION_TIMER
#undef COMPILE_DEMO_SIGNAL_MAIN_LOOP_CUSTOM_SOURCE


static GDBusNodeInfo *introspection_data = NULL;

/* Introspection data for the service we are exporting */
static const gchar introspection_xml[] =

    "<node>"
    "  <interface name='com.ddcutil.DdcutilInterface'>"

    "    <method name='Restart'>"
    "      <arg name='text_options' type='s' direction='in'/>"
    "      <arg name='syslog_level' type='u' direction='in'/>"
    "      <arg name='flags' type='u' direction='in'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"

    "    <method name='Detect'>"
    "      <arg name='flags' type='u' direction='in'/>"
    "      <arg name='number_of_displays' type='i' direction='out'/>"
    "      <arg name='detected_displays' type='a(iiisssqsu)' direction='out'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"

    "    <method name='GetVcp'>"
    "      <arg name='display_number' type='i' direction='in'/>"
    "      <arg name='edid_txt' type='s' direction='in'/>"
    "      <arg name='vcp_code' type='y' direction='in'/>"
    "      <arg name='flags' type='u' direction='in'/>"
    "      <arg name='vcp_current_value' type='q' direction='out'/>"
    "      <arg name='vcp_max_value' type='q' direction='out'/>"
    "      <arg name='vcp_formatted_value' type='s' direction='out'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"

    "    <method name='GetMultipleVcp'>"
    "      <arg name='display_number' type='i' direction='in'/>"
    "      <arg name='edid_txt' type='s' direction='in'/>"
    "      <arg name='vcp_code' type='ay' direction='in'/>"
    "      <arg name='flags' type='u' direction='in'/>"
    "      <arg name='vcp_current_value' type='a(yqqs)' direction='out'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"

    "    <method name='SetVcp'>"
    "      <arg name='display_number' type='i' direction='in'/>"
    "      <arg name='edid_txt' type='s' direction='in'/>"
    "      <arg name='vcp_code' type='y' direction='in'/>"
    "      <arg name='vcp_new_value' type='q' direction='in'/>"
    "      <arg name='flags' type='u' direction='in'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"
    
    "    <method name='GetVcpMetadata'>"
    "      <arg name='display_number' type='i' direction='in'/>"
    "      <arg name='edid_txt' type='s' direction='in'/>"
    "      <arg name='vcp_code' type='y' direction='in'/>"
    "      <arg name='flags' type='u' direction='in'/>"
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
    "      <arg name='edid_txt' type='s' direction='in'/>"
    "      <arg name='flags' type='u' direction='in'/>"
    "      <arg name='capabilities_text' type='s' direction='out'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"
    
    "    <method name='GetCapabilitiesMetadata'>"
    "      <arg name='display_number' type='i' direction='in'/>"
    "      <arg name='edid_txt' type='s' direction='in'/>"
    "      <arg name='flags' type='u' direction='in'/>"
    "      <arg name='model_name' type='s' direction='out'/>"
    "      <arg name='mccs_major' type='y' direction='out'/>"
    "      <arg name='mccs_minor' type='y' direction='out'/>"
    "      <arg name='commands' type='a{ys}' direction='out'/>"
    "      <arg name='capabilities' type='a{y(ssa{ys})}' direction='out'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"

    "    <signal name='ConnectedDisplaysChanged'>"
    "      <arg type='i' name='count'/>"
    "      <arg type='u' name='flags'/>"
    "    </signal>"

    "    <property type='d' name='Verify' access='readwrite'/>"
    "    <property type='d' name='SleepMultiplier' access='readwrite'/>"
    "    <property type='s' name='DdcutilVersionString' access='read'/>"
    "    <property type='s' name='InterfaceVersionString' access='read'/>"
    "    <property type='as' name='AttributesReturnedByDetect' access='read'/>"
    "    <property type='a{is}' name='StatusValues' access='read'/>"
    "    <property type='y' name='OutputLevel' access='readwrite'/>"

    "  </interface>"
    "</node>";

/* ----------------------------------------------------------------------------------------------------
 */

static const char *attributes_returned_from_detect[] = {
  "display_number", "usb_bus", "usb_device",
  "manufacturer_id", "model_name", "serial_number", "product_code",
  "edid_txt", "binary_serial_number",
  NULL
};

static char *edid_encode(const uint8_t *edid) {
  return g_base64_encode(edid, 128);
}

char *server_executable = "ddcutil-dbus-server";

static uint32_t edid_to_binary_serial_number(const uint8_t *edid_bytes) {
  const uint32_t binary_serial =
    edid_bytes[0x0c]       |
    edid_bytes[0x0d] <<  8 |
    edid_bytes[0x0e] << 16 |
    edid_bytes[0x0f] << 24;
  return binary_serial;
}

static gchar *sanitize_utf8(const char *text) {
  gchar *result = strdup(text);
  const char *ptr = result, *end = ptr + strlen(result);
  while (true) {
    const char *ptr2;
    g_utf8_validate(ptr, end - ptr, &ptr2);
    if (ptr2 == end)
      break;
    result[ptr2 - result] = '?';  // Sanitize invalid utf-8
    ptr = ptr2 + 1;
  }
  return result;
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
        gchar *dlist_edid_encoded = edid_encode((*dlist)->info[ndx].edid_bytes);
        if (hex_edid != NULL && strcmp(hex_edid, dlist_edid_encoded) == 0) {
          *dinfo = &((*dlist)->info[ndx]);
          free(dlist_edid_encoded);
          break;
        }
        free(dlist_edid_encoded);
      }
      if (*dinfo == NULL) {
        g_printf("Bad display ID %d %s?\n", display_number, hex_edid);
        status = DDCRC_INVALID_DISPLAY;
      }
    }
    return status;
}

extern char** environ;
static void restart(GVariant* parameters, GDBusMethodInvocation* invocation) {
  char *libopts;
  u_int32_t syslog_level;
  u_int32_t opts;
  g_variant_get(parameters, "(suu)", &libopts, &syslog_level, &opts);

  g_printf("DdcaInit syslog_level=%x opts=%x libopts=%s\n", syslog_level, opts, libopts);

  char *message_text = get_status_message(DDCRC_OK);
  g_printf("DdcaInit status=%d message=%s\n", DDCRC_OK, message_text);

  GVariant *result = g_variant_new("(is)", DDCRC_OK, message_text);
  g_dbus_method_invocation_return_value(invocation, result);

  gchar** argv;
#if DDCUTIL_VMAJOR >= 2
  gchar *args_str = "";
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
  struct sigaction arg = {
    .sa_handler=SIG_IGN,
    .sa_flags=SA_NOCLDWAIT // Never wait for termination of a child process.
  };
  sigaction(SIGCHLD, &arg, NULL);

  pid_t spawn_pid;
  posix_spawn(&spawn_pid, server_executable, NULL, NULL, argv,environ);
  exit(0);
}


static void detect(GVariant* parameters, GDBusMethodInvocation* invocation) {
  u_int32_t flags;
  g_variant_get(parameters, "(u)", &flags);

  g_printf("Detect flags=%x\n", flags);

  const DDCA_Status detect_status = ddca_redetect_displays();
  char *detect_message_text = get_status_message(detect_status);

  DDCA_Display_Info_List *dlist = NULL;
  const DDCA_Status list_status = ddca_get_display_info_list2(flags != 0, &dlist);
  char *list_message_text = get_status_message(list_status);

  g_printf("Detect status=%d message=%s\n", list_status, list_message_text);
  // see https://docs.gtk.org/glib/struct.VariantBuilder.html

  GVariantBuilder detected_displays_builder_instance;  // Allocate on the stack for easier memory management.
  GVariantBuilder *detected_displays_builder = &detected_displays_builder_instance;

  g_variant_builder_init(detected_displays_builder, G_VARIANT_TYPE("a(iiisssqsu)"));
  for (int ndx = 0; ndx < dlist->ct; ndx++) {
    DDCA_Display_Info *vdu_info = &dlist->info[ndx];
    gchar *safe_mfg_id = sanitize_utf8(vdu_info->mfg_id);
    gchar *safe_model = sanitize_utf8(vdu_info->model_name);//"xxxxwww\xF0\xA4\xADiii" );
    gchar *safe_sn = sanitize_utf8(vdu_info->sn);
    gchar *edid_encoded = edid_encode(vdu_info->edid_bytes);
    g_printf("Detected %s %s %s\n", safe_mfg_id, safe_model, safe_sn);

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

  const int final_status = (detect_status != 0) ? detect_status : list_status;
  const char *final_message_text = (detect_status != 0) ? detect_message_text : list_message_text;

  GVariant *result = g_variant_new("(ia(iiisssqsu)is)",
    dlist->ct, detected_displays_builder, final_status, final_message_text);

  g_dbus_method_invocation_return_value(invocation, result);  // Think this frees the result.
  ddca_free_display_info_list(dlist);
  free(list_message_text);
  free(detect_message_text);
}

static void get_vcp(GVariant* parameters, GDBusMethodInvocation* invocation) {
  int display_number;
  char *hex_edid;
  uint8_t vcp_code;
  u_int32_t flags;

  g_variant_get(parameters, "(isyu)", &display_number, &hex_edid, &vcp_code, &flags);
  g_printf("GetVcp vcp_code=%d display_num=%d, edid=%.30s...\n", vcp_code, display_number, hex_edid);

  uint16_t current_value = 0;
  uint16_t max_value = 0;
  char *formatted_value = NULL;

  DDCA_Display_Info_List *info_list = NULL;
  DDCA_Display_Info *vdu_info = NULL;  // pointer into info_list
  DDCA_Status status = get_display_info(display_number, hex_edid, &info_list, &vdu_info);
  if (status == 0) {
    DDCA_Display_Handle disp_handle;
    ddca_open_display2(vdu_info->dref, 1, &disp_handle);
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
  GVariant *result = g_variant_new("(qqsis)", current_value, max_value, formatted_value, status, message_text);
  g_dbus_method_invocation_return_value(invocation, result);   // Think this frees the result
  ddca_free_display_info_list(info_list);
  free(formatted_value);
  free(hex_edid);
  free(message_text);
}

static void get_multiple_vcp(GVariant* parameters, GDBusMethodInvocation* invocation) {
  int display_number;
  char *hex_edid;
  u_int32_t flags;

  GVariantIter *vcp_code_iter;
  g_variant_get(parameters, "(isayu)", &display_number, &hex_edid, &vcp_code_iter, &flags);

  g_printf("GetMultipleVcp display_num=%d, edid=%.30s...\n", display_number, hex_edid);

  const int number_of_vcp_codes = g_variant_iter_n_children(vcp_code_iter);
  const u_int8_t vcp_codes[number_of_vcp_codes];
  for (int i = 0; g_variant_iter_loop(vcp_code_iter, "y", &vcp_codes[i]); i++) {}
  g_variant_iter_free (vcp_code_iter);

  GVariantBuilder value_array_builder_instance;
  GVariantBuilder *value_array_builder = &value_array_builder_instance;
  g_variant_builder_init(value_array_builder, G_VARIANT_TYPE("a(yqqs)"));

  DDCA_Display_Info_List *info_list = NULL;
  DDCA_Display_Info *vdu_info = NULL;  // pointer into info_list
  DDCA_Status status = get_display_info(display_number, hex_edid, &info_list, &vdu_info);
  if (status == 0) {
    DDCA_Display_Handle disp_handle;
    status = ddca_open_display2(vdu_info->dref, 1, &disp_handle);
    if (status == 0) {
      for (int i = 0; i < number_of_vcp_codes; i++) {
        const u_int8_t vcp_code = vcp_codes[i];
        static DDCA_Non_Table_Vcp_Value valrec;
        status = ddca_get_non_table_vcp_value(disp_handle, vcp_code, &valrec);
        if (status == 0) {
          const uint16_t current_value = valrec.sh << 8 | valrec.sl;
          const uint16_t max_value = valrec.mh << 8 | valrec.ml;
          char *formatted_value;
          status = ddca_format_non_table_vcp_value_by_dref(vcp_code, vdu_info->dref, &valrec, &formatted_value);
          g_variant_builder_add(value_array_builder, "(yqqs)", vcp_code, current_value, max_value, formatted_value);
          free(formatted_value);
        }
        else {
          g_printf("GetMultipleVcp failed to get value for display %d vcp_code %d\n", display_number, vcp_code);
        }
      }
      ddca_close_display(disp_handle);
    }
  }
  char *message_text = get_status_message(status);
  GVariant *result = g_variant_new("(a(yqqs)is)", value_array_builder, status, message_text);
  g_dbus_method_invocation_return_value(invocation, result);   // Think this frees the result
  ddca_free_display_info_list(info_list);
  g_free(hex_edid);
  free(message_text);
}

static void set_vcp(GVariant* parameters, GDBusMethodInvocation* invocation) {
  int display_number;
  char *hex_edid;
  uint8_t vcp_code;
  uint16_t new_value;
  u_int32_t flags;

  g_variant_get(parameters, "(isyqu)", &display_number, &hex_edid, &vcp_code, &new_value, &flags);
  g_printf("SetVcp vcp_code=%d value=%d display_num=%d edid=%.30s...\n",
           vcp_code, new_value, display_number, hex_edid);

  DDCA_Display_Info_List *info_list = NULL;
  DDCA_Display_Info *vdu_info = NULL;  // pointer into info_list
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
  g_dbus_method_invocation_return_value(invocation, result);   // Think this frees the result
  ddca_free_display_info_list(info_list);
  g_free(hex_edid);
  free(message_text);
}

static void get_capabilities_string(GVariant* parameters, GDBusMethodInvocation* invocation) {
  int display_number;
  char *hex_edid;
  char *caps_text = NULL;
  u_int32_t flags;

  g_variant_get(parameters, "(isu)", &display_number, &hex_edid, &flags);
  g_printf("GetCapabilitiesString display_num=%d, edid=%.30s...\n", display_number, hex_edid);

  DDCA_Display_Info_List *info_list = NULL;
  DDCA_Display_Info *vdu_info = NULL;  // pointer into info_list
  DDCA_Display_Handle disp_handle;
  DDCA_Status status = get_display_info(display_number, hex_edid, &info_list, &vdu_info);

  if (status == 0) {
    status = ddca_open_display2(vdu_info->dref, 1, &disp_handle);
    if (status == 0) {
      status = ddca_get_capabilities_string(disp_handle, &caps_text);
      ddca_close_display(disp_handle);
    }
  }
  char *message_text = get_status_message(status);
  GVariant *result = g_variant_new("(sis)",
                                   caps_text == NULL ? "" : caps_text,
                                   status, message_text);
  g_dbus_method_invocation_return_value(invocation, result);  // Think this frees the result
  ddca_free_display_info_list(info_list);
  free(caps_text);
  g_free(hex_edid);
  free(message_text);
}

static void get_capabilities_metadata(GVariant* parameters, GDBusMethodInvocation* invocation) {
  int display_number;
  char *hex_edid;
  char *caps_text = NULL;
  u_int32_t flags;

  g_variant_get(parameters, "(isu)", &display_number, &hex_edid, &flags);
  g_printf("GetCapabilitiesMetadata display_num=%d, edid=%.30s...\n", display_number, hex_edid);

  DDCA_Display_Info_List *info_list = NULL;
  DDCA_Display_Info *vdu_info = NULL;  // pointer into info_list
  DDCA_Display_Handle disp_handle;
  DDCA_Capabilities *parsed_capabilities_ptr = NULL;
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
            char *command_desc = g_strdup_printf("desc of %d", parsed_capabilities_ptr->cmd_codes[command_idx]);
            //ddca_cmd_code_name();
            g_printf("CommandDef %x %s \n", parsed_capabilities_ptr->cmd_codes[command_idx], command_desc);
            g_variant_builder_add(command_dict_builder, "{ys}", parsed_capabilities_ptr->cmd_codes[command_idx], command_desc);
            g_free(command_desc);  // TODO is this OK, or are we freeing too early?
          }

          for (int feature_idx = 0; feature_idx < parsed_capabilities_ptr->vcp_code_ct; feature_idx++) {
            DDCA_Cap_Vcp *feature_def = vcp_feature_array + feature_idx;
            DDCA_Feature_Metadata *metadata_ptr;

            status = ddca_get_feature_metadata_by_dh(feature_def->feature_code, disp_handle, true, &metadata_ptr);  // TODO valgrind complains
            if (status == 0) {
              g_printf("FeatureDef: %x %s %s\n",
                       metadata_ptr->feature_code, metadata_ptr->feature_name, metadata_ptr->feature_desc);
              GVariantBuilder value_dict_builder_instance;  // Allocate on the stack for easier memory management.
              GVariantBuilder *value_dict_builder = &value_dict_builder_instance;
              g_variant_builder_init(value_dict_builder, G_VARIANT_TYPE("a{ys}"));
              for (int value_idx = 0; value_idx < feature_def->value_ct; value_idx++) {
                u_int8_t value_code = feature_def->values[value_idx];
                char *value_name = "";
                if (metadata_ptr->sl_values != NULL) {
                  for (DDCA_Feature_Value_Entry *sl_ptr = metadata_ptr->sl_values; sl_ptr->value_code != 0; sl_ptr++) {
                    if (sl_ptr->value_code == value_code) {
                      g_printf("  ValueDef match feature %x value %d %s\n",
                               feature_def->feature_code, sl_ptr->value_code, sl_ptr->value_name);
                      g_variant_builder_add(value_dict_builder, "{ys}", sl_ptr->value_code, sl_ptr->value_name);
                      value_name = sl_ptr->value_name;
                    }
                  }
                }
                g_printf("  ValueDef feature %x value %d %s\n",
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
              g_printf("%x %s\n", feature_def->feature_code, get_status_message(status));
            }
          }
        }
      }
      ddca_close_display(disp_handle);
    }
  }

  char *message_text = NULL;
  if (parsed_capabilities_ptr != NULL && parsed_capabilities_ptr->msg_ct > 0) {
    char *messages_null_terminated[parsed_capabilities_ptr->msg_ct + 1];
    for (int i = 0; i <= parsed_capabilities_ptr->msg_ct; i++) {
      messages_null_terminated[i] = parsed_capabilities_ptr->messages[i];
    }
    messages_null_terminated[parsed_capabilities_ptr->msg_ct] = NULL;
    message_text = g_strjoinv("; ", messages_null_terminated);
  }
  else {
    message_text = get_status_message(status);
  }

  GVariant *result = g_variant_new("(syya{ys}a{y(ssa{ys})}is)",
                                   vdu_model,
                                   mccs_version_major,
                                   mccs_version_minor,
                                   command_dict_builder,
                                   feature_dict_builder,
                                   status, message_text);
  g_dbus_method_invocation_return_value(invocation, result);   // Think this frees the result
  ddca_free_display_info_list(info_list);
  ddca_free_parsed_capabilities(parsed_capabilities_ptr);
  free(caps_text);
  g_free(hex_edid);
  free(message_text);
}

static void get_vcp_metadata(GVariant* parameters, GDBusMethodInvocation* invocation) {
  int display_number;
  char *hex_edid;
  uint8_t vcp_code;
  u_int32_t flags;

  g_variant_get(parameters, "(isyu)", &display_number, &hex_edid, &vcp_code, &flags);
  g_printf("GetVcpMetadata display_num=%d, edid=%.30s...\nvcp_code=%d\n", display_number, hex_edid, vcp_code);

  DDCA_Display_Info_List *info_list = NULL;
  DDCA_Display_Info *vdu_info = NULL;  // pointer into info_list
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
      status = ddca_get_feature_metadata_by_dh(vcp_code, disp_handle, 0, &metadata_ptr);  // TODO valgrind complains
      if (status == 0) {
        if (metadata_ptr->feature_name != NULL) {
          feature_name = metadata_ptr->feature_name;
        }
        if (metadata_ptr->feature_desc != NULL) {
          feature_description = metadata_ptr->feature_desc;
        }
        if (metadata_ptr->sl_values != NULL) {
          for (DDCA_Feature_Value_Entry *sl_ptr = metadata_ptr->sl_values; sl_ptr->value_code != 0; sl_ptr++) {}
        }
        is_read_only = metadata_ptr->feature_flags & DDCA_RO;
        is_write_only = metadata_ptr->feature_flags & DDCA_WO;
        is_rw = metadata_ptr->feature_flags & DDCA_RW;
        is_complex = metadata_ptr->feature_flags & (DDCA_COMPLEX_CONT | DDCA_COMPLEX_NC);
        is_continuous = metadata_ptr->feature_flags & DDCA_CONT;
      }
      ddca_close_display(disp_handle);
    }
  }
  char *message_text = get_status_message(status);
  GVariant *result = g_variant_new("(ssbbbbbis)",
                                   feature_name, feature_description,
                                   is_read_only, is_write_only, is_rw, is_complex, is_continuous,
                                   status, status == 0 ? "OK" : message_text);
  g_dbus_method_invocation_return_value(invocation, result);  // Think this frees the result
  ddca_free_display_info_list(info_list);
  ddca_free_feature_metadata(metadata_ptr);
  g_free(hex_edid);
  free(message_text);
}

static void handle_method_call(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                               const gchar *interface_name, const gchar *method_name, GVariant *parameters,
                               GDBusMethodInvocation *invocation, gpointer user_data) {
  if (g_strcmp0(method_name, "Detect") == 0) {
    detect(parameters, invocation);
  } else if (g_strcmp0(method_name, "GetVcp") == 0) {
    get_vcp(parameters, invocation);
  } else if (g_strcmp0(method_name, "GetMultipleVcp") == 0) {
    get_multiple_vcp(parameters, invocation);
  } else if (g_strcmp0(method_name, "SetVcp") == 0) {
    set_vcp(parameters, invocation);
  } else if (g_strcmp0(method_name, "GetVcpMetadata") == 0) {
    get_vcp_metadata(parameters, invocation);
  } else if (g_strcmp0(method_name, "GetCapabilitiesString") == 0) {
    get_capabilities_string(parameters, invocation);
  } else if (g_strcmp0(method_name, "GetCapabilitiesMetadata") == 0) {
    get_capabilities_metadata(parameters, invocation);
  } else if (g_strcmp0(method_name, "Restart") == 0) {
    restart(parameters, invocation);
  }
}

static GVariant *handle_get_property(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                     const gchar *interface_name, const gchar *property_name, GError **error,
                                     gpointer user_data) {
  GVariant *ret = NULL;
  if (g_strcmp0(property_name, "DdcutilVersionString") == 0) {
    ret = g_variant_new_string(ddca_ddcutil_extended_version_string());
  }
  else   if (g_strcmp0(property_name, "InterfaceVersionString") == 0) {
    ret = g_variant_new_string(DDCUTIL_SERVICE_VERSION_STRING);
  }
  else if (g_strcmp0(property_name, "Verify") == 0) {
    ret = g_variant_new_boolean(ddca_is_verify_enabled());
  } 
  else if (g_strcmp0(property_name, "SleepMultiplier") == 0) {
#if DDCUTIL_VMAJOR >= 2
    ret = g_variant_new_double(ddca_get_sleep_multiplier());
#else
    ret = g_variant_new_double(ddca_get_default_sleep_multiplier());
#endif
  }
  else if (g_strcmp0(property_name, "AttributesReturnedByDetect") == 0) {
    GVariantBuilder *builder;
    GVariant *value;
    builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
    for (int i = 0; attributes_returned_from_detect[i] != NULL; i++) {
      g_variant_builder_add(builder, "s", attributes_returned_from_detect[i]);
    }
    value = g_variant_new("as", builder);
    g_variant_builder_unref(builder);
    ret = value;
  }
  else if (g_strcmp0(property_name, "StatusValues") == 0) {
    GVariantBuilder *builder;
    GVariant *value;
    builder = g_variant_builder_new(G_VARIANT_TYPE ("a{is}"));
    g_variant_builder_add(builder, "{is}", DDCRC_OK, ddca_rc_name(DDCRC_OK));
    for (int i = RCRANGE_DDC_START + 1; ddca_rc_name(-i) != NULL; i++) {
      g_variant_builder_add(builder, "{is}", -i, ddca_rc_name(-i));
    }
    value = g_variant_new("a{is}", builder);
    g_variant_builder_unref(builder);
    ret = value;
  }
  else if (g_strcmp0(property_name, "OutputLevel") == 0) {
    ret = g_variant_new_byte(ddca_get_output_level());
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
#if DDCUTIL_VMAJOR >= 2
    ddca_set_sleep_multiplier(g_variant_get_double(value));
#elif DDCUTIL_VMAJOR
    ddca_set_default_sleep_multiplier(g_variant_get_double(value));
#endif
  }
  else if (g_strcmp0(property_name, "OutputLevel") == 0) {
    ddca_set_output_level(g_variant_get_byte(value));
  }
  return *error == NULL;
}

#ifdef COMPILE_DEMO_SIGNAL_CONNECTION_TIMER

/*** Test code that generates a signal from a timer attached to the d-bus connection ***/

static gboolean handle_polling_timeout (gpointer user_data) {
  GDBusConnection *connection = G_DBUS_CONNECTION (user_data);
  GError *local_error = NULL;
  printf("handle_polling_timeout\n");
  g_dbus_connection_emit_signal (connection,
                                 NULL,
                                 "/com/ddcutil/DdcutilObject",
                                 "com.ddcutil.DdcutilInterface",
                                 "ConnectedDisplaysChanged",
                                 g_variant_new ("(iu)", 0, 0),
                                 &local_error);
  return TRUE;
}
#endif

/* GDBUS service handlers */
static const GDBusInterfaceVTable interface_vtable = {handle_method_call, handle_get_property, handle_set_property};

GDBusConnection *dbus_connection = NULL;
static void on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  const char* object_path = "/com/ddcutil/DdcutilObject";
  dbus_connection = connection;
  const guint registration_id =
    g_dbus_connection_register_object(connection,
                                      object_path,
                                      introspection_data->interfaces[0],
                                      &interface_vtable, NULL, /* user_data */
                                      NULL,                    /* user_data_free_func */
                                      NULL);                   /* GError** */
  g_assert(registration_id > 0);
  g_print("Registered %s\n", object_path);
#ifdef COMPILE_DEMO_SIGNAL_CONNECTION_TIMER
  g_timeout_add_seconds (2,
                         handle_polling_timeout,
                         connection);
#endif
}

static void on_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  g_print("Name acquired %s\n", name);
}

static void on_name_lost(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  g_print("Lost registration - is another instance already registered?\n");
  exit(1);
}

#ifdef COMPILE_DEMO_SIGNAL_MAIN_LOOP_CUSTOM_SOURCE

/*** Test code that generates a signal conditionally during the g-main-loop ***/

struct  Signal_Source {
    GSource source;
    gchar my_data[256];
};
typedef struct Signal_Source Signal_Source;

static gboolean signal_prepare(GSource *source, gint *timeout) {
  *timeout = 0;
  if (dbus_connection == NULL) {
    return FALSE;
  }
  return TRUE;
}

static gboolean signal_check(GSource *source) {
  if (dbus_connection == NULL) {
    return FALSE;
  }
  return TRUE;
}

static gboolean signal_dispatch(GSource *source, GSourceFunc callback, gpointer user_data) {
  //Signal_Source *signal_source = (Signal_Source *) source;
  GError *local_error = NULL;
  if (dbus_connection == NULL) {
    printf("3 null connection\n");
    return FALSE;
  }
  static time_t last_time = 0;
  time_t now = time(0);
  if (now - last_time > 5) {
    last_time = now;
    printf("dispatch now %ld\n", now);
    g_dbus_connection_emit_signal (dbus_connection,
                                   NULL,
                                   "/com/ddcutil/DdcutilObject",
                                   "com.ddcutil.DdcutilInterface",
                                   "ConnectedDisplaysChanged",
                                   g_variant_new ("(iu)", 0, 0),
                                   &local_error);
  }
  return TRUE;
}
#endif

int main(int argc, char *argv[]) {
  server_executable = argv[0];
  g_set_prgname("ddcutil-dbus-server");

  bool version_request = FALSE;

#if DDCUTIL_VMAJOR >= 2
  gint ddca_syslog_level = 0;
  gint ddca_init_options = 0;
#endif

  const GOptionEntry entries[] = {
    { "version", 'v', 0, G_OPTION_ARG_NONE, &version_request,
"print ddcutil version, com.ddcutil.DdcUtilInterface version, and exit", NULL },
#if DDCUTIL_VMAJOR >= 2
    { "ddca-syslog-level", 's', 0, G_OPTION_ARG_INT, &ddca_syslog_level,
      "0=Never|3=Error|6=Warning|9=Notice|12=Info|18=Debug", NULL },
    { "ddca-init-options", 'i', 0, G_OPTION_ARG_INT, &ddca_init_options,
      "1=Disable-Config-File", NULL },
#endif
    { NULL }
  };

  GError *error = NULL;
  GOptionContext *context = g_option_context_new("-- [LIBDDCUTIL-OPTION?]");
  g_option_context_add_main_entries(context, entries, NULL);
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_print ("option parsing failed: %s\n", error->message);
    exit (1);
  }

if (version_request) {
  g_print("ddcutil %s com.ddcutil.DdcUtilInterface %s\n",
    ddca_ddcutil_extended_version_string(), DDCUTIL_SERVICE_VERSION_STRING);
  exit(1);
}

#if DDCUTIL_VMAJOR >= 2
  char *argv_null_terminated[argc];
  for (int i = 0; i < argc; i++) {
    argv_null_terminated[i] = argv[i + 2];
  }
  argv_null_terminated[argc - 1] = NULL;
  char *arg_string = g_strjoinv(" ", argv_null_terminated);

  ddca_init(arg_string,0,0);
#endif

  /* Build introspection data structures from XML.
   */
  introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
  g_assert(introspection_data != NULL);

  const guint owner_id = g_bus_own_name(
    G_BUS_TYPE_SESSION,
    "com.ddcutil.DdcutilService",
    G_BUS_NAME_OWNER_FLAGS_NONE,
    on_bus_acquired,
    on_name_acquired,
    on_name_lost, NULL,
    NULL);

  GMainLoop *loop = g_main_loop_new(NULL, FALSE);

#ifdef COMPILE_DEMO_SIGNAL_MAIN_LOOP_CUSTOM_SOURCE
  GMainContext* loop_context = g_main_loop_get_context(loop);
  GSourceFuncs source_funcs = { signal_prepare, signal_check, signal_dispatch };
  GSource *source = g_source_new(&source_funcs, sizeof(Signal_Source));
  g_source_attach(source, loop_context);
  g_source_unref(source);
#endif

  g_main_loop_run(loop);
  g_bus_unown_name(owner_id);
  g_dbus_node_info_unref(introspection_data);
  return 0;
}
