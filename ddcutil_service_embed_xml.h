#ifndef DDCUTIL_SERVICE_EMBED_XML_H
#define DDCUTIL_SERVICE_EMBED_XML_H

// ddcutil_service_embed_xml.h – picks #embed if available, otherwise .incbin


#include <stddef.h>

// Detect #embed support
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
// C23 mode – #embed is mandatory
#define USE_EMBED 1
#elif defined(__GNUC__) && __GNUC__ >= 14 && defined(__has_include)
// GCC 14+ supports #embed even in gnu17/gnu2x modes
// (__has_include is just a placeholder; actual test is compiler version)
#define USE_EMBED 1
#elif defined(__clang__) && __clang_major__ >= 18
// Clang 18+ supports #embed as an extension
#define USE_EMBED 1
#endif

#ifdef USE_EMBED

// C23 / modern extension method
static const char introspection_xml[] = {
    #embed "com.ddcutil.DdcutilService.xml"
    , 0   // null terminator
};

#else

// Fallback to inline assembly .incbin (GCC/Clang)
__asm__(
    ".section .rodata\n"
    "xml_data_begin:\n"
    ".incbin \"com.ddcutil.DdcutilService.xml\"\n"
    "xml_data_end:\n"
    ".byte 0\n"
    ".previous\n"
);
extern const char xml_data_begin[];
extern const char xml_data_end[];

static const char* const introspection_xml = xml_data_begin;

#endif

#endif // DDCUTIL_SERVICE_EMBED_XML_H
