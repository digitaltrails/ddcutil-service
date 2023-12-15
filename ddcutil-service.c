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
#include <spawn.h>

#include <ddcutil_c_api.h>
#include <ddcutil_status_codes.h>
#include <ddcutil_macros.h>


#define DDCUTIL_DBUS_INTERFACE_VERSION_STRING "1.0.0"

#if DDCUTIL_VMAJOR == 2 && DDCUTIL_VMINOR == 0 && DDCUTIL_VMICRO < 2
  #define HAS_OPTION_ARGUMENTS
  #define HAS_DDCA_GET_SLEEP_MULTIPLIER
  #define USE_INTERNAL_CHANGE_POLLING
#elif DDCUTIL_VMAJOR >= 2
  #define HAS_DISPLAYS_CHANGED_CALLBACK
  #define HAS_OPTION_ARGUMENTS
  #define HAS_INDIVIDUAL_SLEEP_MULTIPLIER
  #define HAS_DYNAMIC_SLEEP
#else
  #define HAS_DDCA_GET_DEFAULT_SLEEP_MULTIPLIER
  #define USE_INTERNAL_CHANGE_POLLING
#endif

#if !defined(HAS_DISPLAYS_CHANGED_CALLBACK)
// Earlier version of ddcutil lacking this definition
typedef enum {
  DDCA_EVENT_CONNECTED    = 1,
  DDCA_EVENT_DISCONNETED  = 2,
  DDCA_EVENT_DPMS_AWAKE   = 4,
  DDCA_EVENT_DPMS_ASLEEP  = 8,
} DDCA_Display_Event_Type;
// Also lacking anything to translate the names of the event types
char *DDCA_Display_Event_Type_names[] = {
  G_STRINGIFY(DDCA_EVENT_CONNECTED), G_STRINGIFY(DDCA_EVENT_DISCONNETED),
  G_STRINGIFY(DDCA_EVENT_DPMS_AWAKE), G_STRINGIFY(DDCA_EVENT_DPMS_ASLEEP)};
#endif

static GDBusNodeInfo *introspection_data = NULL;

/* Introspection data for the service we are exporting
 * TODO At some point this could possibly be moved to a file, but maybe is handy to embed it here.
 * TODO Needs Documentation, see https://dbus.freedesktop.org/doc/dbus-api-design.html#annotations
 */
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

    "    <method name='GetSleepMultiplier'>"
    "      <arg name='display_number' type='i' direction='in'/>"
    "      <arg name='edid_txt' type='s' direction='in'/>"
    "      <arg name='flags' type='u' direction='in'/>"
    "      <arg name='current_multiplier' type='d' direction='out'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"

    "    <method name='SetSleepMultiplier'>"
    "      <arg name='display_number' type='i' direction='in'/>"
    "      <arg name='edid_txt' type='s' direction='in'/>"
    "      <arg name='new_multiplier' type='d' direction='in'/>"
    "      <arg name='flags' type='u' direction='in'/>"
    "      <arg name='error_status' type='i' direction='out'/>"
    "      <arg name='error_message' type='s' direction='out'/>"
    "    </method>"

    "    <signal name='ConnectedDisplaysChanged'>"
    "      <arg type='s' name='edid'/>"
    "      <arg type='i' name='event_type'/>"
    "      <arg type='u' name='flags'/>"
    "    </signal>"

    "    <property type='as' name='AttributesReturnedByDetect' access='read'/>"
    "    <property type='a{is}' name='StatusValues' access='read'/>"
    "    <property type='a{is}' name='DisplayEventTypes' access='read'/>"
    "    <property type='s' name='DdcutilVersion' access='read'/>"
    "    <property type='b' name='DdcutilVerifySetVcp' access='readwrite'/>"
    "    <property type='b' name='DdcutilDynamicSleep' access='readwrite'/>"
    "    <property type='u' name='DdcutilOutputLevel' access='readwrite'/>"
    "    <property type='s' name='ServiceInterfaceVersion' access='read'/>"
    "    <property type='b' name='ServiceInfoLogging' access='readwrite'/>"
    "    <property type='b' name='ServiceSignalChanges' access='readwrite'/>"

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

static bool enable_change_signals = TRUE;

/**
 * Encode the EDID for easy/efficient unmarshalling on clients.
 * @param edid binary EDID
 * @return a relatively compact character string encoded edid
 */
static char *edid_encode(const uint8_t *edid) {
  return g_base64_encode(edid, 128);  // Shorter than hex but not too much like line noise.
}

static char *server_executable = PROGRAM_NAME;

static uint32_t edid_to_binary_serial_number(const uint8_t *edid_bytes) {
  const uint32_t binary_serial =
    edid_bytes[0x0c]       |
    edid_bytes[0x0d] <<  8 |
    edid_bytes[0x0e] << 16 |
    edid_bytes[0x0f] << 24;
  return binary_serial;
}

