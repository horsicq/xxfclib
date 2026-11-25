/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_PROCESS_LOADER_LAYOUTS_H
#define XX_PROCESS_LOADER_LAYOUTS_H

/* Original target-layout metadata derived from public symbol facts and phnt
 * declarations, not host C layouts. Include after xx_psw_unicode and before
 * xx_psw_loader. These private records are enabled only for the exact builds
 * and machines below; an absent profile means the caller keeps its SDK view.
 *
 * Primary layout sources (19045 is Windows 10 22H2; 26100 is Windows 11 24H2):
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_PEB_LDR_DATA
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_PEB_LDR_DATA
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_PEB_LDR_DATA
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_LDR_DATA_TABLE_ENTRY
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_LDR_DATA_TABLE_ENTRY
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_LDR_DATA_TABLE_ENTRY
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_LDR_DDAG_NODE
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_LDR_DDAG_NODE
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_LDR_DDAG_NODE
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_RTL_BALANCED_NODE
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_RTL_BALANCED_NODE
 * https://raw.githubusercontent.com/winsiderss/phnt/master/ntldr.h
 * https://raw.githubusercontent.com/winsiderss/phnt/master/ntwow64.h
 * https://raw.githubusercontent.com/winsiderss/systeminformer/master/phnt/include/phnt_ntdef.h
 *
 * The 19045 native I386 and phnt fixed-width WOW64 declarations agree. No
 * exact 26100 WOW64 loader symbols were established, so that family keeps
 * the published SDK fallback even though full PEB/TEB profiles exist for it.
 * Service-tag records are the phnt two-field ABI, shown only as direct heads.
 * Red and Balance are overlapping union alternatives in phnt; both start at
 * bit zero of ParentValue. Their values do not identify an active union arm.
 */

#define XX_PSWL_LIST32(n, o)                                                                                  \
    XX_PSW_FIELD(n, "LIST_ENTRY32[1]", o, 8, 0), XX_PSW_FIELD(n ".Flink", "PLIST_ENTRY32", o, 4, XX_PSW_PTR), \
        XX_PSW_FIELD(n ".Blink", "PLIST_ENTRY32", (o) + 4, 4, XX_PSW_PTR)
#define XX_PSWL_LIST64(n, o)                                                                                   \
    XX_PSW_FIELD(n, "LIST_ENTRY64[1]", o, 16, 0), XX_PSW_FIELD(n ".Flink", "PLIST_ENTRY64", o, 8, XX_PSW_PTR), \
        XX_PSW_FIELD(n ".Blink", "PLIST_ENTRY64", (o) + 8, 8, XX_PSW_PTR)
#define XX_PSWL_US32(n, o)                                                                                   \
    XX_PSW_FIELD(n, "UNICODE_STRING32[1]", o, 8, 0), XX_PSW_FIELD(n ".Length", "USHORT", o, 2, XX_PSW_SIZE), \
        XX_PSW_FIELD(n ".MaximumLength", "USHORT", (o) + 2, 2, XX_PSW_SIZE), XX_PSW_FIELD(n ".Buffer", "PWSTR32", (o) + 4, 4, XX_PSW_PTR)
#define XX_PSWL_US64(n, o)                                                                                    \
    XX_PSW_FIELD(n, "UNICODE_STRING64[1]", o, 16, 0), XX_PSW_FIELD(n ".Length", "USHORT", o, 2, XX_PSW_SIZE), \
        XX_PSW_FIELD(n ".MaximumLength", "USHORT", (o) + 2, 2, XX_PSW_SIZE), XX_PSW_FIELD(n ".Buffer", "PWSTR64", (o) + 8, 8, XX_PSW_PTR)
