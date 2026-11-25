/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_PROCESS_WOW64_HEAP_IDENTITY_H
#define XX_PROCESS_WOW64_HEAP_IDENTITY_H

/* Read only the loaded guest NTDLL's PE/CodeView metadata. The target module
 * is never executed or loaded into this inspector, and a file with the same
 * name on disk is not substituted when target memory is inaccessible.
 * Include after the common Windows read helpers and related-layout header.
 *
 * Primary PE/debug layout and module-enumeration definitions:
 * https://learn.microsoft.com/windows/win32/debug/pe-format
 * https://learn.microsoft.com/windows/win32/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot
 * https://learn.microsoft.com/windows/win32/api/tlhelp32/nf-tlhelp32-module32firstw
 * https://learn.microsoft.com/windows/win32/api/tlhelp32/ns-tlhelp32-moduleentry32w
 * https://github.com/microsoft/microsoft-pdb/blob/master/include/cvinfo.h
 * https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/include/llvm/Object/CVDebugRecord.h
 * https://learn.microsoft.com/windows/win32/winprog64/wow64-implementation-details
 *
 * Directory and path limits bound metadata processing only, not the number
 * of modules or heaps. Unknown/unreadable metadata selects prefix fallback.
 */

static bool xx_pswh_wow_target(const xx_psw_target *target)
{
    return target && target->version_available && target->major == 10 && target->minor == 0 && (target->build == 19045 || target->build == 26100) &&
           target->native_machine == 0x8664 && target->process_machine == 0x014c;
}

static bool xx_pswh_wow_module_matches(const xx_psw_target *target, unsigned width, const unsigned char guid[16], DWORD age)
{
    if (width != 4 || !guid || !age || !xx_pswh_wow_target(target) || !target->wow64_ntdll_identity || target->wow64_ntdll_age != age) return false;
    for (size_t i = 0; i < 16; ++i)
        if (target->wow64_ntdll_guid[i] != guid[i]) return false;
    return true;
}

