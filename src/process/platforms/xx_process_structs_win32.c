/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "../xx_process_structs_internal.h"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <limits.h>
#include "../xx_process_structs_runtime.h"

/* Exact symbol-derived private NT layouts are selected by build and machine.
 * Other builds use explicitly labeled SDK views and verified core prefixes;
 * unlisted private tails are not inferred. Target offsets are explicit;
 * the compiler's layout of host pointer types never determines target offsets.
 * Sources (Microsoft may change these internal layouts in later Windows):
 * https://learn.microsoft.com/windows/win32/api/winternl/ns-winternl-peb
 * https://learn.microsoft.com/windows/win32/api/winternl/ns-winternl-teb
 * https://learn.microsoft.com/windows/win32/api/winternl/ns-winternl-peb_ldr_data
 * https://learn.microsoft.com/windows/win32/api/winternl/ns-winternl-rtl_user_process_parameters
 * https://learn.microsoft.com/windows/win32/api/ntdef/ns-ntdef-_unicode_string
 * https://learn.microsoft.com/windows/win32/api/winternl/nf-winternl-ntqueryinformationprocess
 * https://learn.microsoft.com/windows/win32/api/winternl/nf-winternl-ntqueryinformationthread
 * https://learn.microsoft.com/windows-hardware/drivers/debugger/debugging-a-stack-overflow
 * https://devblogs.microsoft.com/oldnewthing/20190418-00/?p=102428
 * https://learn.microsoft.com/windows/win32/api/winnt/ns-winnt-io_counters
 * https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-getprocessiocounters
 * https://learn.microsoft.com/windows/win32/api/minwinbase/ns-minwinbase-filetime
 * https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes
 * https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodeprocess
 * https://learn.microsoft.com/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex
 * https://learn.microsoft.com/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo
 * https://raw.githubusercontent.com/microsoft/win32metadata/main/generation/WinSDK/RecompiledIdlHeaders/um/winternl.h
 * https://learn.microsoft.com/windows-hardware/drivers/ddi/wdm/nf-wdm-rtlgetversion
 * https://learn.microsoft.com/windows/win32/api/wow64apiset/nf-wow64apiset-iswow64process2
 * https://raw.githubusercontent.com/winsiderss/phnt/master/ntrtl.h
 */
#define XX_PSW_PTR (XX_DATA_STRUCT_RECORD_PROPERTY_POINTER | XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
#define XX_PSW_RESERVED XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED
#define XX_PSW_SIZE XX_DATA_STRUCT_RECORD_PROPERTY_SIZE
#define XX_PSW_ID XX_DATA_STRUCT_RECORD_PROPERTY_ID
#define XX_PSW_FIELD(n, t, o, s, p) {L##n, L##t, o, s, p}
#define XX_PSW_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#include "xx_process_peb_layouts.h"
#include "xx_process_teb_layouts.h"

typedef struct xx_psw_target {
    DWORD major, minor, build;
    USHORT process_machine, native_machine;
    bool version_available;
    unsigned char wow64_ntdll_guid[16]; /* Loaded guest module's RSDS bytes. */
    DWORD wow64_ntdll_age;
    bool wow64_ntdll_identity;
} xx_psw_target;
typedef struct xx_psw_nt_layout {
    DWORD build;
    USHORT machine;
    const char *label;
    const xx_data_struct_field_desc *fields;
    size_t count, size;
} xx_psw_nt_layout;
/* Concrete symbol-derived profiles are deliberately build and architecture
 * specific. Pointer width alone does not select an AMD64 layout on ARM64. */
static const xx_psw_nt_layout xx_psw_peb_profiles[] = {
    {19045, 0x014c, "NT 10.0.19045 I386/WOW64 PEB symbol profile", xx_psw_peb32_19045, XX_PSW_COUNT(xx_psw_peb32_19045), XX_PSW_PEB32_19045_SIZE},
    {19045, 0x8664, "NT 10.0.19045 AMD64 PEB symbol profile", xx_psw_peb64_19045, XX_PSW_COUNT(xx_psw_peb64_19045), XX_PSW_PEB64_19045_SIZE},
#ifdef XX_PSW_PEB32_26100_SIZE
    {26100, 0x014c, "NT 10.0.26100 WOW64 PEB32 symbol profile", xx_psw_peb32_26100, XX_PSW_COUNT(xx_psw_peb32_26100), XX_PSW_PEB32_26100_SIZE},
#endif
    {26100, 0x8664, "NT 10.0.26100 AMD64 PEB symbol profile", xx_psw_peb64_26100, XX_PSW_COUNT(xx_psw_peb64_26100), XX_PSW_PEB64_26100_SIZE}};
static const xx_psw_nt_layout xx_psw_teb_profiles[] = {
    {19045, 0x014c, "NT 10.0.19045 I386/WOW64 TEB symbol profile", xx_psw_teb32_19045, XX_PSW_COUNT(xx_psw_teb32_19045), XX_PSW_TEB32_19045_SIZE},
    {19045, 0x8664, "NT 10.0.19045 AMD64 TEB symbol profile", xx_psw_teb64_19045, XX_PSW_COUNT(xx_psw_teb64_19045), XX_PSW_TEB64_19045_SIZE},
#ifdef XX_PSW_TEB32_26100_SIZE
    {26100, 0x014c, "NT 10.0.26100 WOW64 TEB32 symbol profile", xx_psw_teb32_26100, XX_PSW_COUNT(xx_psw_teb32_26100), XX_PSW_TEB32_26100_SIZE},
#endif
    {26100, 0x8664, "NT 10.0.26100 AMD64 TEB symbol profile", xx_psw_teb64_26100, XX_PSW_COUNT(xx_psw_teb64_26100), XX_PSW_TEB64_26100_SIZE}};

static USHORT xx_psw_profile_machine(const xx_psw_target *target, unsigned width)
{
    if (target->native_machine == 0x8664) {
        if (width == 8 && (target->process_machine == 0x8664 || target->process_machine == 0x014c)) return 0x8664;
        if (width == 4 && target->process_machine == 0x014c) return 0x014c;
    } else if (target->native_machine == 0x014c && target->process_machine == 0x014c && width == 4) return 0x014c;
    return 0;
}

static const xx_psw_nt_layout *xx_psw_profile(const xx_psw_target *target, unsigned width, bool teb)
{
    const xx_psw_nt_layout *profiles = teb ? xx_psw_teb_profiles : xx_psw_peb_profiles;
    size_t count = teb ? XX_PSW_COUNT(xx_psw_teb_profiles) : XX_PSW_COUNT(xx_psw_peb_profiles);
    USHORT machine = xx_psw_profile_machine(target, width);
    if (!target->version_available || target->major != 10 || target->minor != 0 || !machine) return NULL;
    for (size_t i = 0; i < count; ++i)
        if (profiles[i].build == target->build && profiles[i].machine == machine) return &profiles[i];
    return NULL;
}

static void xx_psw_fallback_label(const xx_psw_target *target, unsigned width, char *label, size_t capacity)
{
    if (target->version_available)
        (void)snprintf(label, capacity, "SDK fallback: NT %lu.%lu.%lu, %u-bit; full private profile unavailable", (unsigned long)target->major,
                       (unsigned long)target->minor, (unsigned long)target->build, width * 8);
    else (void)snprintf(label, capacity, "SDK fallback: OS version unavailable, %u-bit; full private profile unavailable", width * 8);
}

static const xx_data_struct_field_desc xx_psw_peb_core32[] = {XX_PSW_FIELD("ImageBaseAddress", "PVOID32", 8, 4, XX_PSW_PTR),
                                                              XX_PSW_FIELD("ProcessHeap", "PVOID32", 24, 4, XX_PSW_PTR)};
static const xx_data_struct_field_desc xx_psw_peb_core64[] = {XX_PSW_FIELD("ImageBaseAddress", "PVOID64", 16, 8, XX_PSW_PTR),
                                                              XX_PSW_FIELD("ProcessHeap", "PVOID64", 48, 8, XX_PSW_PTR)};
static const xx_data_struct_field_desc xx_psw_teb_core32[] = {
    XX_PSW_FIELD("ClientId", "CLIENT_ID32[1]", 32, 8, XX_PSW_ID), XX_PSW_FIELD("ClientId.UniqueProcess", "HANDLE32", 32, 4, XX_PSW_ID),
    XX_PSW_FIELD("ClientId.UniqueThread", "HANDLE32", 36, 4, XX_PSW_ID), XX_PSW_FIELD("LastErrorValue", "ULONG", 52, 4, 0)};
static const xx_data_struct_field_desc xx_psw_teb_core64[] = {
    XX_PSW_FIELD("ClientId", "CLIENT_ID64[1]", 64, 16, XX_PSW_ID), XX_PSW_FIELD("ClientId.UniqueProcess", "HANDLE64", 64, 8, XX_PSW_ID),
    XX_PSW_FIELD("ClientId.UniqueThread", "HANDLE64", 72, 8, XX_PSW_ID), XX_PSW_FIELD("LastErrorValue", "ULONG", 104, 4, 0)};