#define XX_PSWL_TREE32(n, o)                                                                                                                         \
    XX_PSW_FIELD(n, "RTL_BALANCED_NODE32[1]", o, 12, 0), XX_PSW_FIELD(n ".Children", "PRTL_BALANCED_NODE32[2]", o, 8, 0),                            \
        XX_PSW_FIELD(n ".Left", "PRTL_BALANCED_NODE32", o, 4, XX_PSW_PTR), XX_PSW_FIELD(n ".Right", "PRTL_BALANCED_NODE32", (o) + 4, 4, XX_PSW_PTR), \
        XX_PSW_FIELD(n ".ParentValue", "ULONG_PTR32", (o) + 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),                                             \
        XX_PSW_FIELD(n ".Red", "bitfield:0:1", (o) + 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),                                                    \
        XX_PSW_FIELD(n ".Balance", "bitfield:0:2", (o) + 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
#define XX_PSWL_TREE64(n, o)                                                                                                                         \
    XX_PSW_FIELD(n, "RTL_BALANCED_NODE64[1]", o, 24, 0), XX_PSW_FIELD(n ".Children", "PRTL_BALANCED_NODE64[2]", o, 16, 0),                           \
        XX_PSW_FIELD(n ".Left", "PRTL_BALANCED_NODE64", o, 8, XX_PSW_PTR), XX_PSW_FIELD(n ".Right", "PRTL_BALANCED_NODE64", (o) + 8, 8, XX_PSW_PTR), \
        XX_PSW_FIELD(n ".ParentValue", "ULONG_PTR64", (o) + 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),                                            \
        XX_PSW_FIELD(n ".Red", "bitfield:0:1", (o) + 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),                                                   \
        XX_PSW_FIELD(n ".Balance", "bitfield:0:2", (o) + 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)

static const xx_data_struct_field_desc xx_pswl_ldr32[] = {XX_PSW_FIELD("Length", "ULONG", 0, 4, XX_PSW_SIZE),
                                                          XX_PSW_FIELD("Initialized", "BOOLEAN", 4, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                          XX_PSW_FIELD("AlignmentPadding0", "BYTE[3]", 5, 3, XX_PSW_RESERVED),
                                                          XX_PSW_FIELD("SsHandle", "HANDLE32", 8, 4, 0),
                                                          XX_PSWL_LIST32("InLoadOrderModuleList", 0x0c),
                                                          XX_PSWL_LIST32("InMemoryOrderModuleList", 0x14),
                                                          XX_PSWL_LIST32("InInitializationOrderModuleList", 0x1c),
                                                          XX_PSW_FIELD("EntryInProgress", "PVOID32", 0x24, 4, XX_PSW_PTR),
                                                          XX_PSW_FIELD("ShutdownInProgress", "BOOLEAN", 0x28, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                          XX_PSW_FIELD("AlignmentPadding1", "BYTE[3]", 0x29, 3, XX_PSW_RESERVED),
                                                          XX_PSW_FIELD("ShutdownThreadId", "HANDLE32", 0x2c, 4, XX_PSW_ID)};
static const xx_data_struct_field_desc xx_pswl_ldr64[] = {XX_PSW_FIELD("Length", "ULONG", 0, 4, XX_PSW_SIZE),
                                                          XX_PSW_FIELD("Initialized", "BOOLEAN", 4, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                          XX_PSW_FIELD("AlignmentPadding0", "BYTE[3]", 5, 3, XX_PSW_RESERVED),
                                                          XX_PSW_FIELD("SsHandle", "HANDLE64", 8, 8, 0),
                                                          XX_PSWL_LIST64("InLoadOrderModuleList", 0x10),
                                                          XX_PSWL_LIST64("InMemoryOrderModuleList", 0x20),
                                                          XX_PSWL_LIST64("InInitializationOrderModuleList", 0x30),
                                                          XX_PSW_FIELD("EntryInProgress", "PVOID64", 0x40, 8, XX_PSW_PTR),
                                                          XX_PSW_FIELD("ShutdownInProgress", "BOOLEAN", 0x48, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                          XX_PSW_FIELD("AlignmentPadding1", "BYTE[7]", 0x49, 7, XX_PSW_RESERVED),
                                                          XX_PSW_FIELD("ShutdownThreadId", "HANDLE64", 0x50, 8, XX_PSW_ID)};

#define XX_PSWL_FLAG(n, o, s, b) XX_PSW_FIELD(n, "bitfield:" #s ":" #b, o, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
#define XX_PSWL_FLAGS_COMMON(o)                                                                                                                                         \
    XX_PSW_FIELD("FlagGroup", "UCHAR[4]", o, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS), XX_PSW_FIELD("Flags", "ULONG", o, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),      \
        XX_PSWL_FLAG("PackagedBinary", o, 0, 1), XX_PSWL_FLAG("MarkedForRemoval", o, 1, 1), XX_PSWL_FLAG("ImageDll", o, 2, 1),                                          \
        XX_PSWL_FLAG("LoadNotificationsSent", o, 3, 1), XX_PSWL_FLAG("TelemetryEntryProcessed", o, 4, 1), XX_PSWL_FLAG("ProcessStaticImport", o, 5, 1),                 \
        XX_PSWL_FLAG("InLegacyLists", o, 6, 1), XX_PSWL_FLAG("InIndexes", o, 7, 1), XX_PSWL_FLAG("ShimDll", o, 8, 1), XX_PSWL_FLAG("InExceptionTable", o, 9, 1),        \
        XX_PSWL_FLAG("LoadInProgress", o, 12, 1), XX_PSWL_FLAG("LoadConfigProcessed", o, 13, 1), XX_PSWL_FLAG("EntryProcessed", o, 14, 1),                              \
        XX_PSWL_FLAG("ProtectDelayLoad", o, 15, 1), XX_PSWL_FLAG("DontCallForThreads", o, 18, 1), XX_PSWL_FLAG("ProcessAttachCalled", o, 19, 1),                        \
        XX_PSWL_FLAG("ProcessAttachFailed", o, 20, 1), XX_PSWL_FLAG("CorImage", o, 22, 1), XX_PSWL_FLAG("DontRelocate", o, 23, 1), XX_PSWL_FLAG("CorILOnly", o, 24, 1), \
        XX_PSWL_FLAG("ChpeImage", o, 25, 1), XX_PSWL_FLAG("Redirected", o, 28, 1), XX_PSWL_FLAG("ReservedFlags6", o, 29, 2),                                            \
        XX_PSWL_FLAG("CompatDatabaseProcessed", o, 31, 1)
#define XX_PSWL_FLAGS_19045(o)                                                                                                                                  \
    XX_PSWL_FLAGS_COMMON(o), XX_PSWL_FLAG("ReservedFlags1", o, 10, 2), XX_PSWL_FLAG("ReservedFlags3", o, 16, 2), XX_PSWL_FLAG("CorDeferredValidate", o, 21, 1), \
        XX_PSWL_FLAG("ReservedFlags5", o, 26, 2)
#define XX_PSWL_FLAGS_26100(o)                                                                                                                                         \
    XX_PSWL_FLAGS_COMMON(o), XX_PSWL_FLAG("VerifierProvider", o, 10, 1), XX_PSWL_FLAG("ShimEngineCalloutSent", o, 11, 1), XX_PSWL_FLAG("AuxIatCopyPrivate", o, 16, 1), \
        XX_PSWL_FLAG("ReservedFlags3", o, 17, 1), XX_PSWL_FLAG("ScpInExceptionTable", o, 21, 1), XX_PSWL_FLAG("ChpeEmulatorImage", o, 26, 1),                          \
        XX_PSWL_FLAG("ReservedFlags5", o, 27, 1)
#define XX_PSWL_ENTRY32_PREFIX                                                                                                             \
    XX_PSWL_LIST32("InLoadOrderLinks", 0), XX_PSWL_LIST32("InMemoryOrderLinks", 0x08), XX_PSWL_LIST32("InInitializationOrderLinks", 0x10), \
        XX_PSW_FIELD("DllBase", "PVOID32", 0x18, 4, XX_PSW_PTR), XX_PSW_FIELD("EntryPoint", "PDLL_INIT_ROUTINE32", 0x1c, 4, XX_PSW_PTR),   \
        XX_PSW_FIELD("SizeOfImage", "ULONG", 0x20, 4, XX_PSW_SIZE), XX_PSWL_US32("FullDllName", 0x24), XX_PSWL_US32("BaseDllName", 0x2c)
#define XX_PSWL_ENTRY64_PREFIX                                                                                                              \
    XX_PSWL_LIST64("InLoadOrderLinks", 0), XX_PSWL_LIST64("InMemoryOrderLinks", 0x10), XX_PSWL_LIST64("InInitializationOrderLinks", 0x20),  \
        XX_PSW_FIELD("DllBase", "PVOID64", 0x30, 8, XX_PSW_PTR), XX_PSW_FIELD("EntryPoint", "PDLL_INIT_ROUTINE64", 0x38, 8, XX_PSW_PTR),    \
        XX_PSW_FIELD("SizeOfImage", "ULONG", 0x40, 4, XX_PSW_SIZE), XX_PSW_FIELD("AlignmentPadding0", "BYTE[4]", 0x44, 4, XX_PSW_RESERVED), \
        XX_PSWL_US64("FullDllName", 0x48), XX_PSWL_US64("BaseDllName", 0x58)
#define XX_PSWL_ENTRY32_TAIL                                                                                                                                             \
    XX_PSW_FIELD("ObsoleteLoadCount", "USHORT", 0x38, 2, 0), XX_PSW_FIELD("TlsIndex", "USHORT", 0x3a, 2, XX_PSW_ID), XX_PSWL_LIST32("HashLinks", 0x3c),                  \
        XX_PSW_FIELD("TimeDateStamp", "ULONG", 0x44, 4, 0), XX_PSW_FIELD("EntryPointActivationContext", "PACTIVATION_CONTEXT32", 0x48, 4, XX_PSW_PTR),                   \
        XX_PSW_FIELD("Lock", "PVOID32", 0x4c, 4, XX_PSW_PTR), XX_PSW_FIELD("DdagNode", "PLDR_DDAG_NODE32", 0x50, 4, XX_PSW_PTR), XX_PSWL_LIST32("NodeModuleLink", 0x54), \
        XX_PSW_FIELD("LoadContext", "PLDRP_LOAD_CONTEXT32", 0x5c, 4, XX_PSW_PTR), XX_PSW_FIELD("ParentDllBase", "PVOID32", 0x60, 4, XX_PSW_PTR),                         \
        XX_PSW_FIELD("SwitchBackContext", "PVOID32", 0x64, 4, XX_PSW_PTR), XX_PSWL_TREE32("BaseAddressIndexNode", 0x68), XX_PSWL_TREE32("MappingInfoIndexNode", 0x74),   \
        XX_PSW_FIELD("OriginalBase", "ULONG", 0x80, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),                                                                  \
        XX_PSW_FIELD("AlignmentPadding0", "BYTE[4]", 0x84, 4, XX_PSW_RESERVED), XX_PSW_FIELD("LoadTime", "LARGE_INTEGER[1]", 0x88, 8, 0),                                \
        XX_PSW_FIELD("LoadTime.QuadPart", "int64", 0x88, 8, 0), XX_PSW_FIELD("BaseNameHashValue", "ULONG", 0x90, 4, 0), XX_PSW_FIELD("LoadReason", "int32", 0x94, 4, 0), \
        XX_PSW_FIELD("ImplicitPathOptions", "ULONG", 0x98, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS), XX_PSW_FIELD("ReferenceCount", "ULONG", 0x9c, 4, 0),                \
        XX_PSW_FIELD("DependentLoadFlags", "ULONG", 0xa0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS), XX_PSW_FIELD("SigningLevel", "UCHAR", 0xa4, 1, 0)
#define XX_PSWL_ENTRY64_TAIL                                                                                                                                           \
    XX_PSW_FIELD("ObsoleteLoadCount", "USHORT", 0x6c, 2, 0), XX_PSW_FIELD("TlsIndex", "USHORT", 0x6e, 2, XX_PSW_ID), XX_PSWL_LIST64("HashLinks", 0x70),                \
        XX_PSW_FIELD("TimeDateStamp", "ULONG", 0x80, 4, 0), XX_PSW_FIELD("AlignmentPadding1", "BYTE[4]", 0x84, 4, XX_PSW_RESERVED),                                    \
        XX_PSW_FIELD("EntryPointActivationContext", "PACTIVATION_CONTEXT64", 0x88, 8, XX_PSW_PTR), XX_PSW_FIELD("Lock", "PVOID64", 0x90, 8, XX_PSW_PTR),               \
        XX_PSW_FIELD("DdagNode", "PLDR_DDAG_NODE64", 0x98, 8, XX_PSW_PTR), XX_PSWL_LIST64("NodeModuleLink", 0xa0),                                                     \
        XX_PSW_FIELD("LoadContext", "PLDRP_LOAD_CONTEXT64", 0xb0, 8, XX_PSW_PTR), XX_PSW_FIELD("ParentDllBase", "PVOID64", 0xb8, 8, XX_PSW_PTR),                       \
        XX_PSW_FIELD("SwitchBackContext", "PVOID64", 0xc0, 8, XX_PSW_PTR), XX_PSWL_TREE64("BaseAddressIndexNode", 0xc8), XX_PSWL_TREE64("MappingInfoIndexNode", 0xe0), \
        XX_PSW_FIELD("OriginalBase", "ULONGLONG", 0xf8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS), XX_PSW_FIELD("LoadTime", "LARGE_INTEGER[1]", 0x100, 8, 0), \
        XX_PSW_FIELD("LoadTime.QuadPart", "int64", 0x100, 8, 0), XX_PSW_FIELD("BaseNameHashValue", "ULONG", 0x108, 4, 0),                                              \
        XX_PSW_FIELD("LoadReason", "int32", 0x10c, 4, 0), XX_PSW_FIELD("ImplicitPathOptions", "ULONG", 0x110, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),                \
        XX_PSW_FIELD("ReferenceCount", "ULONG", 0x114, 4, 0), XX_PSW_FIELD("DependentLoadFlags", "ULONG", 0x118, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),             \
        XX_PSW_FIELD("SigningLevel", "UCHAR", 0x11c, 1, 0)

static const xx_data_struct_field_desc xx_pswl_entry32_19045[] = {XX_PSWL_ENTRY32_PREFIX, XX_PSWL_FLAGS_19045(0x34), XX_PSWL_ENTRY32_TAIL,
                                                                  XX_PSW_FIELD("AlignmentPadding1", "BYTE[3]", 0xa5, 3, XX_PSW_RESERVED)};
static const xx_data_struct_field_desc xx_pswl_entry64_19045[] = {XX_PSWL_ENTRY64_PREFIX, XX_PSWL_FLAGS_19045(0x68), XX_PSWL_ENTRY64_TAIL,
                                                                  XX_PSW_FIELD("AlignmentPadding2", "BYTE[3]", 0x11d, 3, XX_PSW_RESERVED)};
static const xx_data_struct_field_desc xx_pswl_entry64_26100[] = {XX_PSWL_ENTRY64_PREFIX,
                                                                  XX_PSWL_FLAGS_26100(0x68),
                                                                  XX_PSWL_ENTRY64_TAIL,
                                                                  XX_PSW_FIELD("AlignmentPadding2", "BYTE[3]", 0x11d, 3, XX_PSW_RESERVED),
                                                                  XX_PSW_FIELD("CheckSum", "ULONG", 0x120, 4, 0),
                                                                  XX_PSW_FIELD("AlignmentPadding3", "BYTE[4]", 0x124, 4, XX_PSW_RESERVED),
                                                                  XX_PSW_FIELD("ActivePatchImageBase", "PVOID64", 0x128, 8, XX_PSW_PTR),
                                                                  XX_PSW_FIELD("HotPatchState", "int32", 0x130, 4, 0),
                                                                  XX_PSW_FIELD("AlignmentPadding4", "BYTE[4]", 0x134, 4, XX_PSW_RESERVED)};

static const xx_psw_nt_layout xx_pswl_ldr_profiles[] = {
    {19045, 0x014c, "NT 10.0.19045 I386/WOW64 PEB_LDR_DATA symbol profile", xx_pswl_ldr32, XX_PSW_COUNT(xx_pswl_ldr32), 0x30},
    {19045, 0x8664, "NT 10.0.19045 AMD64 PEB_LDR_DATA symbol profile", xx_pswl_ldr64, XX_PSW_COUNT(xx_pswl_ldr64), 0x58},
    {26100, 0x8664, "NT 10.0.26100 AMD64 PEB_LDR_DATA symbol profile", xx_pswl_ldr64, XX_PSW_COUNT(xx_pswl_ldr64), 0x58}};
static const xx_psw_nt_layout xx_pswl_entry_profiles[] = {
    {19045, 0x014c, "NT 10.0.19045 I386/WOW64 LDR_DATA_TABLE_ENTRY symbol profile", xx_pswl_entry32_19045, XX_PSW_COUNT(xx_pswl_entry32_19045), 0xa8},
    {19045, 0x8664, "NT 10.0.19045 AMD64 LDR_DATA_TABLE_ENTRY symbol profile", xx_pswl_entry64_19045, XX_PSW_COUNT(xx_pswl_entry64_19045), 0x120},
    {26100, 0x8664, "NT 10.0.26100 AMD64 LDR_DATA_TABLE_ENTRY symbol profile", xx_pswl_entry64_26100, XX_PSW_COUNT(xx_pswl_entry64_26100), 0x138}};

/* Match the PEB/TEB gate: neither an unknown build nor ARM64 pointer width
 * permits a private x86/AMD64 loader layout. NULL asks for the SDK fallback. */
static const xx_psw_nt_layout *xx_pswl_profile(const xx_psw_target *target, unsigned width, bool entry)
{
    const xx_psw_nt_layout *profiles = entry ? xx_pswl_entry_profiles : xx_pswl_ldr_profiles;
    size_t count = entry ? XX_PSW_COUNT(xx_pswl_entry_profiles) : XX_PSW_COUNT(xx_pswl_ldr_profiles);
    USHORT machine;
    if (!target || !target->version_available || target->major != 10 || target->minor != 0) return NULL;
    machine = xx_psw_profile_machine(target, width);
    for (size_t i = 0; machine && i < count; ++i)
        if (profiles[i].build == target->build && profiles[i].machine == machine) return profiles + i;
    return NULL;
}

static const xx_data_struct_field_desc xx_pswl_balanced32[] = {XX_PSW_FIELD("Children", "PRTL_BALANCED_NODE32[2]", 0, 8, 0),
                                                               XX_PSW_FIELD("Left", "PRTL_BALANCED_NODE32", 0, 4, XX_PSW_PTR),
                                                               XX_PSW_FIELD("Right", "PRTL_BALANCED_NODE32", 4, 4, XX_PSW_PTR),
                                                               XX_PSW_FIELD("ParentValue", "ULONG_PTR32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                               XX_PSW_FIELD("Red", "bitfield:0:1", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                               XX_PSW_FIELD("Balance", "bitfield:0:2", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)};
static const xx_data_struct_field_desc xx_pswl_balanced64[] = {XX_PSW_FIELD("Children", "PRTL_BALANCED_NODE64[2]", 0, 16, 0),
                                                               XX_PSW_FIELD("Left", "PRTL_BALANCED_NODE64", 0, 8, XX_PSW_PTR),
                                                               XX_PSW_FIELD("Right", "PRTL_BALANCED_NODE64", 8, 8, XX_PSW_PTR),
                                                               XX_PSW_FIELD("ParentValue", "ULONG_PTR64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                               XX_PSW_FIELD("Red", "bitfield:0:1", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                               XX_PSW_FIELD("Balance", "bitfield:0:2", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)};
static const xx_data_struct_field_desc xx_pswl_ddag32[] = {XX_PSWL_LIST32("Modules", 0),
                                                           XX_PSW_FIELD("ServiceTagList", "PLDR_SERVICE_TAG_RECORD32", 8, 4, XX_PSW_PTR),
                                                           XX_PSW_FIELD("LoadCount", "ULONG", 0x0c, 4, 0),
                                                           XX_PSW_FIELD("LoadWhileUnloadingCount", "ULONG", 0x10, 4, 0),
                                                           XX_PSW_FIELD("LowestLink", "ULONG", 0x14, 4, 0),
                                                           XX_PSW_FIELD("Dependencies", "LDRP_CSLIST32[1]", 0x18, 4, 0),
                                                           XX_PSW_FIELD("Dependencies.Tail", "PSINGLE_LIST_ENTRY32", 0x18, 4, XX_PSW_PTR),
                                                           XX_PSW_FIELD("RemovalLink", "SINGLE_LIST_ENTRY32[1]", 0x18, 4, 0),
                                                           XX_PSW_FIELD("RemovalLink.Next", "PSINGLE_LIST_ENTRY32", 0x18, 4, XX_PSW_PTR),
                                                           XX_PSW_FIELD("IncomingDependencies", "LDRP_CSLIST32[1]", 0x1c, 4, 0),
                                                           XX_PSW_FIELD("IncomingDependencies.Tail", "PSINGLE_LIST_ENTRY32", 0x1c, 4, XX_PSW_PTR),
                                                           XX_PSW_FIELD("State", "int32", 0x20, 4, 0),
                                                           XX_PSW_FIELD("CondenseLink", "SINGLE_LIST_ENTRY32[1]", 0x24, 4, 0),
                                                           XX_PSW_FIELD("CondenseLink.Next", "PSINGLE_LIST_ENTRY32", 0x24, 4, XX_PSW_PTR),
                                                           XX_PSW_FIELD("PreorderNumber", "ULONG", 0x28, 4, 0)};
static const xx_data_struct_field_desc xx_pswl_ddag64[] = {XX_PSWL_LIST64("Modules", 0),
                                                           XX_PSW_FIELD("ServiceTagList", "PLDR_SERVICE_TAG_RECORD64", 0x10, 8, XX_PSW_PTR),
                                                           XX_PSW_FIELD("LoadCount", "ULONG", 0x18, 4, 0),
                                                           XX_PSW_FIELD("LoadWhileUnloadingCount", "ULONG", 0x1c, 4, 0),
                                                           XX_PSW_FIELD("LowestLink", "ULONG", 0x20, 4, 0),
                                                           XX_PSW_FIELD("AlignmentPadding0", "BYTE[4]", 0x24, 4, XX_PSW_RESERVED),
                                                           XX_PSW_FIELD("Dependencies", "LDRP_CSLIST64[1]", 0x28, 8, 0),
                                                           XX_PSW_FIELD("Dependencies.Tail", "PSINGLE_LIST_ENTRY64", 0x28, 8, XX_PSW_PTR),
                                                           XX_PSW_FIELD("RemovalLink", "SINGLE_LIST_ENTRY64[1]", 0x28, 8, 0),
                                                           XX_PSW_FIELD("RemovalLink.Next", "PSINGLE_LIST_ENTRY64", 0x28, 8, XX_PSW_PTR),
                                                           XX_PSW_FIELD("IncomingDependencies", "LDRP_CSLIST64[1]", 0x30, 8, 0),
                                                           XX_PSW_FIELD("IncomingDependencies.Tail", "PSINGLE_LIST_ENTRY64", 0x30, 8, XX_PSW_PTR),
                                                           XX_PSW_FIELD("State", "int32", 0x38, 4, 0),
                                                           XX_PSW_FIELD("AlignmentPadding1", "BYTE[4]", 0x3c, 4, XX_PSW_RESERVED),
                                                           XX_PSW_FIELD("CondenseLink", "SINGLE_LIST_ENTRY64[1]", 0x40, 8, 0),
                                                           XX_PSW_FIELD("CondenseLink.Next", "PSINGLE_LIST_ENTRY64", 0x40, 8, XX_PSW_PTR),
                                                           XX_PSW_FIELD("PreorderNumber", "ULONG", 0x48, 4, 0),
                                                           XX_PSW_FIELD("AlignmentPadding2", "BYTE[4]", 0x4c, 4, XX_PSW_RESERVED)};
static const xx_data_struct_field_desc xx_pswl_tag32[] = {XX_PSW_FIELD("Next", "PLDR_SERVICE_TAG_RECORD32", 0, 4, XX_PSW_PTR),
                                                          XX_PSW_FIELD("ServiceTag", "ULONG", 4, 4, XX_PSW_ID)};
static const xx_data_struct_field_desc xx_pswl_tag64[] = {XX_PSW_FIELD("Next", "PLDR_SERVICE_TAG_RECORD64", 0, 8, XX_PSW_PTR),
                                                          XX_PSW_FIELD("ServiceTag", "ULONG", 8, 4, XX_PSW_ID),
                                                          XX_PSW_FIELD("AlignmentPadding0", "BYTE[4]", 12, 4, XX_PSW_RESERVED)};

static bool xx_pswl_address(uint64_t base, uint64_t offset, size_t size, unsigned width, uint64_t *address)
{
    uint64_t maximum = width == 4 ? UINT32_MAX : UINT64_MAX;
    if ((width != 4 && width != 8) || !base || base > maximum || offset > maximum - base) return false;
    *address = base + offset;
    return !size || (uint64_t)(size - 1) <= maximum - *address;
}

/* Called only when a real, directly reachable child is about to be omitted. */
static bool xx_pswl_room(xx_ps_context *context)
{
    if (context->status != XX_PROCESS_STRUCTS_OK) return false;
    if (!xx_psw_full(context)) return true;
    context->snapshot->truncated = true;
    xx_psw_warn_once(context, "The structure limit was reached.");
    return false;
}

/* Finite work per loader entry: two embedded tree nodes, one DDAG node, and
 * at most its first service-tag record. Do not chase tree/list/graph links.
 * The loader walker already owns module cycle/change guards and module caps.
 * No xx_process_structure pointer survives a later xx_ps_add/reallocation. */
static void xx_pswl_children(xx_ps_context *context, xx_io_device *device, uint64_t entry, unsigned width, const xx_psw_target *target)
{
    const xx_psw_nt_layout *profile = xx_pswl_profile(target, width, true);
    const xx_data_struct_field_desc *tree = width == 8 ? xx_pswl_balanced64 : xx_pswl_balanced32;
    size_t tree_size = width == 8 ? 24 : 12;
    uint64_t address, ddag = 0, service = 0;
    char child_layout[96];
    const uint64_t offsets[2] = {width == 8 ? 0xc8u : 0x68u, width == 8 ? 0xe0u : 0x74u};
    static const char *const labels32[] = {"RTL_BALANCED_NODE32 (BaseAddressIndexNode)", "RTL_BALANCED_NODE32 (MappingInfoIndexNode)"};
    static const char *const labels64[] = {"RTL_BALANCED_NODE64 (BaseAddressIndexNode)", "RTL_BALANCED_NODE64 (MappingInfoIndexNode)"};
    if (!profile || !xx_pswl_address(entry, 0, profile->size, width, &address) || (entry & (width - 1u))) return;
    (void)snprintf(child_layout, sizeof(child_layout), "NT 10.0.%lu %u-bit loader child symbol profile; graph links are not traversed", (unsigned long)target->build,
                   width * 8);
    for (size_t i = 0; i < 2; ++i) {
        if (!xx_pswl_address(entry, offsets[i], tree_size, width, &address)) continue;
        if (!xx_pswl_room(context)) return;
        if (!xx_psw_memory(context, device, width == 8 ? labels64[i] : labels32[i], child_layout, address, tree_size, 0, tree, XX_PSW_COUNT(xx_pswl_balanced32))) return;
    }
    if (!xx_psw_pointer(device, entry, width == 8 ? 0x98 : 0x50, width, &ddag) || !ddag) return;
    if ((ddag & (width - 1u)) || !xx_pswl_address(ddag, 0, width == 8 ? 0x50 : 0x2c, width, &address)) {
        xx_psw_warn_once(context, "An invalid loader DDAG pointer was not followed.");
        return;
    }
    if (!xx_pswl_room(context)) return;
    if (!xx_psw_memory(context, device, width == 8 ? "LDR_DDAG_NODE64" : "LDR_DDAG_NODE32", child_layout, address, width == 8 ? 0x50 : 0x2c, 0,
                       width == 8 ? xx_pswl_ddag64 : xx_pswl_ddag32, width == 8 ? XX_PSW_COUNT(xx_pswl_ddag64) : XX_PSW_COUNT(xx_pswl_ddag32)))
        return;
    if (!xx_psw_pointer(device, ddag, width == 8 ? 0x10 : 8, width, &service) || !service) return;
    if ((service & (width - 1u)) || !xx_pswl_address(service, 0, width == 8 ? 16 : 8, width, &address)) {
        xx_psw_warn_once(context, "An invalid loader service-tag pointer was not followed.");
        return;
    }
    if (!xx_pswl_room(context)) return;
    (void)xx_psw_memory(context, device, width == 8 ? "LDR_SERVICE_TAG_RECORD64 (head)" : "LDR_SERVICE_TAG_RECORD32 (head)",
                        "phnt direct service-tag head; Next and DDAG dependency graphs are not traversed", address, width == 8 ? 16 : 8, 0,
                        width == 8 ? xx_pswl_tag64 : xx_pswl_tag32, width == 8 ? XX_PSW_COUNT(xx_pswl_tag64) : XX_PSW_COUNT(xx_pswl_tag32));
}

#undef XX_PSWL_ENTRY64_TAIL
#undef XX_PSWL_ENTRY32_TAIL
#undef XX_PSWL_ENTRY64_PREFIX
#undef XX_PSWL_ENTRY32_PREFIX
#undef XX_PSWL_FLAGS_26100
#undef XX_PSWL_FLAGS_19045
#undef XX_PSWL_FLAGS_COMMON
#undef XX_PSWL_FLAG
#undef XX_PSWL_TREE64
#undef XX_PSWL_TREE32
#undef XX_PSWL_US64
#undef XX_PSWL_US32
#undef XX_PSWL_LIST64
#undef XX_PSWL_LIST32
#endif