/**
 * Create a new string with invalid utf-8 edited out (replaced with ?)
 * @param text suspect text
 * @return g_malloced text with invalid utf-8 edited out
 */
static gchar *sanitize_utf8(const char *text) {
  gchar *result = g_strdup(text);
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

/**
 * Enable logging of info and debug level messages for this service.
 * @param enable whether to log or not
 * @param overwrite whether to overwrite any existing setting
 * @return the new enabled state
 */
static bool enable_service_info_logging(bool enable, bool overwrite) {
  if (enable) {
    g_setenv("G_MESSAGES_DEBUG", G_LOG_DOMAIN, overwrite);  // enable info & debug messages for our domain.
    return TRUE;
  }
  g_unsetenv("G_MESSAGES_DEBUG"); // disable info & debug messages for our domain.
  return FALSE;
}

/**
 * @brief Return whether the service is set to log info and debug messages.
 * @return enabled state
 */
static bool is_service_info_logging() {
  const char *value = g_getenv("G_MESSAGES_DEBUG");
  return value != NULL && strstr(value, G_LOG_DOMAIN) != NULL;
}

/**
 * @brief Obtain a text message for a DDCA status.
 * @param status the DDCA status
 * @return a g_malloced message
 */
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
 * @return success status
 */
static DDCA_Status get_display_info(const int display_number, const char *edid_encoded,
                                    DDCA_Display_Info_List **dlist, DDCA_Display_Info **dinfo) {
  *dinfo = NULL;
  DDCA_Status status = ddca_get_display_info_list2(0, dlist);

    if (status == 0) {
      for (int ndx = 0; ndx < (*dlist)->ct; ndx++) {
        if (display_number == (*dlist)->info[ndx].dispno) {
          *dinfo = &((*dlist)->info[ndx]);
          break;
        }
        gchar *dlist_edid_encoded = edid_encode((*dlist)->info[ndx].edid_bytes);
        if (edid_encoded != NULL && strcmp(edid_encoded, dlist_edid_encoded) == 0) {
          *dinfo = &((*dlist)->info[ndx]);
          g_free(dlist_edid_encoded);
          break;
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
  char *libopts;
  u_int32_t syslog_level;
  u_int32_t opts;
  g_variant_get(parameters, "(suu)", &libopts, &syslog_level, &opts);
  char *message_text = get_status_message(DDCRC_OK);
  g_message("DdcaInit syslog_level=%x opts=%x libopts=%s", syslog_level, opts, libopts);
  GVariant *result = g_variant_new("(is)", DDCRC_OK, message_text);
  g_dbus_method_invocation_return_value(invocation, result);

  gchar** argv;
#if defined(HAS_OPTION_ARGUMENTS)
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
  const struct sigaction arg = {
    .sa_handler=SIG_IGN,
    .sa_flags=SA_NOCLDWAIT // Never wait for termination of a child process.
  };
  sigaction(SIGCHLD, &arg, NULL);

  pid_t spawn_pid;
  posix_spawn(&spawn_pid, server_executable, NULL, NULL, argv,environ);
  exit(0);
}

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
  char *detect_message_text = get_status_message(detect_status);

  DDCA_Display_Info_List *dlist = NULL;
  const DDCA_Status list_status = ddca_get_display_info_list2(flags != 0, &dlist);
  char *list_message_text = get_status_message(list_status);

  g_info("Detect status=%d message=%s", list_status, list_message_text);

  // see https://docs.gtk.org/glib/struct.VariantBuilder.html

  GVariantBuilder detected_displays_builder_instance;  // Allocate on the stack for easier memory management.
  GVariantBuilder *detected_displays_builder = &detected_displays_builder_instance;

  g_variant_builder_init(detected_displays_builder, G_VARIANT_TYPE("a(iiisssqsu)"));
  for (int ndx = 0; ndx < dlist->ct; ndx++) {
    const DDCA_Display_Info *vdu_info = &dlist->info[ndx];
    gchar *safe_mfg_id = sanitize_utf8(vdu_info->mfg_id);
    gchar *safe_model = sanitize_utf8(vdu_info->model_name);//"xxxxwww\xF0\xA4\xADiii" );
    gchar *safe_sn = sanitize_utf8(vdu_info->sn);
    gchar *edid_encoded = edid_encode(vdu_info->edid_bytes);
    g_info("Detected %s %s %s", safe_mfg_id, safe_model, safe_sn);

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
  char *hex_edid;
  uint8_t vcp_code;
  u_int32_t flags;

  g_variant_get(parameters, "(isyu)", &display_number, &hex_edid, &vcp_code, &flags);

  g_info("GetVcp vcp_code=%d display_num=%d, edid=%.30s...", vcp_code, display_number, hex_edid);

  uint16_t current_value = 0;
  uint16_t max_value = 0;
  char *formatted_value = NULL;

  DDCA_Display_Info_List *info_list = NULL;
  DDCA_Display_Info *vdu_info = NULL;  // pointer into info_list
  DDCA_Status status = get_display_info(display_number, hex_edid, &info_list, &vdu_info);
  if (status == 0) {
    DDCA_Display_Handle disp_handle;
    status = ddca_open_display2(vdu_info->dref, 1, &disp_handle);
    if (status == 0) {
      static DDCA_Non_Table_Vcp_Value valrec;
      status = ddca_get_non_table_vcp_value(disp_handle, vcp_code, &valrec);
      if (status == 0) {
        current_value = valrec.sh << 8 | valrec.sl;
        max_value = valrec.mh << 8 | valrec.ml;
        status = ddca_format_non_table_vcp_value_by_dref(vcp_code, vdu_info->dref, &valrec, &formatted_value);
      }
      ddca_close_display(disp_handle);
    }
  }
  char *message_text = get_status_message(status);
  GVariant *result = g_variant_new("(qqsis)", current_value, max_value, formatted_value, status, message_text);
  g_dbus_method_invocation_return_value(invocation, result);   // Think this frees the result
  ddca_free_display_info_list(info_list);
  if (formatted_value != NULL) {
    free(formatted_value);
  }
  free(hex_edid);
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
  char *edid_encoded;
  u_int32_t flags;

  GVariantIter *vcp_code_iter;
  g_variant_get(parameters, "(isayu)", &display_number, &edid_encoded, &vcp_code_iter, &flags);

  g_info("GetMultipleVcp display_num=%d, edid=%.30s...", display_number, edid_encoded);

  const int number_of_vcp_codes = g_variant_iter_n_children(vcp_code_iter);
  const u_int8_t vcp_codes[number_of_vcp_codes];
  for (int i = 0; g_variant_iter_loop(vcp_code_iter, "y", &vcp_codes[i]); i++) {}
  g_variant_iter_free (vcp_code_iter);

  GVariantBuilder value_array_builder_instance;
  GVariantBuilder *value_array_builder = &value_array_builder_instance;
  g_variant_builder_init(value_array_builder, G_VARIANT_TYPE("a(yqqs)"));

  DDCA_Display_Info_List *info_list = NULL;
  DDCA_Display_Info *vdu_info = NULL;  // pointer into info_list
  DDCA_Status status = get_display_info(display_number, edid_encoded, &info_list, &vdu_info);
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
          g_variant_builder_add(value_array_builder, "(yqqs)",
            vcp_code, current_value, max_value, formatted_value);
          free(formatted_value);
        }
        else {  // Probably just asleep or turned off
          g_info("GetMultipleVcp failed for vcp_code=%d display_num=%d edid=%.30s...",
             vcp_code, display_number,edid_encoded);
        }
      }
      ddca_close_display(disp_handle);
    }
  }
  char *message_text = get_status_message(status);
  GVariant *result = g_variant_new("(a(yqqs)is)", value_array_builder, status, message_text);
  g_dbus_method_invocation_return_value(invocation, result);   // Think this frees the result
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
  char *edid_encoded;
  uint8_t vcp_code;
  uint16_t new_value;
  u_int32_t flags;

  g_variant_get(parameters, "(isyqu)", &display_number, &edid_encoded, &vcp_code, &new_value, &flags);

  g_info("SetVcp vcp_code=%d value=%d display_num=%d edid=%.30s...", vcp_code, new_value, display_number, edid_encoded);

  DDCA_Display_Info_List *info_list = NULL;
  DDCA_Display_Info *vdu_info = NULL;  // pointer into info_list
  DDCA_Status status = get_display_info(display_number, edid_encoded, &info_list, &vdu_info);
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
  if (status != 0) {  // Probably just asleep or turned off
    g_info("SetVcp failed for vcp_code=%d value=%d display_num=%d edid=%.30s...",
       vcp_code, new_value, display_number, edid_encoded);
  }
  char *message_text = get_status_message(status);
  GVariant *result = g_variant_new("(is)", status, message_text);
  g_dbus_method_invocation_return_value(invocation, result);   // Think this frees the result
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
  char *hex_edid;
  char *caps_text = NULL;
  u_int32_t flags;

  g_variant_get(parameters, "(isu)", &display_number, &hex_edid, &flags);

  g_info("GetCapabilitiesString display_num=%d, edid=%.30s...", display_number, hex_edid);

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
  char *hex_edid;
  char *caps_text = NULL;
  u_int32_t flags;

  g_variant_get(parameters, "(isu)", &display_number, &hex_edid, &flags);

  g_info("GetCapabilitiesMetadata display_num=%d, edid=%.30s...", display_number, hex_edid);

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
          const DDCA_Cap_Vcp *vcp_feature_array = parsed_capabilities_ptr->vcp_codes;

          g_debug("vcp_code_ct=%d", parsed_capabilities_ptr->vcp_code_ct);

          mccs_version_major = parsed_capabilities_ptr->version_spec.major;
          mccs_version_minor = parsed_capabilities_ptr->version_spec.minor;
          for (int command_idx = 0; command_idx < parsed_capabilities_ptr->cmd_ct; command_idx++) {
            char *command_desc = g_strdup_printf("desc of %d", parsed_capabilities_ptr->cmd_codes[command_idx]);
            //ddca_cmd_code_name();
            g_debug("CommandDef %x %s ", parsed_capabilities_ptr->cmd_codes[command_idx], command_desc);

            g_variant_builder_add(
              command_dict_builder, "{ys}", parsed_capabilities_ptr->cmd_codes[command_idx], command_desc);
            g_free(command_desc);  // TODO is this OK, or are we freeing too early?
          }

          for (int feature_idx = 0; feature_idx < parsed_capabilities_ptr->vcp_code_ct; feature_idx++) {
            const DDCA_Cap_Vcp *feature_def = vcp_feature_array + feature_idx;
            DDCA_Feature_Metadata *metadata_ptr;

            // TODO valgrind complains
            status = ddca_get_feature_metadata_by_dh(feature_def->feature_code, disp_handle, true, &metadata_ptr);
            if (status == 0) {
              g_debug("FeatureDef: %x %s %s",
                      metadata_ptr->feature_code, metadata_ptr->feature_name, metadata_ptr->feature_desc);
              GVariantBuilder value_dict_builder_instance;  // Allocate on the stack for easier memory management.
              GVariantBuilder *value_dict_builder = &value_dict_builder_instance;
              g_variant_builder_init(value_dict_builder, G_VARIANT_TYPE("a{ys}"));
              for (int value_idx = 0; value_idx < feature_def->value_ct; value_idx++) {
                const u_int8_t value_code = feature_def->values[value_idx];
                char *value_name = "";
                if (metadata_ptr->sl_values != NULL) {
                  for (const DDCA_Feature_Value_Entry *fve = metadata_ptr->sl_values; fve->value_code != 0; fve++) {
                    if (fve->value_code == value_code) {
                      g_debug("  ValueDef match feature %x value %d %s",
                                feature_def->feature_code, fve->value_code, fve->value_name);
                      g_variant_builder_add(value_dict_builder, "{ys}", fve->value_code, fve->value_name);
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
  char *hex_edid;
  uint8_t vcp_code;
  u_int32_t flags;

  g_variant_get(parameters, "(isyu)", &display_number, &hex_edid, &vcp_code, &flags);
  g_info("GetVcpMetadata display_num=%d, edid=%.30s...vcp_code=%d", display_number, hex_edid, vcp_code);

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
      status = ddca_get_feature_metadata_by_dh(vcp_code, disp_handle, true, &metadata_ptr);  // TODO valgrind complains
      if (status == 0) {
        if (metadata_ptr->feature_name != NULL) {
          feature_name = metadata_ptr->feature_name;
        }
        if (metadata_ptr->feature_desc != NULL) {
          feature_description = metadata_ptr->feature_desc;
        }
        // if (metadata_ptr->sl_values != NULL) {  // TODO - not used, do we need it?
        //   for (DDCA_Feature_Value_Entry *sl_ptr = metadata_ptr->sl_values; sl_ptr->value_code != 0; sl_ptr++) {}
        // }
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
  char *hex_edid;
  u_int32_t flags;
  DDCA_Status status = 0;

  g_variant_get(parameters, "(isu)", &display_number, &hex_edid, &flags);

  g_info("GetSleepMultiplier display_num=%d, edid=%.30s...", display_number, hex_edid);

  double multiplier;
#if defined(HAS_DDCA_GET_SLEEP_MULTIPLIER)
  multiplier = ddca_get_sleep_multiplier();
#elif defined(HAS_DDCA_GET_DEFAULT_SLEEP_MULTIPLIER)
  multiplier = ddca_get_default_sleep_multiplier();
#elif defined(HAS_INDIVIDUAL_SLEEP_MULTIPLIER)
  DDCA_Display_Info_List *info_list = NULL;
  DDCA_Display_Info *vdu_info = NULL;  // pointer into info_list
  status = get_display_info(display_number, hex_edid, &info_list, &vdu_info);
  if (status == 0) {
    status = ddca_get_current_display_sleep_multiplier(vdu_info->dref, &multiplier);
  }
  ddca_free_display_info_list(info_list);
#endif
  char *message_text = get_status_message(status);
  GVariant *result = g_variant_new("(dis)", multiplier, status, message_text);
  g_dbus_method_invocation_return_value(invocation, result);   // Think this frees the result
  free(hex_edid);
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
  char *hex_edid;
  u_int32_t flags;
  double new_multiplier;
  DDCA_Status status = 0;

  g_variant_get(parameters, "(isdu)", &display_number, &hex_edid, &new_multiplier, &flags);

  g_info("SetSleepMultiplier value=%f display_num=%d edid=%.30s...",
         new_multiplier, display_number, hex_edid);

#if defined(HAS_DDCA_GET_SLEEP_MULTIPLIER)
  ddca_set_sleep_multiplier(new_multiplier);
#elif defined(HAS_DDCA_GET_DEFAULT_SLEEP_MULTIPLIER)
  ddca_set_default_sleep_multiplier(new_multiplier);
#elif defined(HAS_INDIVIDUAL_SLEEP_MULTIPLIER)
  DDCA_Display_Info_List *info_list = NULL;
  DDCA_Display_Info *vdu_info = NULL;  // pointer into info_list
  status = get_display_info(display_number, hex_edid, &info_list, &vdu_info);
  if (status == 0) {
    status = ddca_set_display_sleep_multiplier(vdu_info->dref, new_multiplier);
  }
  ddca_free_display_info_list(info_list);
#endif
  char *message_text = get_status_message(status);
  GVariant *result = g_variant_new("(is)", status, message_text);
  g_dbus_method_invocation_return_value(invocation, result);   // Think this frees the result
  g_free(hex_edid);
  free(message_text);
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
  } else if (g_strcmp0(method_name, "GetSleepMultiplier") == 0) {
    get_sleep_multiplier(parameters, invocation);
  } else if (g_strcmp0(method_name, "SetSleepMultiplier") == 0) {
    set_sleep_multiplier(parameters, invocation);
  } else if (g_strcmp0(method_name, "GetCapabilitiesString") == 0) {
    get_capabilities_string(parameters, invocation);
  } else if (g_strcmp0(method_name, "GetCapabilitiesMetadata") == 0) {
    get_capabilities_metadata(parameters, invocation);
  } else if (g_strcmp0(method_name, "Restart") == 0) {
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
static GVariant *handle_get_property(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                     const gchar *interface_name, const gchar *property_name, GError **error,
                                     gpointer user_data) {
  GVariant *ret = NULL;
  if (g_strcmp0(property_name, "DdcutilVersion") == 0) {
    ret = g_variant_new_string(ddca_ddcutil_extended_version_string());
  }
  else   if (g_strcmp0(property_name, "ServiceInterfaceVersion") == 0) {
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
    GVariantBuilder *builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
    for (int i = 0; attributes_returned_from_detect[i] != NULL; i++) {
      g_variant_builder_add(builder, "s", attributes_returned_from_detect[i]);
    }
    GVariant *value = g_variant_new("as", builder);
    g_variant_builder_unref(builder);
    ret = value;
  }
  else if (g_strcmp0(property_name, "StatusValues") == 0) {
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE ("a{is}"));
    g_variant_builder_add(builder, "{is}", DDCRC_OK, ddca_rc_name(DDCRC_OK));
    for (int i = RCRANGE_DDC_START + 1; ddca_rc_name(-i) != NULL; i++) {
      g_variant_builder_add(builder, "{is}", -i, ddca_rc_name(-i));
    }
    GVariant *value = g_variant_new("a{is}", builder);
    g_variant_builder_unref(builder);
    ret = value;
  }
  else if (g_strcmp0(property_name, "DisplayEventTypes") == 0) {
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE ("a{is}"));
    const int event_types[] = {DDCA_EVENT_CONNECTED, DDCA_EVENT_DISCONNETED, DDCA_EVENT_DPMS_AWAKE, DDCA_EVENT_DPMS_ASLEEP};
    const int num_event_types = sizeof(event_types) / sizeof(int);
    for (int i = 0; i < num_event_types; i++) {
#if defined(HAS_DISPLAYS_CHANGED_CALLBACK)
      g_variant_builder_add(builder, "{is}", event_types[i], ddca_display_event_type_name(i));
#else
      g_variant_builder_add(builder, "{is}", event_types[i], DDCA_Display_Event_Type_names[i]);
#endif
    }
    GVariant *value = g_variant_new("a{is}", builder);
    g_variant_builder_unref(builder);
    ret = value;
  }
  else if (g_strcmp0(property_name, "DdcutilOutputLevel") == 0) {
    ret = g_variant_new_uint32(ddca_get_output_level());
  }
  else if (g_strcmp0(property_name, "ServiceInfoLogging") == 0) {
    ret = g_variant_new_boolean(is_service_info_logging());
  }
  else if (g_strcmp0(property_name, "ServiceSignalChanges") == 0) {
    ret = g_variant_new_boolean(enable_change_signals);
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
static gboolean handle_set_property(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
                                    const gchar *interface_name, const gchar *property_name, GVariant *value,
                                    GError **error, gpointer user_data) {
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
  else if (g_strcmp0(property_name, "ServiceSignalChanges") == 0) {
    enable_change_signals = g_variant_get_boolean(value);
    g_message("ServiceSignalChanges %s", enable_change_signals ? "enabled" : "disabled");
  }
  return *error == NULL;
}

#if defined(HAS_DISPLAYS_CHANGED_CALLBACK)
static DDCA_Display_Detection_Event *display_detection_event = NULL;

static void display_detection_callback(DDCA_Display_Detection_Event event) {
  g_debug("Triggered display_detection_callback %d", event.event_type);
  display_detection_event = g_malloc(sizeof(DDCA_Display_Detection_Event));
  *display_detection_event = event;
}
#endif

/*
 * Our service connection - Will be set the the running connection when the bus is aquired.
 */
static GDBusConnection *dbus_connection = NULL;

/*
 * GDBUS service handler table - passed on registraction of the service
 */
static const GDBusInterfaceVTable interface_vtable = {handle_method_call, handle_get_property, handle_set_property};


#if defined(HAS_DISPLAYS_CHANGED_CALLBACK)

/*
 * The follow code enables a main-loop implementation of a main-loop
 * custom event-source, a GSource.  It defines a polled source that
 * handles ddcutil displays-changed data and sends signals to
 * the D-Bus client.
 */

struct  ConnectedDisplaysChanged_SignalSource {  // Source structure including custom data (if any)
  GSource source;
  gchar cdc_data[129];  // do we actually have any data - no, maybe later.
};
typedef struct ConnectedDisplaysChanged_SignalSource CDC_SignalSource_t;

/**
 * @brief registered with main-loop as a custom prepare event function.
 *
 * The main purpose of this function is setting the length of the
 * next timeout and return FALSE.  If an event happens to be ready the
 * function can return TRUE instead and the main-loop will act accordingly.
 *
 * @param source input source, not of much interest for this implementation
 * @param timeout output parameter setting the timeout for next call
 * @return
 */
static gboolean cdc_signal_prepare(GSource *source, gint *timeout) {
  if (enable_change_signals) {
    *timeout = 5000;

    if (dbus_connection == NULL || display_detection_event == NULL) {
      return FALSE;
    }
    g_debug("cdc display_detection_event ready");
  }
  else {
    *timeout = 30000;
  }
  return TRUE;
}

/**
 * @brief registered with main-loop as a custom check event function.
 *
 * Called by the main-loop on timeout to see if an event is ready.
 *
 * @param source
 * @return
 */
static gboolean cdc_signal_check(GSource *source) {
  if (enable_change_signals) {
    if (dbus_connection != NULL && display_detection_event != NULL) {
      return TRUE;
    }
  }
  return FALSE;
}

/**
 * @brief registered with main-loop to dispatch ConnectedDisplaysChanged signals.
 *
 * Called by the mainloop if the check function reports that an event
 * is ready.  This function should emit the signal to the D-Bus client.
 *
 * @param source
 * @param callback
 * @param user_data
 * @return
 */
static gboolean cdc_signal_dispatch(GSource *source, GSourceFunc callback, gpointer user_data) {
  //ConnectedDisplaysChanged_SignalSource *signal_source = (ConnectedDisplaysChanged_SignalSource *) source;
  GError *local_error = NULL;
  if (dbus_connection == NULL) {
    g_warning("cdc_signal_dispatch null D-Bus connection");
    return TRUE;
  }
  g_debug("cdc dispatch called");
  DDCA_Display_Detection_Event *event_ptr = display_detection_event;
  display_detection_event = NULL;
  if (event_ptr != NULL) {
    DDCA_Display_Info* dinfo;
    DDCA_Status status = ddca_get_display_info(event_ptr->dref, &dinfo);
    if (status == 0) {  // TODO needs testing
      g_debug("cdc_signal_dispatch emit ConnectedDisplaysChanged now");
      gchar *edid_encoded = edid_encode(dinfo->edid_bytes);
      if (!g_dbus_connection_emit_signal(dbus_connection,
                                         NULL,
                                         "/com/ddcutil/DdcutilObject",
                                         "com.ddcutil.DdcutilInterface",
                                         "ConnectedDisplaysChanged",
                                         g_variant_new ("(siu)", edid_encoded, event_ptr->event_type, 0),
                                         &local_error)) {
        g_warning("cdc_signal_dispatch failed %s", local_error != NULL ? local_error->message : "");
        g_free(local_error);
      }
      ddca_free_display_info(dinfo);
      g_free(edid_encoded);
    }
    g_free(event_ptr);
  }
  return TRUE;
}
#endif

#if defined(USE_INTERNAL_CHANGE_POLLING)

/*
 * Internal polling implementation of detecting changes.
 *
 * The follow code enables a main-loop implementation of a main-loop
 * custom event-source, a GSource.  It defines a polled source that
 * handles ddcutil displays-changed data and sends signals to
 * the D-Bus client.
 */

static GList *poll_list = NULL;  // List of currently detected edids
typedef struct {
  gchar * edid_encoded;
  gboolean live;
} Poll_List_Item;

static gint pollcmp(gconstpointer item_ptr, gconstpointer target) {
  gchar *target_str = (char *) target;
  Poll_List_Item *item = (Poll_List_Item *) item_ptr;
  return strcmp(target_str, item->edid_encoded);
}

static gchar *poll_event_edid_encoded = NULL;
static DDCA_Display_Event_Type poll_event_type = 0;

static bool poll_for_changes() {
  //ddca_redetect_displays();  // TODO Cannot use redetect it is too slow - delays the whole service loop
  DDCA_Display_Info_List *dlist;
  const DDCA_Status list_status = ddca_get_display_info_list2(1, &dlist);
  int change_count = 0;
  poll_event_edid_encoded = NULL;
  if (list_status == 0) {
    for (GList *ptr = poll_list; ptr != NULL; ptr = ptr->next) {
      ((Poll_List_Item *) (ptr->data))->live = FALSE;
    }

    for (int ndx = 0; ndx < dlist->ct; ndx++) {
      const DDCA_Display_Info *vdu_info = &dlist->info[ndx];
      gchar *edid_encoded = edid_encode(vdu_info->edid_bytes);
      GList *ptr = g_list_find_custom(poll_list, edid_encoded, pollcmp);
      if (ptr == NULL) {
        g_debug("Poll event - new %d %.30s...", ndx + 1, edid_encoded);
        change_count++;
        Poll_List_Item *item = g_malloc(sizeof(Poll_List_Item));
        item->edid_encoded = edid_encoded;
        item->live = TRUE;
        g_debug("Poll event - connected %.30s...\n", item->edid_encoded);
        poll_list = g_list_append(poll_list, item);
        poll_event_edid_encoded = g_strdup(edid_encoded);
        poll_event_type = DDCA_EVENT_CONNECTED;
        return TRUE; // Only one event on each poll
      }
      else {
        g_debug("Poll event - found %d set to live %.30s...", ndx + 1, edid_encoded);
        ((Poll_List_Item *) (ptr->data))->live = TRUE;
        g_free(edid_encoded);
      }
    }

    for (GList *ptr = poll_list; ptr != NULL;) {
      GList *next = ptr->next;
      Poll_List_Item *item = (Poll_List_Item *) ptr->data;
      if (!item->live) {
        g_debug("Poll event - disconnected %.30s...\n", item->edid_encoded);
        poll_event_edid_encoded = g_strdup(item->edid_encoded);
        poll_event_type = DDCA_EVENT_DISCONNETED;
        g_free(item->edid_encoded);
        g_free(item);
        poll_list = g_list_delete_link(poll_list, ptr);
        change_count++;
        return TRUE;  // Only one event on each poll
      }
      ptr = next;
    }
  }
  return FALSE;
}


typedef struct  {  // Source structure including custom data (if any)
  GSource source;
  gchar cdc_data[129];  // do we actually have any data - no, maybe later.
} Poll_SignalSource_t;

/**
 * @brief registered with main-loop as a custom prepare event function.
 *
 * The main purpose of this function is setting the length of the
 * next timeout and return FALSE.
 *
 * @param source input source, not of much interest for this implementation
 * @param timeout output parameter setting the timeout for next call
 * @return
 */
static gboolean poll_signal_prepare(GSource *source, gint *timeout) {
  if (enable_change_signals) {
    *timeout = 10000;
  }
  else {
    *timeout = 30000;
  }
  return FALSE;
}

/**
 * @brief registered with main-loop as a custom check event function.
 *
 * Called by the main-loop on timeout to poll for changes.
 *
 * @param source
 * @return
 */
static gboolean poll_signal_check(GSource *source) {
  if (enable_change_signals) {
    return poll_for_changes();
  }
  return FALSE;
}

/**
 * @brief registered with main-loop to dispatch ConnectedDisplaysChanged signals.
 *
 * Called by the mainloop if the check function reports that an event
 * is ready.  This function should emit the signal to the D-Bus client.
 *
 * @param source
 * @param callback
 * @param user_data
 * @return
 */
static gboolean poll_signal_dispatch(GSource *source, GSourceFunc callback, gpointer user_data) {
  //ConnectedDisplaysChanged_SignalSource *signal_source = (ConnectedDisplaysChanged_SignalSource *) source;
  GError *local_error;
  local_error = NULL;
  g_message("poll emit detected changes event-type=%d edid_ecoded=%s", poll_event_type, poll_event_edid_encoded);
  if (!g_dbus_connection_emit_signal(dbus_connection,
                                     NULL,
                                     "/com/ddcutil/DdcutilObject",
                                     "com.ddcutil.DdcutilInterface",
                                     "ConnectedDisplaysChanged",
                                     g_variant_new ("(siu)", poll_event_edid_encoded, poll_event_type, 1),
                                     &local_error)) {
    g_warning("poll emit detected changes signal failed: %s", local_error != NULL ? local_error->message : "");
    g_free(local_error);
  }
  g_free(poll_event_edid_encoded);
  poll_event_edid_encoded = NULL;
  return TRUE;
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
static void on_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  g_message("Name acquired %s", name);
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
static void on_name_lost(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  g_critical("Exiting: lost registration - is another instance already registered?");
  exit(1);
}

/**
 * @brief Setup the service and start the glib main loop.
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, char *argv[]) {

  server_executable = argv[0];
  g_message("Running %s (%s)", server_executable, PROGRAM_NAME);
  g_set_prgname(PROGRAM_NAME);

  bool version_request = FALSE;
  bool introspect_request = FALSE;
  bool log_info = FALSE;

#if defined(HAS_OPTION_ARGUMENTS)
  gint ddca_syslog_level = 0;
  gint ddca_init_options = 0;
#endif

  // Use the glib command line parser...
  const GOptionEntry entries[] = {
    { "version", 'v', 0, G_OPTION_ARG_NONE, &version_request,
"print ddcutil version, com.ddcutil.DdcUtilInterface version, and exit", NULL },
    { "introspect", 'x', 0, G_OPTION_ARG_NONE, &introspect_request,
"print introspection xml and exit", NULL },
    { "log-info", 'l', 0, G_OPTION_ARG_NONE, &log_info,
"log service info and debug messages", NULL },
#if defined(HAS_OPTION_ARGUMENTS)
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
      ddca_ddcutil_extended_version_string(), DDCUTIL_DBUS_INTERFACE_VERSION_STRING);
    exit(1);
  }

  if (log_info) {
    enable_service_info_logging(TRUE, FALSE);
  }
  g_message("%s %s, libddcutil %s", PROGRAM_NAME,
    DDCUTIL_DBUS_INTERFACE_VERSION_STRING, ddca_ddcutil_extended_version_string());
  g_message("ServiceInfoLogging %s", is_service_info_logging() ? "enabled" : "disabled");

  /* Build introspection data structures from XML.
   */
  introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
  g_assert(introspection_data != NULL);

  if (introspect_request) {
#if defined(XML_FROM_INTROSPECTED_DATA)
    GString *formatted_xml = g_string_new("");
    g_dbus_node_info_generate_xml(introspection_data, 4, formatted_xml); // Create XML from the registered service
#else
    GString *formatted_xml = g_string_new(introspection_xml);
    g_string_replace(formatted_xml, ">", ">\n", 0); // Creat XML from the embedded string constant
#endif
    g_print("%s\n", formatted_xml->str);
    exit(1);
  }

#if defined(HAS_OPTION_ARGUMENTS)
  // Handle ddcutil ddc_init() arguments
  char *argv_null_terminated[argc];
  for (int i = 0; i < argc; i++) {
    argv_null_terminated[i] = argv[i + 2];
  }
  argv_null_terminated[argc - 1] = NULL;
  char *arg_string = g_strjoinv(" ", argv_null_terminated);
  g_message("Calling ddca_init %d %d '%s'", ddca_syslog_level, ddca_init_options, arg_string);
  ddca_init(arg_string, ddca_syslog_level, ddca_init_options);
#endif

#if defined(HAS_DISPLAYS_CHANGED_CALLBACK)
  if (strcmp(ddca_ddcutil_version_string(), "2.0.0") != 0) {
    g_message("Registering DDCA display_detection_callback");
    ddca_register_display_detection_callback(display_detection_callback);
  }
#endif

  const guint owner_id = g_bus_own_name(
    G_BUS_TYPE_SESSION,
    "com.ddcutil.DdcutilService",
    G_BUS_NAME_OWNER_FLAGS_NONE,
    on_bus_acquired,
    on_name_acquired,
    on_name_lost, NULL,
    NULL);

  GMainLoop *loop = g_main_loop_new(NULL, FALSE);

#if defined(HAS_DISPLAYS_CHANGED_CALLBACK)
  if (enable_change_signals) {
    g_message("Using libddcutil change detection");
  }
  GMainContext* loop_context = g_main_loop_get_context(loop);
  GSourceFuncs source_funcs = { cdc_signal_prepare, cdc_signal_check, cdc_signal_dispatch };
  GSource *source = g_source_new(&source_funcs, sizeof(CDC_SignalSource_t));
  g_source_attach(source, loop_context);
  g_source_unref(source);
#endif

#if defined(USE_INTERNAL_CHANGE_POLLING)
  if (enable_change_signals) {
    g_message("Using internal change polling");
  }
  GMainContext* loop_context = g_main_loop_get_context(loop);
  GSourceFuncs source_funcs = { poll_signal_prepare, poll_signal_check, poll_signal_dispatch };
  GSource *source = g_source_new(&source_funcs, sizeof(Poll_SignalSource_t));
  g_source_attach(source, loop_context);
  g_source_unref(source);
#endif

  g_main_loop_run(loop);
  g_bus_unown_name(owner_id);
  g_dbus_node_info_unref(introspection_data);
  return 0;
}
