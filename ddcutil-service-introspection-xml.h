/*
 * SPDX-FileCopyrightText: 2023-2026 Contributors to ddcutil-service <https://github.com/digitaltrails/ddcutil-service>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef DDCUTIL_SERVICE_INTROSPECTION_XML_H_INCLUDED
#define DDCUTIL_SERVICE_INTROSPECTION_XML_H_INCLUDED

/*
 * ddcutil-service-introspection-xml.h
 *
 * Loads ddcutil-service com.ddcutil.DdcutilService.xml into a text string.  
 * Pre-canned XML is preferred because it includes documentation comments.
 * 
 * The file is loaded by C23 #embed if available, otherwise by __asm__ .incbin
 */

#include <stddef.h>

/* 
 *Detect #embed support 
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
/* 
 * C23 mode – #embed is mandatory 
 */
#define USE_EMBED 1
#elif defined(__GNUC__) && __GNUC__ >= 14 && defined(__has_include)
/* 
 * GCC 14+ supports #embed even in gnu17/gnu2x modes
 * (__has_include is just a placeholder; actual test is compiler version)
 */
#define USE_EMBED 1
#elif defined(__clang__) && __clang_major__ >= 18
/*
 * Clang 18+ supports #embed as an extension
 */
#define USE_EMBED 1
#endif

#ifdef USE_EMBED

/*
 * C23 / modern extension method
 */
static const char ddcutil_service_xml_text[] = {
    #embed "com.ddcutil.DdcutilService.xml"
    , 0   /* null terminator */
};

#else

/*
 * Fallback to inline assembly .incbin (GCC/Clang)
 */
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

static const char* const ddcutil_service_xml_text = xml_data_begin;

#endif

#endif // DDCUTIL_SERVICE_INTROSPECTION_XML_H_INCLUDED