static const xx_data_struct_field_desc xx_psw_peb32[] = {XX_PSW_FIELD("Reserved1", "BYTE[2]", 0, 2, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("BeingDebugged", "BYTE", 2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                         XX_PSW_FIELD("Reserved2", "BYTE", 3, 1, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Reserved3", "PVOID32[2]", 4, 8, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Ldr", "PPEB_LDR_DATA32", 12, 4, XX_PSW_PTR),
                                                         XX_PSW_FIELD("ProcessParameters", "PRTL_USER_PROCESS_PARAMETERS32", 16, 4, XX_PSW_PTR),
                                                         XX_PSW_FIELD("Reserved4", "PVOID32[3]", 20, 12, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("AtlThunkSListPtr", "PVOID32", 32, 4, XX_PSW_PTR),
                                                         XX_PSW_FIELD("Reserved5", "PVOID32[1]", 36, 4, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Reserved6", "ULONG", 40, 4, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Reserved7", "PVOID32[1]", 44, 4, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Reserved8", "ULONG", 48, 4, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("AtlThunkSListPtr32", "ULONG", 52, 4, XX_PSW_PTR),
                                                         XX_PSW_FIELD("Reserved9", "PVOID32[45]", 56, 180, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Reserved10", "BYTE[96]", 236, 96, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("PostProcessInitRoutine", "PPS_POST_PROCESS_INIT_ROUTINE32", 332, 4, XX_PSW_PTR),
                                                         XX_PSW_FIELD("Reserved11", "BYTE[128]", 336, 128, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Reserved12", "PVOID32[1]", 464, 4, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("SessionId", "ULONG", 468, 4, XX_PSW_ID)};
static const xx_data_struct_field_desc xx_psw_peb64[] = {XX_PSW_FIELD("Reserved1", "BYTE[2]", 0, 2, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("BeingDebugged", "BYTE", 2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                         XX_PSW_FIELD("Reserved2", "BYTE[1]", 3, 1, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("AlignmentPadding0", "BYTE[4]", 4, 4, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Reserved3", "PVOID64[2]", 8, 16, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Ldr", "PPEB_LDR_DATA64", 24, 8, XX_PSW_PTR),
                                                         XX_PSW_FIELD("ProcessParameters", "PRTL_USER_PROCESS_PARAMETERS64", 32, 8, XX_PSW_PTR),
                                                         XX_PSW_FIELD("Reserved4", "PVOID64[3]", 40, 24, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("AtlThunkSListPtr", "PVOID64", 64, 8, XX_PSW_PTR),
                                                         XX_PSW_FIELD("Reserved5", "PVOID64[1]", 72, 8, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Reserved6", "ULONG", 80, 4, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("AlignmentPadding1", "BYTE[4]", 84, 4, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Reserved7", "PVOID64[1]", 88, 8, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Reserved8", "ULONG", 96, 4, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("AtlThunkSListPtr32", "ULONG", 100, 4, XX_PSW_PTR),
                                                         XX_PSW_FIELD("Reserved9", "PVOID64[45]", 104, 360, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Reserved10", "BYTE[96]", 464, 96, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("PostProcessInitRoutine", "PPS_POST_PROCESS_INIT_ROUTINE64", 560, 8, XX_PSW_PTR),
                                                         XX_PSW_FIELD("Reserved11", "BYTE[128]", 568, 128, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Reserved12", "PVOID64[1]", 696, 8, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("SessionId", "ULONG", 704, 4, XX_PSW_ID),
                                                         XX_PSW_FIELD("AlignmentPadding2", "BYTE[4]", 708, 4, XX_PSW_RESERVED)};
static const xx_data_struct_field_desc xx_psw_teb32[] = {XX_PSW_FIELD("Reserved1", "PVOID32[12]", 0, 48, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("NtTib", "NT_TIB32[1]", 0, 28, 0),
                                                         XX_PSW_FIELD("Reserved1Tail", "PVOID32[5]", 28, 20, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("ProcessEnvironmentBlock", "PPEB32", 48, 4, XX_PSW_PTR),
                                                         XX_PSW_FIELD("Reserved2", "PVOID32[399]", 52, 1596, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Reserved3", "BYTE[1952]", 1648, 1952, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("TlsSlots", "PVOID32[64]", 3600, 256, 0),
                                                         XX_PSW_FIELD("Reserved4", "BYTE[8]", 3856, 8, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Reserved5", "PVOID32[26]", 3864, 104, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("ReservedForOle", "PVOID32", 3968, 4, XX_PSW_PTR),
                                                         XX_PSW_FIELD("Reserved6", "PVOID32[4]", 3972, 16, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("TlsExpansionSlots", "PVOID32", 3988, 4, XX_PSW_PTR)};
static const xx_data_struct_field_desc xx_psw_teb64[] = {XX_PSW_FIELD("Reserved1", "PVOID64[12]", 0, 96, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("NtTib", "NT_TIB64[1]", 0, 56, 0),
                                                         XX_PSW_FIELD("Reserved1Tail", "PVOID64[5]", 56, 40, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("ProcessEnvironmentBlock", "PPEB64", 96, 8, XX_PSW_PTR),
                                                         XX_PSW_FIELD("Reserved2", "PVOID64[399]", 104, 3192, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Reserved3", "BYTE[1952]", 3296, 1952, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("TlsSlots", "PVOID64[64]", 5248, 512, 0),
                                                         XX_PSW_FIELD("Reserved4", "BYTE[8]", 5760, 8, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("Reserved5", "PVOID64[26]", 5768, 208, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("ReservedForOle", "PVOID64", 5976, 8, XX_PSW_PTR),
                                                         XX_PSW_FIELD("Reserved6", "PVOID64[4]", 5984, 32, XX_PSW_RESERVED),
                                                         XX_PSW_FIELD("TlsExpansionSlots", "PVOID64", 6016, 8, XX_PSW_PTR)};
/* Every descriptor string remains a static literal, including array indices. */
#define XX_PSW_TLS_FIELD(n, w, t) XX_PSW_FIELD("TlsSlots[" #n "]", t, (n) * (w), w, XX_PSW_PTR)
#define XX_PSW_TLS_FIELDS(w, t)                                                                                                                                       \
    XX_PSW_TLS_FIELD(0, w, t), XX_PSW_TLS_FIELD(1, w, t), XX_PSW_TLS_FIELD(2, w, t), XX_PSW_TLS_FIELD(3, w, t), XX_PSW_TLS_FIELD(4, w, t), XX_PSW_TLS_FIELD(5, w, t), \
        XX_PSW_TLS_FIELD(6, w, t), XX_PSW_TLS_FIELD(7, w, t), XX_PSW_TLS_FIELD(8, w, t), XX_PSW_TLS_FIELD(9, w, t), XX_PSW_TLS_FIELD(10, w, t),                       \
        XX_PSW_TLS_FIELD(11, w, t), XX_PSW_TLS_FIELD(12, w, t), XX_PSW_TLS_FIELD(13, w, t), XX_PSW_TLS_FIELD(14, w, t), XX_PSW_TLS_FIELD(15, w, t),                   \
        XX_PSW_TLS_FIELD(16, w, t), XX_PSW_TLS_FIELD(17, w, t), XX_PSW_TLS_FIELD(18, w, t), XX_PSW_TLS_FIELD(19, w, t), XX_PSW_TLS_FIELD(20, w, t),                   \
        XX_PSW_TLS_FIELD(21, w, t), XX_PSW_TLS_FIELD(22, w, t), XX_PSW_TLS_FIELD(23, w, t), XX_PSW_TLS_FIELD(24, w, t), XX_PSW_TLS_FIELD(25, w, t),                   \
        XX_PSW_TLS_FIELD(26, w, t), XX_PSW_TLS_FIELD(27, w, t), XX_PSW_TLS_FIELD(28, w, t), XX_PSW_TLS_FIELD(29, w, t), XX_PSW_TLS_FIELD(30, w, t),                   \
        XX_PSW_TLS_FIELD(31, w, t), XX_PSW_TLS_FIELD(32, w, t), XX_PSW_TLS_FIELD(33, w, t), XX_PSW_TLS_FIELD(34, w, t), XX_PSW_TLS_FIELD(35, w, t),                   \
        XX_PSW_TLS_FIELD(36, w, t), XX_PSW_TLS_FIELD(37, w, t), XX_PSW_TLS_FIELD(38, w, t), XX_PSW_TLS_FIELD(39, w, t), XX_PSW_TLS_FIELD(40, w, t),                   \
        XX_PSW_TLS_FIELD(41, w, t), XX_PSW_TLS_FIELD(42, w, t), XX_PSW_TLS_FIELD(43, w, t), XX_PSW_TLS_FIELD(44, w, t), XX_PSW_TLS_FIELD(45, w, t),                   \
        XX_PSW_TLS_FIELD(46, w, t), XX_PSW_TLS_FIELD(47, w, t), XX_PSW_TLS_FIELD(48, w, t), XX_PSW_TLS_FIELD(49, w, t), XX_PSW_TLS_FIELD(50, w, t),                   \
        XX_PSW_TLS_FIELD(51, w, t), XX_PSW_TLS_FIELD(52, w, t), XX_PSW_TLS_FIELD(53, w, t), XX_PSW_TLS_FIELD(54, w, t), XX_PSW_TLS_FIELD(55, w, t),                   \
        XX_PSW_TLS_FIELD(56, w, t), XX_PSW_TLS_FIELD(57, w, t), XX_PSW_TLS_FIELD(58, w, t), XX_PSW_TLS_FIELD(59, w, t), XX_PSW_TLS_FIELD(60, w, t),                   \
        XX_PSW_TLS_FIELD(61, w, t), XX_PSW_TLS_FIELD(62, w, t), XX_PSW_TLS_FIELD(63, w, t)
static const xx_data_struct_field_desc xx_psw_tls32[] = {XX_PSW_TLS_FIELDS(4, "PVOID32")};
static const xx_data_struct_field_desc xx_psw_tls64[] = {XX_PSW_TLS_FIELDS(8, "PVOID64")};
#undef XX_PSW_TLS_FIELDS
#undef XX_PSW_TLS_FIELD
static const xx_data_struct_field_desc xx_psw_list32[] = {XX_PSW_FIELD("Flink", "PLIST_ENTRY32", 0, 4, XX_PSW_PTR),
                                                          XX_PSW_FIELD("Blink", "PLIST_ENTRY32", 4, 4, XX_PSW_PTR)};
static const xx_data_struct_field_desc xx_psw_list64[] = {XX_PSW_FIELD("Flink", "PLIST_ENTRY64", 0, 8, XX_PSW_PTR),
                                                          XX_PSW_FIELD("Blink", "PLIST_ENTRY64", 8, 8, XX_PSW_PTR)};
/* NT_TIB is the published seven-pointer prefix at the TEB address. Its view
 * overlays the SDK TEB Reserved1 span; later exposed SDK TEB fields are
 * decoded separately, while their intervening reserved spans stay opaque. */
static const xx_data_struct_field_desc xx_psw_tib32[] = {XX_PSW_FIELD("ExceptionList", "PEXCEPTION_REGISTRATION_RECORD32", 0, 4, XX_PSW_PTR),
                                                         XX_PSW_FIELD("StackBase", "PVOID32", 4, 4, XX_PSW_PTR),
                                                         XX_PSW_FIELD("StackLimit", "PVOID32", 8, 4, XX_PSW_PTR),
                                                         XX_PSW_FIELD("SubSystemTib", "PVOID32", 12, 4, XX_PSW_PTR),
                                                         XX_PSW_FIELD("FiberData", "PVOID32", 16, 4, XX_PSW_PTR),
                                                         XX_PSW_FIELD("ArbitraryUserPointer", "PVOID32", 20, 4, XX_PSW_PTR),
                                                         XX_PSW_FIELD("Self", "PNT_TIB32", 24, 4, XX_PSW_PTR)};
static const xx_data_struct_field_desc xx_psw_tib64[] = {XX_PSW_FIELD("ExceptionList", "PEXCEPTION_REGISTRATION_RECORD64", 0, 8, XX_PSW_PTR),
                                                         XX_PSW_FIELD("StackBase", "PVOID64", 8, 8, XX_PSW_PTR),
                                                         XX_PSW_FIELD("StackLimit", "PVOID64", 16, 8, XX_PSW_PTR),
                                                         XX_PSW_FIELD("SubSystemTib", "PVOID64", 24, 8, XX_PSW_PTR),
                                                         XX_PSW_FIELD("FiberData", "PVOID64", 32, 8, XX_PSW_PTR),
                                                         XX_PSW_FIELD("ArbitraryUserPointer", "PVOID64", 40, 8, XX_PSW_PTR),
                                                         XX_PSW_FIELD("Self", "PNT_TIB64", 48, 8, XX_PSW_PTR)};
static const xx_data_struct_field_desc xx_psw_ldr32[] = {
    XX_PSW_FIELD("Reserved1", "BYTE[8]", 0, 8, XX_PSW_RESERVED), XX_PSW_FIELD("Reserved2", "PVOID32[3]", 8, 12, XX_PSW_RESERVED),
    XX_PSW_FIELD("InMemoryOrderModuleList.Flink", "PLIST_ENTRY32", 20, 4, XX_PSW_PTR), XX_PSW_FIELD("InMemoryOrderModuleList.Blink", "PLIST_ENTRY32", 24, 4, XX_PSW_PTR)};
static const xx_data_struct_field_desc xx_psw_ldr64[] = {
    XX_PSW_FIELD("Reserved1", "BYTE[8]", 0, 8, XX_PSW_RESERVED), XX_PSW_FIELD("Reserved2", "PVOID64[3]", 8, 24, XX_PSW_RESERVED),
    XX_PSW_FIELD("InMemoryOrderModuleList.Flink", "PLIST_ENTRY64", 32, 8, XX_PSW_PTR), XX_PSW_FIELD("InMemoryOrderModuleList.Blink", "PLIST_ENTRY64", 40, 8, XX_PSW_PTR)};
static const xx_data_struct_field_desc xx_psw_params32[] = {
    XX_PSW_FIELD("Reserved1", "BYTE[16]", 0, 16, XX_PSW_RESERVED), XX_PSW_FIELD("Reserved2", "PVOID32[10]", 16, 40, XX_PSW_RESERVED),
    XX_PSW_FIELD("ImagePathName", "UNICODE_STRING32[1]", 56, 8, 0), XX_PSW_FIELD("CommandLine", "UNICODE_STRING32[1]", 64, 8, 0)};
static const xx_data_struct_field_desc xx_psw_params64[] = {
    XX_PSW_FIELD("Reserved1", "BYTE[16]", 0, 16, XX_PSW_RESERVED), XX_PSW_FIELD("Reserved2", "PVOID64[10]", 16, 80, XX_PSW_RESERVED),
    XX_PSW_FIELD("ImagePathName", "UNICODE_STRING64[1]", 96, 16, 0), XX_PSW_FIELD("CommandLine", "UNICODE_STRING64[1]", 112, 16, 0)};
static const xx_data_struct_field_desc xx_psw_unicode32[] = {
    XX_PSW_FIELD("Length", "USHORT", 0, 2, XX_PSW_SIZE), XX_PSW_FIELD("MaximumLength", "USHORT", 2, 2, XX_PSW_SIZE), XX_PSW_FIELD("Buffer", "PWSTR32", 4, 4, XX_PSW_PTR)};
static const xx_data_struct_field_desc xx_psw_unicode64[] = {
    XX_PSW_FIELD("Length", "USHORT", 0, 2, XX_PSW_SIZE), XX_PSW_FIELD("MaximumLength", "USHORT", 2, 2, XX_PSW_SIZE), XX_PSW_FIELD("Buffer", "PWSTR64", 8, 8, XX_PSW_PTR)};
static const xx_data_struct_field_desc xx_psw_entry32[] = {XX_PSW_FIELD("Reserved1", "PVOID32[2]", 0, 8, XX_PSW_RESERVED),
                                                           XX_PSW_FIELD("InMemoryOrderLinks.Flink", "PLIST_ENTRY32", 8, 4, XX_PSW_PTR),
                                                           XX_PSW_FIELD("InMemoryOrderLinks.Blink", "PLIST_ENTRY32", 12, 4, XX_PSW_PTR),
                                                           XX_PSW_FIELD("Reserved2", "PVOID32[2]", 16, 8, XX_PSW_RESERVED),
                                                           XX_PSW_FIELD("DllBase", "PVOID32", 24, 4, XX_PSW_PTR),
                                                           XX_PSW_FIELD("Reserved3", "PVOID32[2]", 28, 8, XX_PSW_RESERVED),
                                                           XX_PSW_FIELD("FullDllName", "UNICODE_STRING32[1]", 36, 8, 0),
                                                           XX_PSW_FIELD("Reserved4", "BYTE[8]", 44, 8, XX_PSW_RESERVED),
                                                           XX_PSW_FIELD("Reserved5", "PVOID32[3]", 52, 12, XX_PSW_RESERVED),
                                                           XX_PSW_FIELD("CheckSum", "ULONG", 64, 4, 0),
                                                           XX_PSW_FIELD("Reserved6", "PVOID32[1]", 64, 4, XX_PSW_RESERVED),
                                                           XX_PSW_FIELD("TimeDateStamp", "ULONG", 68, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP)};
static const xx_data_struct_field_desc xx_psw_entry64[] = {XX_PSW_FIELD("Reserved1", "PVOID64[2]", 0, 16, XX_PSW_RESERVED),
                                                           XX_PSW_FIELD("InMemoryOrderLinks.Flink", "PLIST_ENTRY64", 16, 8, XX_PSW_PTR),
                                                           XX_PSW_FIELD("InMemoryOrderLinks.Blink", "PLIST_ENTRY64", 24, 8, XX_PSW_PTR),
                                                           XX_PSW_FIELD("Reserved2", "PVOID64[2]", 32, 16, XX_PSW_RESERVED),
                                                           XX_PSW_FIELD("DllBase", "PVOID64", 48, 8, XX_PSW_PTR),
                                                           XX_PSW_FIELD("Reserved3", "PVOID64[2]", 56, 16, XX_PSW_RESERVED),
                                                           XX_PSW_FIELD("FullDllName", "UNICODE_STRING64[1]", 72, 16, 0),
                                                           XX_PSW_FIELD("Reserved4", "BYTE[8]", 88, 8, XX_PSW_RESERVED),
                                                           XX_PSW_FIELD("Reserved5", "PVOID64[3]", 96, 24, XX_PSW_RESERVED),
                                                           XX_PSW_FIELD("CheckSum", "ULONG", 120, 4, 0),
                                                           XX_PSW_FIELD("Reserved6", "PVOID64[1]", 120, 8, XX_PSW_RESERVED),
                                                           XX_PSW_FIELD("TimeDateStamp", "ULONG", 128, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP),
                                                           XX_PSW_FIELD("AlignmentPadding0", "BYTE[4]", 132, 4, XX_PSW_RESERVED)};
static const xx_data_struct_field_desc xx_psw_io_fields[] = {XX_PSW_FIELD("ReadOperationCount", "ULONGLONG", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
                                                             XX_PSW_FIELD("WriteOperationCount", "ULONGLONG", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
                                                             XX_PSW_FIELD("OtherOperationCount", "ULONGLONG", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
                                                             XX_PSW_FIELD("ReadTransferCount", "ULONGLONG", 24, 8, XX_PSW_SIZE),
                                                             XX_PSW_FIELD("WriteTransferCount", "ULONGLONG", 32, 8, XX_PSW_SIZE),
                                                             XX_PSW_FIELD("OtherTransferCount", "ULONGLONG", 40, 8, XX_PSW_SIZE)};
static const xx_data_struct_field_desc xx_psw_filetime_fields[] = {XX_PSW_FIELD("dwLowDateTime", "DWORD", 0, 4, 0), XX_PSW_FIELD("dwHighDateTime", "DWORD", 4, 4, 0)};
/* Native query output uses the caller's SDK ABI, including SIZE_T. Unlike
 * remote NT_TIB/PEB/TEB layouts, native output may safely use SDK offsetof. */
#define XX_PSW_MEMORY_FIELD(n, t, p) XX_PSW_FIELD(#n, t, offsetof(PROCESS_MEMORY_COUNTERS_EX, n), sizeof(((PROCESS_MEMORY_COUNTERS_EX *)0)->n), p)
static const xx_data_struct_field_desc xx_psw_memory_fields[] = {XX_PSW_MEMORY_FIELD(cb, "DWORD", XX_PSW_SIZE),
                                                                 XX_PSW_MEMORY_FIELD(PageFaultCount, "DWORD", XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
                                                                 XX_PSW_MEMORY_FIELD(PeakWorkingSetSize, "SIZE_T", XX_PSW_SIZE),
                                                                 XX_PSW_MEMORY_FIELD(WorkingSetSize, "SIZE_T", XX_PSW_SIZE),
                                                                 XX_PSW_MEMORY_FIELD(QuotaPeakPagedPoolUsage, "SIZE_T", XX_PSW_SIZE),
                                                                 XX_PSW_MEMORY_FIELD(QuotaPagedPoolUsage, "SIZE_T", XX_PSW_SIZE),
                                                                 XX_PSW_MEMORY_FIELD(QuotaPeakNonPagedPoolUsage, "SIZE_T", XX_PSW_SIZE),
                                                                 XX_PSW_MEMORY_FIELD(QuotaNonPagedPoolUsage, "SIZE_T", XX_PSW_SIZE),
                                                                 XX_PSW_MEMORY_FIELD(PagefileUsage, "SIZE_T", XX_PSW_SIZE),
                                                                 XX_PSW_MEMORY_FIELD(PeakPagefileUsage, "SIZE_T", XX_PSW_SIZE),
                                                                 XX_PSW_MEMORY_FIELD(PrivateUsage, "SIZE_T", XX_PSW_SIZE)};
#undef XX_PSW_MEMORY_FIELD

/* These structures describe the host ABI of native query output, not a target
 * memory layout. They are never used as a remote-memory read destination. */
typedef struct xx_psw_basic_information {
    LONG exit_status;
    PVOID peb;
    ULONG_PTR affinity;
    LONG priority;
    ULONG_PTR pid, parent_pid;
} xx_psw_basic_information;
typedef struct xx_psw_thread_information {
    LONG exit_status;
    PVOID teb;
    struct {
        HANDLE process, thread;
    } client;
    ULONG_PTR affinity;
    LONG priority, base_priority;
} xx_psw_thread_information;
typedef LONG(NTAPI *xx_psw_query_process_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef LONG(NTAPI *xx_psw_query_thread_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef BOOL(WINAPI *xx_psw_wow64_process2_fn)(HANDLE, USHORT *, USHORT *);
typedef BOOL(WINAPI *xx_psw_memory_info_fn)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
typedef LONG(NTAPI *xx_psw_get_version_fn)(OSVERSIONINFOW *);

static uint64_t xx_psw_le(const unsigned char *bytes, size_t length)
{
    uint64_t value = 0;
    size_t i;
    for (i = 0; i < length; ++i) value |= (uint64_t)bytes[i] << (8 * i);
    return value;
}

static bool xx_psw_read_at(xx_io_device *device, uint64_t base, uint64_t offset, void *bytes, size_t size)
{
    return device && offset <= UINT64_MAX - base && xx_ps_read(device, base + offset, bytes, size);
}

static bool xx_psw_pointer(xx_io_device *device, uint64_t base, uint64_t offset, unsigned width, uint64_t *value)
{
    unsigned char bytes[8];
    if (!xx_psw_read_at(device, base, offset, bytes, width)) return false;
    *value = xx_psw_le(bytes, width);
    return true;
}

static bool xx_psw_full(xx_ps_context *context)
{
    return context->status != XX_PROCESS_STRUCTS_OK || context->snapshot->structure_count >= context->limits.max_structures;
}

static void xx_psw_warn_once(xx_ps_context *context, const char *message)
{
    if (!strstr(context->snapshot->warning, message)) xx_ps_warn(context, "%s", message);
}

/* Named bitmembers share their parent's physical raw bytes. Interpret only
 * the generated, bounded bitfield:SHIFT:BITS type, leaving raw unchanged. */
static void xx_psw_bitfields(xx_process_structure *structure)
{
    static const wchar_t prefix[] = L"bitfield:";
    for (size_t i = 0; i < structure->field_count; ++i) {
        xx_process_struct_field *field = structure->fields + i;
        const wchar_t *type = field->desc.type, *p;
        const char *raw;
        unsigned shift = 0, bits = 0, digits = 0;
        uint64_t storage = 0, value;
        size_t j;
        if (!field->available || !type || (field->desc.size != 1 && field->desc.size != 2 && field->desc.size != 4 && field->desc.size != 8)) continue;
        for (j = 0; prefix[j] && type[j] == prefix[j]; ++j) {
        }
        if (prefix[j]) continue;
        p = type + j;
        if (*p < L'0' || *p > L'9') continue;
        while (*p >= L'0' && *p <= L'9' && shift <= 64) shift = shift * 10 + (unsigned)(*p++ - L'0');
        if (shift > 63 || *p++ != L':' || *p < L'0' || *p > L'9') continue;
        while (*p >= L'0' && *p <= L'9' && bits <= 64) bits = bits * 10 + (unsigned)(*p++ - L'0');
        if (*p || !bits || bits > 64 || shift >= (unsigned)field->desc.size * 8 || bits > (unsigned)field->desc.size * 8 - shift) continue;
        raw = field->raw;
        if (raw[0] != '0' || (raw[1] != 'x' && raw[1] != 'X')) continue;
        raw += 2;
        while (*raw && digits < 16) {
            unsigned digit;
            if (*raw >= '0' && *raw <= '9') digit = (unsigned)(*raw - '0');
            else if (*raw >= 'A' && *raw <= 'F') digit = (unsigned)(*raw - 'A' + 10);
            else if (*raw >= 'a' && *raw <= 'f') digit = (unsigned)(*raw - 'a' + 10);
            else break;
            storage = (storage << 4) | digit;
            ++raw;
            ++digits;
        }
        if (*raw || !digits) continue;
        value = (storage >> shift) & (bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1);
        (void)snprintf(field->value, sizeof(field->value), "%llu", (unsigned long long)value);
        (void)snprintf(field->comment, sizeof(field->comment), "Bits %u..%u (%u-bit member); raw retains the complete storage value.", shift, shift + bits - 1, bits);
    }
}

/* A failed complete read does not turn unreadable fields into zeroes. Retry
 * individual fields so a guard or unmapped page within a large reserved span
 * does not hide independently readable named fields later in the structure. */
static xx_process_structure *xx_psw_memory(xx_ps_context *context, xx_io_device *device, const char *name, const char *layout, uint64_t address, size_t size,
                                           uint64_t thread, const xx_data_struct_field_desc *fields, size_t count)
{
    unsigned char *bytes;
    xx_process_structure *structure;
    size_t i;
    bool complete, decoded = false;
    if (!size || size > 65536) {
        context->status = XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        if (fields[i].rel_offset < 0 || fields[i].size < 0 || (uint64_t)fields[i].rel_offset > size || (uint64_t)fields[i].size > size - (size_t)fields[i].rel_offset) {
            context->status = XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
            return NULL;
        }
    }
    structure = xx_ps_add(context, name, layout, address, true, size, thread);
    if (!structure) return NULL;
    bytes = (unsigned char *)calloc(size, 1);
    if (!bytes) {
        context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
        return NULL;
    }
    complete = xx_psw_read_at(device, address, 0, bytes, size);
    if (complete) {
        if (!xx_ps_decode(context, structure, fields, count, bytes, size, false)) goto done;
    } else {
        for (i = 0; i < count; ++i) {
            const xx_data_struct_field_desc *field = fields + i;
            size_t offset = (size_t)field->rel_offset, length = (size_t)field->size;
            bool available;
            available = xx_psw_read_at(device, address, offset, bytes + offset, length);
            if (!xx_ps_decode(context, structure, field, 1, available ? bytes : NULL, available ? offset + length : 0, false)) goto done;
        }
        xx_psw_warn_once(context, "Some target-memory fields were unreadable.");
    }
    xx_psw_bitfields(structure);
    decoded = true;
done:
    free(bytes);
    return decoded ? structure : NULL;
}

/* Add the requested stable x86/AMD64 prefix names to an SDK fallback view.
 * Do not reuse these private aliases for an unverified ARM or other ABI. */
static bool xx_psw_core_fields(xx_ps_context *context, xx_io_device *device, xx_process_structure *structure, const xx_data_struct_field_desc *fields, size_t count)
{
    unsigned char bytes[128];
    for (size_t i = 0; i < count; ++i) {
        size_t offset = (size_t)fields[i].rel_offset, size = (size_t)fields[i].size;
        bool available;
        if (offset > sizeof(bytes) || size > sizeof(bytes) - offset) {
            context->status = XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
            return false;
        }
        available = xx_psw_read_at(device, structure->address, offset, bytes + offset, size);
        if (!xx_ps_decode(context, structure, fields + i, 1, available ? bytes : NULL, available ? offset + size : 0, false)) return false;
        if (available)
            (void)snprintf(structure->fields[structure->field_count - 1].comment, sizeof(structure->fields[structure->field_count - 1].comment),
                           "Stable named x86/AMD64 prefix view; no matching full private build profile was selected.");
    }
    return true;
}

static void xx_psw_list(xx_ps_context *context, xx_io_device *device, const char *label, uint64_t address, unsigned width)
{
    char name[96];
    (void)snprintf(name, sizeof(name), "LIST_ENTRY%u (%s)", width * 8, label);
    (void)xx_psw_memory(context, device, name, "Complete published SDK LIST_ENTRY", address, 2 * width, 0, width == 8 ? xx_psw_list64 : xx_psw_list32, 2);
}

/* Definitions follow the shared remote-memory and Unicode helpers. */
static void xx_pswr_peb_children(xx_ps_context *context, xx_io_device *device, uint64_t address, unsigned width, const xx_psw_target *target);
static void xx_pswr_teb_children(xx_ps_context *context, xx_io_device *device, uint64_t address, unsigned width, uint64_t thread, const xx_psw_target *target);

static void xx_psw_teb(xx_ps_context *context, xx_io_device *device, const char *name, uint64_t address, unsigned width, uint64_t thread, const xx_psw_target *target)
{
    const xx_psw_nt_layout *profile = xx_psw_profile(target, width, true);
    char fallback[96];
    xx_psw_fallback_label(target, width, fallback, sizeof(fallback));
    xx_process_structure *tib, *structure = xx_psw_memory(context, device, name, profile ? profile->label : fallback, address,
                                                          profile      ? profile->size
                                                          : width == 8 ? 6024
                                                                       : 3992,
                                                          thread,
                                                          profile      ? profile->fields
                                                          : width == 8 ? xx_psw_teb64
                                                                       : xx_psw_teb32,
                                                          profile      ? profile->count
                                                          : width == 8 ? XX_PSW_COUNT(xx_psw_teb64)
                                                                       : XX_PSW_COUNT(xx_psw_teb32));
    if (!structure) return;
    if (!profile && xx_psw_profile_machine(target, width) &&
        !xx_psw_core_fields(context, device, structure, width == 8 ? xx_psw_teb_core64 : xx_psw_teb_core32, XX_PSW_COUNT(xx_psw_teb_core32)))
        return;
    if (!profile) xx_psw_warn_once(context, "No exact private TEB build/architecture profile matched; SDK fallback fields are shown.");
    tib = xx_psw_memory(context, device, width == 8 ? "NT_TIB64" : "NT_TIB32", "Published NT_TIB prefix at the TEB address", address, width == 8 ? 56 : 28, thread,
                        width == 8 ? xx_psw_tib64 : xx_psw_tib32, width == 8 ? XX_PSW_COUNT(xx_psw_tib64) : XX_PSW_COUNT(xx_psw_tib32));
    if (tib && tib->field_count > 4 && tib->fields[4].available)
        (void)snprintf(tib->fields[4].comment, sizeof(tib->fields[4].comment), "FiberData storage; no version-dependent fiber data is followed.");
    if (context->status == XX_PROCESS_STRUCTS_OK && address <= UINT64_MAX - (width == 8 ? 5248u : 3600u))
        (void)xx_psw_memory(context, device, width == 8 ? "TLS_SLOTS64" : "TLS_SLOTS32", "All 64 entries of the SDK TEB TlsSlots array",
                            address + (width == 8 ? 5248u : 3600u), 64 * width, thread, width == 8 ? xx_psw_tls64 : xx_psw_tls32,
                            width == 8 ? XX_PSW_COUNT(xx_psw_tls64) : XX_PSW_COUNT(xx_psw_tls32));
    if (context->status == XX_PROCESS_STRUCTS_OK) xx_pswr_teb_children(context, device, address, width, thread, target);
}

static uint64_t xx_psw_creation(HANDLE process, bool *valid, bool *exited)
{
    FILETIME creation, exit_time, kernel, user;
    DWORD exit_code = STILL_ACTIVE;
    *exited = false;
    *valid = GetProcessTimes(process, &creation, &exit_time, &kernel, &user) != 0 && GetExitCodeProcess(process, &exit_code) != 0;
    if (!*valid) return 0;
    /* ExitTime is undefined while running. Win32 reserves exit code 259 for
     * STILL_ACTIVE; an application returning 259 is ambiguous to this API. */
    *exited = exit_code != STILL_ACTIVE;
    return ((uint64_t)creation.dwHighDateTime << 32) | creation.dwLowDateTime;
}

static void xx_psw_version(HMODULE ntdll, xx_psw_target *target)
{
    OSVERSIONINFOW version;
    xx_psw_get_version_fn query = ntdll ? (xx_psw_get_version_fn)(void (*)(void))GetProcAddress(ntdll, "RtlGetVersion") : NULL;
    memset(&version, 0, sizeof(version));
    version.dwOSVersionInfoSize = (DWORD)sizeof(version);
    if (query && query(&version) >= 0) {
        target->major = version.dwMajorVersion;
        target->minor = version.dwMinorVersion;
        target->build = version.dwBuildNumber;
        target->version_available = true;
    }
}

static unsigned xx_psw_architecture(HANDLE process, char *name, size_t capacity, bool *wow64, xx_psw_target *target)
{
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    xx_psw_wow64_process2_fn query = kernel ? (xx_psw_wow64_process2_fn)(void (*)(void))GetProcAddress(kernel, "IsWow64Process2") : NULL;
    USHORT machine = 0, native_machine = 0;
    unsigned width = 0;
    const char *label = "unknown";
    *wow64 = false;
    if (query) {
        if (!query(process, &machine, &native_machine)) return 0;
        *wow64 = machine != 0;
        if (!machine) machine = native_machine;
        target->process_machine = machine;
        target->native_machine = native_machine;
        switch (machine) {
            case 0x014c:
                width = 4;
                label = "x86";
                break;
            case 0x01c0:
            case 0x01c2:
            case 0x01c4:
                width = 4;
                label = "ARM32";
                break;
            case 0x8664:
                width = 8;
                label = "x86-64";
                break;
            case 0xaa64:
                width = 8;
                label = "ARM64";
                break;
            case 0x0200:
                width = 8;
                label = "IA64";
                break;
            default: break;
        }
    } else {
        BOOL is_wow64 = FALSE;
        SYSTEM_INFO system;
        if (!IsWow64Process(process, &is_wow64)) return 0;
        GetNativeSystemInfo(&system);
        *wow64 = is_wow64 != FALSE;
        switch (system.wProcessorArchitecture) {
            case PROCESSOR_ARCHITECTURE_INTEL: target->native_machine = 0x014c; break;
            case PROCESSOR_ARCHITECTURE_AMD64: target->native_machine = 0x8664; break;
            case PROCESSOR_ARCHITECTURE_ARM: target->native_machine = 0x01c4; break;
            case PROCESSOR_ARCHITECTURE_IA64: target->native_machine = 0x0200; break;
            case 12: target->native_machine = 0xaa64; break;
            default: break;
        }
        target->process_machine = !is_wow64 ? target->native_machine : target->native_machine == 0x8664 ? 0x014c : 0;
        if (target->native_machine == 0xaa64) {
            (void)snprintf(name, capacity, "unknown (ARM64 emulation)");
            return 0;
        }
        if (is_wow64 || system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) {
            width = 4;
            label = "x86";
        } else if (system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
            width = 8;
            label = "x86-64";
        } else if (system.wProcessorArchitecture == 12) {
            width = 8;
            label = "ARM64";
        } else if (system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM) {
            width = 4;
            label = "ARM32";
        } else if (system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64) {
            width = 8;
            label = "IA64";
        }
    }
    (void)snprintf(name, capacity, "%s%s", label, *wow64 ? " (WOW64)" : "");
    return width;
}

static bool xx_psw_query_field(xx_ps_context *context, xx_process_structure *structure, const wchar_t *name, const wchar_t *type, size_t offset, size_t size,
                               xx_data_struct_record_property_t property, uint64_t value, bool available)
{
    return xx_ps_uint(context, structure, name, type, offset, size, property, value, available);
}

static bool xx_psw_basic(xx_ps_context *context, const xx_psw_basic_information *basic, bool available)
{
    unsigned width = (unsigned)sizeof(void *);
    xx_process_structure *structure = xx_ps_add(context, "PROCESS_BASIC_INFORMATION",
                                                width == 8 ? "native query output (64-bit ABI)" : "native query output (32-bit ABI)", 0, false, sizeof(*basic), 0);
    if (!structure) return false;
    return xx_psw_query_field(context, structure, L"ExitStatus", L"NTSTATUS", 0, 4, 0, (uint32_t)basic->exit_status, available) &&
           xx_psw_query_field(context, structure, L"PebBaseAddress", L"PPEB", width, width, XX_PSW_PTR, (uint64_t)(uintptr_t)basic->peb, available) &&
           xx_psw_query_field(context, structure, L"AffinityMask", L"ULONG_PTR", 2 * width, width, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS, (uint64_t)basic->affinity,
                              available) &&
           xx_psw_query_field(context, structure, L"BasePriority", L"KPRIORITY", 3 * width, 4, 0, (uint32_t)basic->priority, available) &&
           xx_psw_query_field(context, structure, L"UniqueProcessId", L"ULONG_PTR", 4 * width, width, XX_PSW_ID, (uint64_t)basic->pid, available) &&
           xx_psw_query_field(context, structure, L"InheritedFromUniqueProcessId", L"ULONG_PTR", 5 * width, width, XX_PSW_ID, (uint64_t)basic->parent_pid, available);
}

static bool xx_psw_thread_basic(xx_ps_context *context, DWORD thread, const xx_psw_thread_information *basic, bool available)
{
    unsigned width = (unsigned)sizeof(void *);
    xx_process_structure *structure = xx_ps_add(context, "THREAD_BASIC_INFORMATION", width == 8 ? "native query output (64-bit ABI)" : "native query output (32-bit ABI)",
                                                0, false, sizeof(*basic), thread);
    if (!structure) return false;
    if (!(xx_psw_query_field(context, structure, L"ExitStatus", L"NTSTATUS", 0, 4, 0, (uint32_t)basic->exit_status, available) &&
          xx_psw_query_field(context, structure, L"TebBaseAddress", L"PTEB", width, width, XX_PSW_PTR, (uint64_t)(uintptr_t)basic->teb, available) &&
          xx_psw_query_field(context, structure, L"ClientId.UniqueProcess", L"HANDLE", 2 * width, width, XX_PSW_ID, (uint64_t)(uintptr_t)basic->client.process,
                             available) &&
          xx_psw_query_field(context, structure, L"ClientId.UniqueThread", L"HANDLE", 3 * width, width, XX_PSW_ID, (uint64_t)(uintptr_t)basic->client.thread,
                             available) &&
          xx_psw_query_field(context, structure, L"AffinityMask", L"ULONG_PTR", 4 * width, width, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS, (uint64_t)basic->affinity,
                             available) &&
          xx_psw_query_field(context, structure, L"Priority", L"KPRIORITY", 5 * width, 4, 0, (uint32_t)basic->priority, available) &&
          xx_psw_query_field(context, structure, L"BasePriority", L"KPRIORITY", 5 * width + 4, 4, 0, (uint32_t)basic->base_priority, available)))
        return false;
    structure = xx_ps_add(context, "CLIENT_ID",
                          width == 8 ? "Native THREAD_BASIC_INFORMATION ClientId query (64-bit ABI)" : "Native THREAD_BASIC_INFORMATION ClientId query (32-bit ABI)", 0,
                          false, 2 * width, thread);
    if (!structure) return false;
    return xx_psw_query_field(context, structure, L"UniqueProcess", L"HANDLE", 0, width, XX_PSW_ID, (uint64_t)(uintptr_t)basic->client.process, available) &&
           xx_psw_query_field(context, structure, L"UniqueThread", L"HANDLE", width, width, XX_PSW_ID, (uint64_t)(uintptr_t)basic->client.thread, available);
}

static bool xx_psw_io_counters(xx_ps_context *context, HANDLE process)
{
    IO_COUNTERS counters;
    bool available;
    xx_process_structure *structure = xx_ps_add(context, "IO_COUNTERS", "GetProcessIoCounters query output (six fixed 64-bit counters)", 0, false, sizeof(counters), 0);
    if (!structure) return false;
    memset(&counters, 0, sizeof(counters));
    available = GetProcessIoCounters(process, &counters) != 0;
    if (!available) xx_psw_warn_once(context, "Process I/O counters were unavailable.");
    return xx_ps_decode(context, structure, xx_psw_io_fields, XX_PSW_COUNT(xx_psw_io_fields), available ? &counters : NULL, available ? sizeof(counters) : 0, false);
}

/* Prefer the Kernel32 export. Vista can use PSAPI loaded from an absolute
 * system-directory path, without adding Psapi.lib or searching the current
 * directory. The caller releases only the module acquired here. */
static xx_psw_memory_info_fn xx_psw_memory_info_api(HMODULE *owned_module)
{
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    xx_psw_memory_info_fn query = kernel ? (xx_psw_memory_info_fn)(void (*)(void))GetProcAddress(kernel, "K32GetProcessMemoryInfo") : NULL;
    WCHAR path[MAX_PATH + 16];
    static const WCHAR suffix[] = L"\\psapi.dll";
    UINT length;
    *owned_module = NULL;
    if (query) return query;
    length = GetSystemDirectoryW(path, (UINT)XX_PSW_COUNT(path));
    if (!length || length > XX_PSW_COUNT(path) - XX_PSW_COUNT(suffix)) return NULL;
    memcpy(path + length, suffix, sizeof(suffix));
    *owned_module = LoadLibraryW(path);
    if (!*owned_module) return NULL;
    return (xx_psw_memory_info_fn)(void (*)(void))GetProcAddress(*owned_module, "GetProcessMemoryInfo");
}

static bool xx_psw_memory_counters(xx_ps_context *context, HANDLE process)
{
    PROCESS_MEMORY_COUNTERS_EX counters;
    HMODULE owned_module;
    xx_psw_memory_info_fn query;
    size_t available_length = 0;
    xx_process_structure *structure =
        xx_ps_add(context, "PROCESS_MEMORY_COUNTERS_EX",
                  sizeof(SIZE_T) == 8 ? "GetProcessMemoryInfo native query output (64-bit SIZE_T)" : "GetProcessMemoryInfo native query output (32-bit SIZE_T)", 0, false,
                  sizeof(counters), 0);
    if (!structure) return false;
    memset(&counters, 0, sizeof(counters));
    counters.cb = (DWORD)sizeof(counters);
    query = xx_psw_memory_info_api(&owned_module);
    if (query) {
        if (query(process, (PPROCESS_MEMORY_COUNTERS)&counters, (DWORD)sizeof(counters))) available_length = sizeof(counters);
        else {
            /* Older implementations may only supply the documented base
             * prefix. Do not fabricate an extended PrivateUsage value. */
            memset(&counters, 0, sizeof(counters));
            counters.cb = (DWORD)sizeof(PROCESS_MEMORY_COUNTERS);
            if (query(process, (PPROCESS_MEMORY_COUNTERS)&counters, (DWORD)sizeof(PROCESS_MEMORY_COUNTERS))) available_length = sizeof(PROCESS_MEMORY_COUNTERS);
        }
    }
    if (owned_module) (void)FreeLibrary(owned_module);
    if (!xx_ps_decode(context, structure, xx_psw_memory_fields, XX_PSW_COUNT(xx_psw_memory_fields), available_length ? &counters : NULL, available_length, false))
        return false;
    if (!available_length) xx_psw_warn_once(context, "Process memory counters were unavailable.");
    else if (available_length < sizeof(counters)) {
        xx_psw_warn_once(context, "Only the base process memory counter prefix was available.");
        if (structure->field_count > 10)
            (void)snprintf(structure->fields[10].comment, sizeof(structure->fields[10].comment), "The base query did not supply PrivateUsage; this value is unknown.");
    }
    return true;
}

static bool xx_psw_filetime(xx_ps_context *context, const char *name, const FILETIME *time, bool available, bool duration, const char *unavailable_reason)
{
    uint64_t ticks;
    xx_process_structure *structure =
        xx_ps_add(context, name, duration ? "GetProcessTimes FILETIME output (duration in 100-ns units)" : "GetProcessTimes FILETIME output (UTC epoch 1601-01-01)", 0,
                  false, sizeof(*time), 0);
    if (!structure) return false;
    if (!xx_ps_decode(context, structure, xx_psw_filetime_fields, XX_PSW_COUNT(xx_psw_filetime_fields), available ? time : NULL, available ? sizeof(*time) : 0, false))
        return false;
    if (!available) {
        if (unavailable_reason)
            for (size_t i = 0; i < structure->field_count; ++i)
                (void)snprintf(structure->fields[i].comment, sizeof(structure->fields[i].comment), "%s", unavailable_reason);
        return true;
    }
    ticks = ((uint64_t)time->dwHighDateTime << 32) | time->dwLowDateTime;
    if (duration) {
        (void)snprintf(structure->fields[0].comment, sizeof(structure->fields[0].comment), "Combined value: %llu x 100 ns (%llu.%07llu s).", (unsigned long long)ticks,
                       (unsigned long long)(ticks / 10000000u), (unsigned long long)(ticks % 10000000u));
    } else {
        SYSTEMTIME utc;
        if (FileTimeToSystemTime(time, &utc))
            (void)snprintf(structure->fields[0].comment, sizeof(structure->fields[0].comment),
                           "Combined value: %llu x 100 ns since 1601-01-01 UTC; %04u-%02u-%02u %02u:%02u:%02u.%03u UTC.", (unsigned long long)ticks, (unsigned)utc.wYear,
                           (unsigned)utc.wMonth, (unsigned)utc.wDay, (unsigned)utc.wHour, (unsigned)utc.wMinute, (unsigned)utc.wSecond, (unsigned)utc.wMilliseconds);
        else
            (void)snprintf(structure->fields[0].comment, sizeof(structure->fields[0].comment), "Combined value: %llu x 100 ns since 1601-01-01 UTC.",
                           (unsigned long long)ticks);
    }
    return true;
}

static bool xx_psw_times(xx_ps_context *context, HANDLE process)
{
    FILETIME creation, exit_time, kernel, user;
    bool available;
    memset(&creation, 0, sizeof(creation));
    memset(&exit_time, 0, sizeof(exit_time));
    memset(&kernel, 0, sizeof(kernel));
    memset(&user, 0, sizeof(user));
    available = GetProcessTimes(process, &creation, &exit_time, &kernel, &user) != 0;
    if (!available) xx_psw_warn_once(context, "Process timing records were unavailable.");
    return xx_psw_filetime(context, "FILETIME (process creation)", &creation, available, false, NULL) &&
           /* This collector rejects exited targets during the final identity
            * check. GetProcessTimes explicitly leaves live ExitTime undefined. */
           xx_psw_filetime(context, "FILETIME (process exit)", &exit_time, false, false, "GetProcessTimes exit output is undefined while the process is running.") &&
           xx_psw_filetime(context, "FILETIME (process kernel duration)", &kernel, available, true, NULL) &&
           xx_psw_filetime(context, "FILETIME (process user duration)", &user, available, true, NULL);
}

/* Public query output uses the inspector ABI, never the target's private heap
 * layout. Common members avoid SDK-specific PartitionId/alignment variants.
 * https://learn.microsoft.com/windows/win32/api/memoryapi/nf-memoryapi-virtualqueryex
 * https://learn.microsoft.com/windows/win32/api/winnt/ns-winnt-memory_basic_information
 */
static void xx_psw_region(xx_ps_context *context, HANDLE process, const char *label, uint64_t address)
{
    MEMORY_BASIC_INFORMATION information;
    xx_process_structure *structure;
    SIZE_T returned;
    bool available, allocated, committed;
    char name[96], layout[96];
    if (!address || address > (uint64_t)UINTPTR_MAX || context->status != XX_PROCESS_STRUCTS_OK) return;
    (void)snprintf(name, sizeof(name), "MEMORY_BASIC_INFORMATION (%s)", label);
    (void)snprintf(layout, sizeof(layout), "VirtualQueryEx common query fields (%u-bit caller ABI)", (unsigned)sizeof(void *) * 8);
    structure = xx_ps_add(context, name, layout, 0, false, sizeof(information), 0);
    if (!structure) return;
    memset(&information, 0, sizeof(information));
    returned = VirtualQueryEx(process, (LPCVOID)(uintptr_t)address, &information, sizeof(information));
    available = returned >= offsetof(MEMORY_BASIC_INFORMATION, Type) + sizeof(information.Type);
    allocated = available && information.State != MEM_FREE;
    committed = available && information.State == MEM_COMMIT;
#define XX_PSW_REGION_NAME_INNER(name) L##name
#define XX_PSW_REGION_NAME(name) XX_PSW_REGION_NAME_INNER(name)
#define XX_PSW_REGION_FIELD(member, type, property, valid)                                                                                                      \
    if (!xx_ps_uint(context, structure, XX_PSW_REGION_NAME(#member), L##type, offsetof(MEMORY_BASIC_INFORMATION, member), sizeof(information.member), property, \
                    (uint64_t)(uintptr_t)information.member, valid))                                                                                            \
    return
    XX_PSW_REGION_FIELD(BaseAddress, "PVOID", XX_PSW_PTR, available);
    XX_PSW_REGION_FIELD(AllocationBase, "PVOID", XX_PSW_PTR, allocated);
    XX_PSW_REGION_FIELD(AllocationProtect, "DWORD", XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS, allocated);
    XX_PSW_REGION_FIELD(RegionSize, "SIZE_T", XX_PSW_SIZE, available);
    XX_PSW_REGION_FIELD(State, "DWORD", XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS, available);
    XX_PSW_REGION_FIELD(Protect, "DWORD", XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS, committed);
    XX_PSW_REGION_FIELD(Type, "DWORD", XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS, allocated);
#undef XX_PSW_REGION_FIELD
#undef XX_PSW_REGION_NAME
#undef XX_PSW_REGION_NAME_INNER
    if (available)
        (void)snprintf(structure->fields[0].comment, sizeof(structure->fields[0].comment),
                       "Query address 0x%llX; this record is API output, not bytes at the target address.", (unsigned long long)address);
    else xx_psw_warn_once(context, "Some VirtualQueryEx memory-region records were unavailable.");
    if (available && (!allocated || !committed)) {
        for (size_t i = 0; i < structure->field_count; ++i)
            if (!structure->fields[i].available)
                (void)snprintf(structure->fields[i].comment, sizeof(structure->fields[i].comment),
                               "VirtualQueryEx leaves this member undefined for the reported memory state.");
    }
}

static void xx_psw_peb_regions(xx_ps_context *context, HANDLE process, xx_io_device *device, uint64_t address, unsigned width, const xx_psw_target *target)
{
    uint64_t heap = 0, image = 0;
    if (!address) return;
    xx_psw_region(context, process, width == 8 ? "PEB64" : "PEB32", address);
    if (!xx_psw_profile_machine(target, width) || context->status != XX_PROCESS_STRUCTS_OK) return;
    if (xx_psw_pointer(device, address, width == 8 ? 48 : 24, width, &heap) && heap)
        xx_psw_region(context, process, width == 8 ? "ProcessHeap64" : "ProcessHeap32", heap);
    if (context->status == XX_PROCESS_STRUCTS_OK && xx_psw_pointer(device, address, width == 8 ? 16 : 8, width, &image) && image)
        xx_psw_region(context, process, width == 8 ? "ImageBase64" : "ImageBase32", image);
}

/* Convert the bounded UTF-16LE preview without depending on host wchar_t width.
 * Preserve pointer values in the field value; the preview is a field comment. */
static void xx_psw_utf8_preview(const unsigned char *bytes, size_t length, char *text, size_t capacity)
{
    size_t i, used = 0;
    if (!capacity) return;
    for (i = 0; i + 1 < length; i += 2) {
        uint32_t ch = (uint32_t)xx_psw_le(bytes + i, 2);
        unsigned char encoded[4];
        size_t count;
        if (ch >= 0xd800 && ch <= 0xdbff && i + 3 < length) {
            uint32_t low = (uint32_t)xx_psw_le(bytes + i + 2, 2);
            if (low >= 0xdc00 && low <= 0xdfff) {
                ch = 0x10000 + ((ch - 0xd800) << 10) + (low - 0xdc00);
                i += 2;
            } else ch = 0xfffd;
        } else if (ch >= 0xd800 && ch <= 0xdfff) ch = 0xfffd;
        if (!ch) ch = 0x2400; /* an embedded NUL must not hide subsequent bytes */
        if (ch < 0x20 || ch == 0x7f) ch = ' ';
        if (ch < 0x80) {
            encoded[0] = (unsigned char)ch;
            count = 1;
        } else if (ch < 0x800) {
            encoded[0] = (unsigned char)(0xc0 | (ch >> 6));
            encoded[1] = (unsigned char)(0x80 | (ch & 63));
            count = 2;
        } else if (ch < 0x10000) {
            encoded[0] = (unsigned char)(0xe0 | (ch >> 12));
            encoded[1] = (unsigned char)(0x80 | ((ch >> 6) & 63));
            encoded[2] = (unsigned char)(0x80 | (ch & 63));
            count = 3;
        } else {
            encoded[0] = (unsigned char)(0xf0 | (ch >> 18));
            encoded[1] = (unsigned char)(0x80 | ((ch >> 12) & 63));
            encoded[2] = (unsigned char)(0x80 | ((ch >> 6) & 63));
            encoded[3] = (unsigned char)(0x80 | (ch & 63));
            count = 4;
        }
        if (count >= capacity - used) break;
        memcpy(text + used, encoded, count);
        used += count;
    }
    text[used] = 0;
}

static void xx_psw_unicode(xx_ps_context *context, xx_io_device *device, const char *label, uint64_t address, unsigned width)
{
    const xx_data_struct_field_desc *fields = width == 8 ? xx_psw_unicode64 : xx_psw_unicode32;
    xx_process_structure *structure;
    unsigned char header[16], text[160];
    uint64_t buffer;
    size_t length, maximum, preview;
    char name[96], converted[172];
    (void)snprintf(name, sizeof(name), "UNICODE_STRING%u (%s)", width * 8, label);
    structure = xx_psw_memory(context, device, name, "Microsoft documented UNICODE_STRING", address, width == 8 ? 16 : 8, 0, fields, 3);
    if (!structure || structure->field_count < 3) return;
    if (!xx_psw_read_at(device, address, 0, header, width == 8 ? 16 : 8)) return;
    length = (size_t)xx_psw_le(header, 2);
    maximum = (size_t)xx_psw_le(header + 2, 2);
    buffer = xx_psw_le(header + (width == 8 ? 8 : 4), width);
    if ((length & 1) || length > maximum || (length && !buffer)) {
        (void)snprintf(structure->fields[2].comment, sizeof(structure->fields[2].comment), "Invalid UNICODE_STRING length/buffer; text was not read.");
        xx_ps_warn(context, "An inconsistent UNICODE_STRING was not followed.");
        return;
    }
    preview = length < sizeof(text) ? length : sizeof(text);
    if (preview && !xx_ps_read(device, buffer, text, preview)) {
        (void)snprintf(structure->fields[2].comment, sizeof(structure->fields[2].comment), "String buffer is unreadable.");
        xx_ps_warn(context, "A Unicode string buffer was unreadable.");
        return;
    }
    xx_psw_utf8_preview(text, preview, converted, sizeof(converted));
    (void)snprintf(structure->fields[2].comment, sizeof(structure->fields[2].comment), "UTF-16LE preview: %s", converted);
}

/* Versioned structure families reuse the checked read/decode helpers above. */
#include "xx_process_params_layouts.h"
#include "xx_process_loader_layouts.h"
#include "xx_process_related_layouts.h"
#include "xx_process_heap_layouts.h"
#include "xx_process_segment_heap_layouts.h"
#include "xx_process_wow64_heap_identity.h"
#include "xx_process_wow64_heap_layouts.h"
#include "xx_process_wow64_segment_heap_layouts.h"
#include "xx_process_heap_legacy_children.h"
#include "xx_process_heap_context_children.h"
#include "xx_process_heap_suballocator_layouts.h"
#include "xx_process_wow64_heap_suballocator_layouts.h"
#include "xx_process_heap_suballocator_discovery.h"
#include "xx_process_heap_vs_chunk_layouts.h"
#include "xx_process_wow64_heap_vs_chunk_layouts.h"
#include "xx_process_heap_vs_chunk_discovery.h"
#include "xx_process_heap_discovery.h"

static void xx_psw_loader(xx_ps_context *context, xx_io_device *device, uint64_t address, unsigned width, size_t *module_total, const xx_psw_target *target)
{
    const xx_psw_nt_layout *profile = xx_pswl_profile(target, width, false);
    const xx_psw_nt_layout *entry_profile = xx_pswl_profile(target, width, true);
    const xx_data_struct_field_desc *fields = width == 8 ? xx_psw_ldr64 : xx_psw_ldr32;
    uint64_t head, link, previous, next, backward, entry, checked_address;
    uint64_t *visited = NULL;
    size_t count = 0, capacity = 0, i;
    size_t loader_size = profile ? profile->size : width == 8 ? 48 : 28;
    size_t entry_size = entry_profile ? entry_profile->size : width == 8 ? 136 : 72;
    size_t link_offset = width == 8 ? 16 : 8, name_offset = width == 8 ? 72 : 36;
    if (!xx_pswl_address(address, 0, loader_size, width, &checked_address)) {
        xx_psw_warn_once(context, "An invalid loader structure address was not followed.");
        return;
    }
    if (!xx_psw_memory(context, device, width == 8 ? "PEB_LDR_DATA64" : "PEB_LDR_DATA32", profile ? profile->label : "Complete published SDK PEB_LDR_DATA", address,
                       loader_size, 0, profile ? profile->fields : fields, profile ? profile->count : 4))
        return;
    if (!profile || !entry_profile) xx_psw_warn_once(context, "No exact private loader build/architecture profile matched; SDK loader fields are shown.");
    if (address > UINT64_MAX - (width == 8 ? 32u : 20u)) return;
    head = address + (width == 8 ? 32u : 20u);
    xx_psw_list(context, device, "InMemoryOrderModuleList", head, width);
    if (profile && context->status == XX_PROCESS_STRUCTS_OK && address <= UINT64_MAX - (width == 8 ? 16u : 12u))
        xx_psw_list(context, device, "InLoadOrderModuleList", address + (width == 8 ? 16u : 12u), width);
    if (profile && context->status == XX_PROCESS_STRUCTS_OK && address <= UINT64_MAX - (width == 8 ? 48u : 28u))
        xx_psw_list(context, device, "InInitializationOrderModuleList", address + (width == 8 ? 48u : 28u), width);
    if (context->status != XX_PROCESS_STRUCTS_OK) return;
    if (!xx_psw_pointer(device, head, 0, width, &link)) return;
    previous = head;
    while (link != head && link && context->status == XX_PROCESS_STRUCTS_OK) {
        for (i = 0; i < count; ++i)
            if (visited[i] == link) break;
        if (i != count || link < link_offset || (link & (width - 1u))) {
            xx_ps_warn(context, "The loader list changed or contained an invalid link.");
            break;
        }
        if (*module_total >= context->limits.max_modules) {
            context->snapshot->truncated = true;
            xx_ps_warn(context, "The module structure limit was reached.");
            break;
        }
        if (xx_psw_full(context)) {
            context->snapshot->truncated = true;
            xx_psw_warn_once(context, "The structure limit was reached.");
            break;
        }
        if (count == capacity) {
            const size_t maximum = SIZE_MAX / sizeof(*visited);
            size_t grown = capacity ? (capacity > maximum / 2 ? maximum : capacity * 2) : 32;
            uint64_t *items;
            if (grown > context->limits.max_modules) grown = context->limits.max_modules;
            if (grown > maximum) grown = maximum;
            if (grown <= capacity) {
                context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
                break;
            }
            items = (uint64_t *)realloc(visited, grown * sizeof(*visited));
            if (!items) {
                context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
                break;
            }
            visited = items;
            capacity = grown;
        }
        visited[count++] = link;
        if (!xx_psw_pointer(device, link, 0, width, &next) || !xx_psw_pointer(device, link, width, width, &backward)) {
            xx_ps_warn(context, "The loader list contained an unreadable entry.");
            break;
        }
        if (backward != previous) {
            xx_ps_warn(context, "The loader list changed during inspection.");
            break;
        }
        entry = link - link_offset;
        if (!xx_pswl_address(entry, 0, entry_size, width, &checked_address)) {
            xx_psw_warn_once(context, "An invalid loader entry address was not followed.");
            break;
        }
        xx_process_structure *structure = xx_psw_memory(context, device, width == 8 ? "LDR_DATA_TABLE_ENTRY64" : "LDR_DATA_TABLE_ENTRY32",
                                                        entry_profile ? entry_profile->label : "Complete published SDK LDR_DATA_TABLE_ENTRY", entry, entry_size, 0,
                                                        entry_profile ? entry_profile->fields
                                                        : width == 8  ? xx_psw_entry64
                                                                      : xx_psw_entry32,
                                                        entry_profile ? entry_profile->count
                                                        : width == 8  ? XX_PSW_COUNT(xx_psw_entry64)
                                                                      : XX_PSW_COUNT(xx_psw_entry32));
        if (!structure) break;
        ++*module_total;
        for (i = 0; !entry_profile && i < structure->field_count; ++i)
            if (structure->fields[i].available && (!wcscmp(structure->fields[i].desc.name, L"CheckSum") || !wcscmp(structure->fields[i].desc.name, L"Reserved6")))
                (void)snprintf(structure->fields[i].comment, sizeof(structure->fields[i].comment),
                               "CheckSum and Reserved6 share union storage; the active member is not inferred.");
        if (context->status == XX_PROCESS_STRUCTS_OK) xx_psw_list(context, device, "InMemoryOrderLinks", link, width);
        if (entry <= UINT64_MAX - name_offset && context->status == XX_PROCESS_STRUCTS_OK) xx_psw_unicode(context, device, "FullDllName", entry + name_offset, width);
        if (entry_profile && context->status == XX_PROCESS_STRUCTS_OK) xx_psw_unicode(context, device, "BaseDllName", entry + (width == 8 ? 0x58u : 0x2cu), width);
        if (context->status == XX_PROCESS_STRUCTS_OK) xx_pswl_children(context, device, entry, width, target);
        previous = link;
        link = next;
    }
    if (!link) xx_ps_warn(context, "The loader list ended at a null link.");
    free(visited);
}

static void xx_psw_peb(xx_ps_context *context, xx_io_device *device, uint64_t address, unsigned width, size_t *module_total, const xx_psw_target *target)
{
    const xx_psw_nt_layout *profile = xx_psw_profile(target, width, false);
    xx_process_structure *structure;
    char fallback[96];
    uint64_t loader = 0, parameters = 0;
    if (!address) {
        xx_ps_warn(context, "The process PEB address was unavailable.");
        return;
    }
    xx_psw_fallback_label(target, width, fallback, sizeof(fallback));
    structure = xx_psw_memory(context, device, width == 8 ? "PEB64" : "PEB32", profile ? profile->label : fallback, address,
                              profile      ? profile->size
                              : width == 8 ? 712
                                           : 472,
                              0,
                              profile      ? profile->fields
                              : width == 8 ? xx_psw_peb64
                                           : xx_psw_peb32,
                              profile      ? profile->count
                              : width == 8 ? XX_PSW_COUNT(xx_psw_peb64)
                                           : XX_PSW_COUNT(xx_psw_peb32));
    if (!structure) return;
    if (!profile && xx_psw_profile_machine(target, width) &&
        !xx_psw_core_fields(context, device, structure, width == 8 ? xx_psw_peb_core64 : xx_psw_peb_core32, XX_PSW_COUNT(xx_psw_peb_core32)))
        return;
    if (!profile) xx_psw_warn_once(context, "No exact private PEB build/architecture profile matched; SDK fallback fields are shown.");
    (void)xx_psw_pointer(device, address, width == 8 ? 24 : 12, width, &loader);
    (void)xx_psw_pointer(device, address, width == 8 ? 32 : 16, width, &parameters);
    if (context->status == XX_PROCESS_STRUCTS_OK) xx_pswh_collect(context, device, address, width, target);
    if (parameters && context->status == XX_PROCESS_STRUCTS_OK) xx_pswp_parameters(context, device, parameters, width, target);
    if (context->status == XX_PROCESS_STRUCTS_OK) xx_pswr_peb_children(context, device, address, width, target);
    if (loader && context->status == XX_PROCESS_STRUCTS_OK) xx_psw_loader(context, device, loader, width, module_total, target);
}

/* Query-derived native TEB addresses have an ABI; low numerical addresses do
 * not identify a 32-bit layout. Independently read the public NT_TIB.Self and
 * TEB.ProcessEnvironmentBlock links before accepting a WOW64 candidate. */
static int xx_psw_teb_identity(xx_io_device *device, uint64_t address, unsigned width, uint64_t expected_peb)
{
    uint64_t self = 0, peb = 0;
    bool have_self = xx_psw_pointer(device, address, 6 * width, width, &self);
    bool have_peb = xx_psw_pointer(device, address, 12 * width, width, &peb);
    if ((have_self && self != address) || (have_peb && expected_peb && peb != expected_peb)) return -1;
    return have_self && have_peb ? 1 : 0;
}

static bool xx_psw_wow64_candidate(xx_io_device *device, uint64_t candidate, uint64_t peb32)
{
    return candidate && candidate <= UINT32_MAX && (candidate & 3u) == 0 && peb32 && xx_psw_teb_identity(device, candidate, 4, peb32) == 1;
}

static bool xx_psw_wow64_teb(xx_io_device *device, uint64_t teb64, uint64_t peb32, const xx_psw_target *target, uint64_t *teb32)
{
    const xx_psw_nt_layout *profile = xx_psw_profile(target, 8, true);
    uint64_t candidate = 0;
    if (profile)
        for (size_t i = 0; i < profile->count; ++i) {
            const xx_data_struct_field_desc *field = profile->fields + i;
            unsigned char bytes[4];
            uint64_t value;
            int64_t delta;
            if (!field->name || wcscmp(field->name, L"WowTebOffset")) continue;
            if (field->size != 4 || field->rel_offset < 0 || profile->size < 4 || (uint64_t)field->rel_offset > profile->size - 4 ||
                !xx_psw_read_at(device, teb64, (uint64_t)field->rel_offset, bytes, sizeof(bytes)))
                break;
            value = xx_psw_le(bytes, sizeof(bytes));
            delta = value <= INT32_MAX ? (int64_t)value : (int64_t)value - INT64_C(0x100000000);
            if (delta > 0 && (uint64_t)delta <= UINT64_MAX - teb64) candidate = teb64 + (uint64_t)delta;
            else if (delta < 0 && (uint64_t)(-delta) <= teb64) candidate = teb64 - (uint64_t)(-delta);
            if (xx_psw_wow64_candidate(device, candidate, peb32)) {
                *teb32 = candidate;
                return true;
            }
            break;
        }
    /* The pointer at TEB64+0 is documented for Vista..8.1 and remains only a
     * candidate on other versions. Never assume an allocation delta. */
    if (xx_psw_pointer(device, teb64, 0, 8, &candidate) && xx_psw_wow64_candidate(device, candidate, peb32)) {
        *teb32 = candidate;
        return true;
    }
    return false;
}

static void xx_psw_threads(xx_ps_context *context, xx_io_device *device, DWORD pid, unsigned target_width, bool wow64, uint64_t native_peb, uint64_t wow64_peb32,
                           xx_psw_query_thread_fn query, const xx_psw_target *target)
{
    HANDLE snapshot;
    THREADENTRY32 entry;
    size_t count = 0;
    BOOL more;
    bool wow_teb_unavailable = false;
    if (!query) {
        xx_ps_warn(context, "NtQueryInformationThread was unavailable.");
        return;
    }
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        xx_ps_warn(context, "The thread snapshot was unavailable.");
        return;
    }
    memset(&entry, 0, sizeof(entry));
    entry.dwSize = (DWORD)sizeof(entry);
    more = Thread32First(snapshot, &entry);
    while (more && context->status == XX_PROCESS_STRUCTS_OK) {
        if (entry.th32OwnerProcessID == pid) {
            HANDLE thread;
            xx_psw_thread_information basic;
            bool available = false;
            ULONG returned = 0;
            uint64_t teb;
            if (count >= context->limits.max_threads) {
                context->snapshot->truncated = true;
                xx_ps_warn(context, "The thread structure limit was reached.");
                break;
            }
            if (xx_psw_full(context)) {
                context->snapshot->truncated = true;
                xx_psw_warn_once(context, "The structure limit was reached.");
                break;
            }
            ++count;
            memset(&basic, 0, sizeof(basic));
            thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
            if (!thread) thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
            if (thread) {
                available = GetProcessIdOfThread(thread) == pid && query(thread, 0, &basic, (ULONG)sizeof(basic), &returned) >= 0 && returned >= sizeof(basic) &&
                            (uint64_t)(uintptr_t)basic.client.process == pid && (uint64_t)(uintptr_t)basic.client.thread == entry.th32ThreadID;
            }
            if (!xx_psw_thread_basic(context, entry.th32ThreadID, &basic, available)) {
                if (thread) (void)CloseHandle(thread);
                break;
            }
            teb = (uint64_t)(uintptr_t)basic.teb;
            if (available && teb && target_width && !(sizeof(void *) == 4 && target_width == 8) && context->status == XX_PROCESS_STRUCTS_OK) {
                unsigned native_width = (unsigned)sizeof(void *);
                int native_identity = xx_psw_teb_identity(device, teb, native_width, native_peb);
                bool have_wow64 = wow64 && native_width == 8 && target_width == 4;
                if (native_identity < 0) {
                    if (have_wow64 && wow64_peb32 && xx_psw_teb_identity(device, teb, 4, wow64_peb32) == 1) {
                        xx_psw_teb(context, device, "TEB32 (WOW64)", teb, 4, entry.th32ThreadID, target);
                        xx_psw_warn_once(context, "A thread query exposed a validated TEB32; its native TEB64 address was unavailable.");
                    } else xx_psw_warn_once(context, "A thread TEB did not match the expected Self/PEB links; its layout was not inferred.");
                } else xx_psw_teb(context, device, native_width == 8 ? "TEB64" : "TEB32", teb, native_width, entry.th32ThreadID, target);
                if (native_identity >= 0 && device && have_wow64 && context->status == XX_PROCESS_STRUCTS_OK) {
                    uint64_t teb32 = 0;
                    if (wow64_peb32 && xx_psw_wow64_teb(device, teb, wow64_peb32, target, &teb32)) {
                        xx_psw_teb(context, device, "TEB32 (WOW64)", teb32, 4, entry.th32ThreadID, target);
                    } else wow_teb_unavailable = true;
                }
            } else if (!available) xx_psw_warn_once(context, "Some thread query results were unavailable or changed.");
            if (thread) (void)CloseHandle(thread);
        }
        entry.dwSize = (DWORD)sizeof(entry);
        more = Thread32Next(snapshot, &entry);
    }
    if (wow_teb_unavailable) xx_ps_warn(context, "Some WOW64 TEB32 addresses could not be validated; available native TEB64 views are shown.");
    (void)CloseHandle(snapshot);
}

xx_process_structs_status xx_ps_collect(xx_ps_context *context, uint64_t pid, uint64_t expected_start)
{
    HANDLE process;
    HMODULE ntdll;
    xx_psw_query_process_fn query_process;
    xx_psw_query_thread_fn query_thread;
    xx_psw_basic_information basic;
    xx_psw_target target = {0};
    xx_io_device *device = NULL;
    xx_process_structs_status status = XX_PROCESS_STRUCTS_OK;
    uint64_t start, after, native_peb = 0, wow64_peb32 = 0;
    size_t module_total = 0;
    bool valid = false, exited = false, memory_rights = true, wow64 = false, basic_available = false;
    unsigned width;
    ULONG returned = 0;
    WCHAR path[4096];
    DWORD path_size = (DWORD)XX_PSW_COUNT(path);
    char *base;
    if (!context || !context->snapshot || pid > MAXDWORD) return XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
    if (!pid) pid = GetCurrentProcessId();
    if (!pid) return XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
    process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, (DWORD)pid);
    if (!process) process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, (DWORD)pid);
    if (!process) {
        memory_rights = false;
        process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
        if (!process) process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    }
    if (!process)
        return GetLastError() == ERROR_NOT_ENOUGH_MEMORY || GetLastError() == ERROR_OUTOFMEMORY ? XX_PROCESS_STRUCTS_OUT_OF_MEMORY : XX_PROCESS_STRUCTS_UNAVAILABLE;
    start = xx_psw_creation(process, &valid, &exited);
    if (!valid || !start) {
        status = XX_PROCESS_STRUCTS_UNAVAILABLE;
        goto done;
    }
    if ((expected_start && start != expected_start) || exited) {
        status = XX_PROCESS_STRUCTS_CHANGED;
        goto done;
    }
    context->snapshot->pid = pid;
    context->snapshot->start_time = start;
    width = xx_psw_architecture(process, context->snapshot->architecture, sizeof(context->snapshot->architecture), &wow64, &target);
    context->snapshot->pointer_size = width;
    if (QueryFullProcessImageNameW(process, 0, path, &path_size) && path_size < XX_PSW_COUNT(path)) {
        int result = WideCharToMultiByte(CP_UTF8, 0, path, (int)path_size, context->snapshot->path, (int)sizeof(context->snapshot->path) - 1, NULL, NULL);
        if (result > 0) context->snapshot->path[result] = 0;
    }
    base = strrchr(context->snapshot->path, '\\');
    (void)snprintf(context->snapshot->name, sizeof(context->snapshot->name), "%s", base ? base + 1 : context->snapshot->path);
    if (!context->snapshot->name[0]) (void)snprintf(context->snapshot->name, sizeof(context->snapshot->name), "PID %lu", (unsigned long)pid);
    ntdll = GetModuleHandleW(L"ntdll.dll");
    xx_psw_version(ntdll, &target);
    query_process = ntdll ? (xx_psw_query_process_fn)(void (*)(void))GetProcAddress(ntdll, "NtQueryInformationProcess") : NULL;
    query_thread = ntdll ? (xx_psw_query_thread_fn)(void (*)(void))GetProcAddress(ntdll, "NtQueryInformationThread") : NULL;
    memset(&basic, 0, sizeof(basic));
    if (query_process)
        basic_available = query_process(process, 0, &basic, (ULONG)sizeof(basic), &returned) >= 0 && returned >= sizeof(basic) && (uint64_t)basic.pid == pid;
    if (!xx_psw_basic(context, &basic, basic_available)) goto collected;
    if (!basic_available) xx_ps_warn(context, "ProcessBasicInformation was unavailable.");
    if (!xx_psw_io_counters(context, process) || !xx_psw_memory_counters(context, process) || !xx_psw_times(context, process)) goto collected;
    if (!width) xx_ps_warn(context, "The target architecture could not be identified; only query metadata is shown.");
    if (sizeof(void *) == 4 && width == 8) xx_ps_warn(context, "A 32-bit inspector cannot read 64-bit target structures; use a 64-bit build.");
    if (memory_rights && width && !(sizeof(void *) == 4 && width == 8)) {
        device = xx_io_process_open_handle_ex((void *)process, XX_IO_PROCESS_READ);
        if (!device) xx_ps_warn(context, "The read-only process memory device was unavailable.");
    } else if (!memory_rights) xx_ps_warn(context, "Process memory access was denied; memory fields remain unavailable.");
    if (basic_available) native_peb = (uint64_t)(uintptr_t)basic.peb;
    if (wow64 && sizeof(void *) == 8 && width == 4) {
        ULONG_PTR wow_peb = 0;
        xx_process_structure *structure;
        bool available;
        returned = 0;
        available = query_process && query_process(process, 26, &wow_peb, (ULONG)sizeof(wow_peb), &returned) >= 0 && returned >= sizeof(wow_peb) && wow_peb &&
                    (uint64_t)wow_peb <= UINT32_MAX;
        structure = xx_ps_add(context, "ProcessWow64Information", "native query output (32-bit PEB address)", 0, false, sizeof(wow_peb), 0);
        if (!structure) goto collected;
        if (!xx_ps_uint(context, structure, L"Peb32Address", L"ULONG_PTR", 0, sizeof(wow_peb), XX_PSW_PTR, (uint64_t)wow_peb, available)) goto collected;
        wow64_peb32 = available ? (uint64_t)wow_peb : 0;
        if (!available) xx_ps_warn(context, "The WOW64 PEB32 address could not be queried.");
        else xx_psw_warn_once(context, "The WOW64 PEB32 is a best-effort native query view; thread candidates require matching Self/PEB32 links.");
    }
    if (native_peb && width && !(sizeof(void *) == 4 && width == 8) && context->status == XX_PROCESS_STRUCTS_OK) {
        xx_psw_peb(context, device, native_peb, (unsigned)sizeof(void *), &module_total, &target);
        if (context->status == XX_PROCESS_STRUCTS_OK) xx_psw_peb_regions(context, process, device, native_peb, (unsigned)sizeof(void *), &target);
    }
    if (wow64_peb32 && context->status == XX_PROCESS_STRUCTS_OK) {
        xx_psw_peb(context, device, wow64_peb32, 4, &module_total, &target);
        if (context->status == XX_PROCESS_STRUCTS_OK) xx_psw_peb_regions(context, process, device, wow64_peb32, 4, &target);
    }
    if (context->status == XX_PROCESS_STRUCTS_OK) xx_psw_threads(context, device, (DWORD)pid, width, wow64, native_peb, wow64_peb32, query_thread, &target);
collected:
    if (context->status != XX_PROCESS_STRUCTS_OK) {
        status = context->status;
        goto done;
    }
    valid = false;
    exited = false;
    after = xx_psw_creation(process, &valid, &exited);
    if (!valid) status = XX_PROCESS_STRUCTS_UNAVAILABLE;
    else if (after != start || exited) status = XX_PROCESS_STRUCTS_CHANGED;
done:
    if (device) (void)xx_io_process_close(device);
    (void)CloseHandle(process);
    return status;
}
#endif
