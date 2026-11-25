/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XFILEUNPACKER_FREEARC_SFX_NAMES_H
#define XFILEUNPACKER_FREEARC_SFX_NAMES_H
#include <windows.h>
#include <stdint.h>
#include <string.h>

static inline bool fs_safe_name(const char *name)
{
    const char *part = name, *p;
    unsigned depth = 0;
    if (!name || !name[0] || strlen(name) > 32700U || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, NULL, 0)) return false;
    for (p = name;; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c && c != '/' && (c < 32 || c == 127 || strchr("\\:*?\"<>|", c))) return false;
        if (c == '/' || !c) {
            size_t length = (size_t)(p - part), stem = 0, copied;
            char device[9];
            if (!length || part[length - 1] == '.' || part[length - 1] == ' ' || (length == 1 && part[0] == '.') || (length == 2 && !memcmp(part, "..", 2))) return false;
            while (stem < length && part[stem] != '.') ++stem;
            while (stem && part[stem - 1] == ' ') --stem;
            for (copied = 0; copied < stem && copied < 8; ++copied) {
                unsigned char ch = (unsigned char)part[copied];
                device[copied] = (char)(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
            }
            device[copied] = 0;
            if (!strcmp(device, "CON") || !strcmp(device, "PRN") || !strcmp(device, "AUX") || !strcmp(device, "NUL") || !strcmp(device, "CLOCK$") ||
                !strcmp(device, "CONIN$") || !strcmp(device, "CONOUT$") ||
                (stem == 4 && (!memcmp(device, "COM", 3) || !memcmp(device, "LPT", 3)) && device[3] >= '0' && device[3] <= '9') ||
                (stem == 5 && (!memcmp(device, "COM", 3) || !memcmp(device, "LPT", 3)) && (unsigned char)device[3] == 0xc2 &&
                 ((unsigned char)device[4] == 0xb9 || (unsigned char)device[4] == 0xb2 || (unsigned char)device[4] == 0xb3)))
                return false;
            if (!c) break;
            if (++depth > 128) return false;
            part = p + 1;
        }
    }
    return true;
}

/* Match the native reader's duplicate-name table, including hash collisions. */
static inline uint64_t fs_name_hash(const char *name)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (; *name; ++name) {
        unsigned char c = (unsigned char)*name;
        hash ^= c >= 'a' && c <= 'z' ? c - 32 : c;
        hash *= UINT64_C(1099511628211);
    }
    return hash ? hash : 1;
}
#endif