static unsigned xx_pswh_wow_ascii_lower(unsigned ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

static bool xx_pswh_wow_module_name(const WCHAR *path, size_t capacity)
{
    static const char expected[] = "ntdll.dll";
    size_t length = 0, basename = 0;
    while (length < capacity && path[length]) {
        if (path[length] == L'\\' || path[length] == L'/') basename = length + 1;
        ++length;
    }
    if (length == capacity || length - basename != sizeof(expected) - 1) return false;
    for (size_t i = 0; i < sizeof(expected) - 1; ++i)
        if (xx_pswh_wow_ascii_lower((unsigned)path[basename + i]) != (unsigned)expected[i]) return false;
    return true;
}

static bool xx_pswh_wow_pdb_name(const unsigned char *path, size_t capacity)
{
    static const char expected[] = "wntdll.pdb";
    size_t length = 0, basename = 0;
    while (length < capacity && path[length]) {
        if (path[length] == '\\' || path[length] == '/') basename = length + 1;
        ++length;
    }
    if (length == capacity || length - basename != sizeof(expected) - 1) return false;
    for (size_t i = 0; i < sizeof(expected) - 1; ++i)
        if (xx_pswh_wow_ascii_lower(path[basename + i]) != (unsigned)expected[i]) return false;
    return true;
}

/* All image reads fit both the enumerated mapping and the explicit PE32 RVA
 * range. Last-addressed-byte checks permit a span ending at UINT32_MAX. */
static bool xx_pswh_wow_image_read(xx_io_device *device, uint64_t base, uint64_t image_size, uint64_t rva, void *bytes, size_t size)
{
    return base && base <= UINT32_MAX && image_size && image_size - 1 <= UINT32_MAX - base && rva <= image_size && (uint64_t)size <= image_size - rva &&
           xx_psw_read_at(device, base, rva, bytes, size);
}

static bool xx_pswh_wow_pe_identity(xx_io_device *device, uint64_t base, DWORD module_size, unsigned char guid[16], DWORD *age)
{
    unsigned char dos[64], coff[24], optional[152], debug[28], rsds[24], verify[24], path[1024];
    uint64_t nt, optional_rva, optional_end, image_size, headers_size, directories;
    uint64_t debug_rva, debug_size, match_rva = 0;
    uint64_t candidate_age = 0;
    unsigned char candidate_guid[16];
    unsigned optional_size, sections;
    bool found = false;
    if (!xx_pswh_wow_image_read(device, base, module_size, 0, dos, sizeof(dos)) || dos[0] != 'M' || dos[1] != 'Z') return false;
    nt = xx_psw_le(dos + 60, 4);
    if (nt < sizeof(dos) || !xx_pswh_wow_image_read(device, base, module_size, nt, coff, sizeof(coff)) || coff[0] != 'P' || coff[1] != 'E' || coff[2] || coff[3] ||
        xx_psw_le(coff + 4, 2) != 0x014c)
        return false;
    sections = (unsigned)xx_psw_le(coff + 6, 2);
    optional_size = (unsigned)xx_psw_le(coff + 20, 2);
    optional_rva = nt + sizeof(coff);
    if (!sections || optional_size < sizeof(optional) || optional_rva > module_size || optional_size > (uint64_t)module_size - optional_rva ||
        !xx_pswh_wow_image_read(device, base, module_size, optional_rva, optional, sizeof(optional)) || xx_psw_le(optional, 2) != 0x010b)
        return false;
    image_size = xx_psw_le(optional + 56, 4);
    headers_size = xx_psw_le(optional + 60, 4);
    directories = xx_psw_le(optional + 92, 4);
    optional_end = optional_rva + optional_size;
    if (image_size != module_size || !headers_size || headers_size > image_size || optional_end > headers_size || (uint64_t)sections * 40 > headers_size - optional_end ||
        directories < 7 || directories > (optional_size - 96u) / 8u)
        return false;
    debug_rva = xx_psw_le(optional + 144, 4);
    debug_size = xx_psw_le(optional + 148, 4);
    if (!debug_rva || !debug_size || debug_size > 65536 || debug_size % sizeof(debug) || debug_rva > image_size || debug_size > image_size - debug_rva) return false;
    for (uint64_t offset = 0; offset < debug_size; offset += sizeof(debug)) {
        uint64_t data_rva, data_size, path_size, this_age;
        if (!xx_pswh_wow_image_read(device, base, image_size, debug_rva + offset, debug, sizeof(debug))) return false;
        if (xx_psw_le(debug + 12, 4) != 2) continue; /* IMAGE_DEBUG_TYPE_CODEVIEW */
        data_size = xx_psw_le(debug + 16, 4);
        data_rva = xx_psw_le(debug + 20, 4); /* AddressOfRawData is an RVA; ignore file offset. */
        if (!data_rva || data_size < sizeof(rsds) + sizeof("wntdll.pdb") || data_size > 65536 || data_rva > image_size || data_size > image_size - data_rva ||
            !xx_pswh_wow_image_read(device, base, image_size, data_rva, rsds, sizeof(rsds)))
            return false;
        if (rsds[0] != 'R' || rsds[1] != 'S' || rsds[2] != 'D' || rsds[3] != 'S') continue;
        path_size = data_size - sizeof(rsds);
        if (path_size > sizeof(path)) path_size = sizeof(path);
        if (!xx_pswh_wow_image_read(device, base, image_size, data_rva + sizeof(rsds), path, (size_t)path_size) || !xx_pswh_wow_pdb_name(path, (size_t)path_size))
            return false;
        this_age = xx_psw_le(rsds + 20, 4);
        if (!this_age) return false;
        if (found) {
            if (this_age != candidate_age) return false;
            for (size_t i = 0; i < sizeof(candidate_guid); ++i)
                if (candidate_guid[i] != rsds[4 + i]) return false;
        } else {
            for (size_t i = 0; i < sizeof(candidate_guid); ++i) candidate_guid[i] = rsds[4 + i];
            candidate_age = this_age;
            match_rva = data_rva;
            found = true;
        }
    }
    /* Reject a visible replacement of the RSDS record during enumeration. */
    if (!found || !xx_pswh_wow_image_read(device, base, image_size, match_rva, verify, sizeof(verify)) || verify[0] != 'R' || verify[1] != 'S' || verify[2] != 'D' ||
        verify[3] != 'S' || xx_psw_le(verify + 20, 4) != candidate_age)
        return false;
    for (size_t i = 0; i < sizeof(candidate_guid); ++i)
        if (candidate_guid[i] != verify[4 + i]) return false;
    for (size_t i = 0; i < sizeof(candidate_guid); ++i) guid[i] = candidate_guid[i];
    *age = (DWORD)candidate_age;
    return true;
}

static void xx_pswh_identify_wow_module(xx_ps_context *context, xx_io_device *device, xx_psw_target *target)
{
    MODULEENTRY32W module;
    HANDLE snapshot;
    DWORD flags, age = 0;
    unsigned char guid[16];
    bool found = false;
    if (!target) return;
    target->wow64_ntdll_identity = false;
    target->wow64_ntdll_age = 0;
    for (size_t i = 0; i < sizeof(target->wow64_ntdll_guid); ++i) target->wow64_ntdll_guid[i] = 0;
    if (!xx_pswh_wow_target(target) || !device || !context || !context->snapshot || context->status != XX_PROCESS_STRUCTS_OK || !context->snapshot->pid ||
        context->snapshot->pid > UINT32_MAX)
        return;
    flags = sizeof(void *) == 8 ? TH32CS_SNAPMODULE32 : TH32CS_SNAPMODULE;
    snapshot = CreateToolhelp32Snapshot(flags, (DWORD)context->snapshot->pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        xx_psw_warn_once(context, "The loaded WOW64 NTDLL identity was unavailable; full heap layouts were not selected.");
        return;
    }
    memset(&module, 0, sizeof(module));
    module.dwSize = sizeof(module);
    if (Module32FirstW(snapshot, &module)) do {
            if (!xx_pswh_wow_module_name(module.szModule, XX_PSW_COUNT(module.szModule))) continue;
            found = xx_pswh_wow_pe_identity(device, (uint64_t)(uintptr_t)module.modBaseAddr, module.modBaseSize, guid, &age);
            break; /* A failed identity check uses prefix fallback, never a file lookup. */
        } while (Module32NextW(snapshot, &module));
    CloseHandle(snapshot);
    if (!found) {
        xx_psw_warn_once(context, "The loaded WOW64 NTDLL identity was unavailable; full heap layouts were not selected.");
        return;
    }
    for (size_t i = 0; i < sizeof(target->wow64_ntdll_guid); ++i) target->wow64_ntdll_guid[i] = guid[i];
    target->wow64_ntdll_age = age;
    target->wow64_ntdll_identity = true;
}

#endif /* XX_PROCESS_WOW64_HEAP_IDENTITY_H */
