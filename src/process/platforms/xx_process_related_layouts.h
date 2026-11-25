/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_PROCESS_RELATED_LAYOUTS_H
#define XX_PROCESS_RELATED_LAYOUTS_H

/* Private, read-only child views. Include after xx_psw_unicode and the common
 * xx_psw_* helpers. Exact parent profiles supply every PEB/TEB member offset;
 * I386 and AMD64 target addresses are never laid out with host C structs.
 *
 * Layout facts and field semantics:
 * https://raw.githubusercontent.com/microsoft/win32metadata/main/generation/WinSDK/RecompiledIdlHeaders/um/winnt.h
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_RTL_CRITICAL_SECTION
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_RTL_CRITICAL_SECTION
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_RTL_CRITICAL_SECTION
 * https://raw.githubusercontent.com/winsiderss/systeminformer/master/phnt/include/ntrtl.h
 * https://raw.githubusercontent.com/winsiderss/systeminformer/master/phnt/include/ntsxs.h
 * https://raw.githubusercontent.com/winsiderss/systeminformer/master/phnt/include/ntwow64.h
 * The parent TEB descriptor header documents its concrete symbol sources.
 * System Informer's current source is MIT licensed. These descriptors contain
 * independently written factual metadata, without copied implementations.
 *
 * Scope: PEB locks/debug records; TEB ClientId/RealClientId/GdiTebBatch;
 * activation stacks and their active frame heads. Previous frame pointers and
 * lock-list links remain visible values. Reading them never acquires a target
 * lock, calls target code, changes protection, or changes target memory.
 */

static const xx_data_struct_field_desc xx_pswr_cs32[] = {XX_PSW_FIELD("DebugInfo", "PRTL_CRITICAL_SECTION_DEBUG32", 0, 4, XX_PSW_PTR),
                                                         XX_PSW_FIELD("LockCount", "int32", 4, 4, 0),
                                                         XX_PSW_FIELD("RecursionCount", "int32", 8, 4, 0),
                                                         XX_PSW_FIELD("OwningThread", "HANDLE32", 12, 4, XX_PSW_ID),
                                                         XX_PSW_FIELD("LockSemaphore", "HANDLE32", 16, 4, XX_PSW_ID),
                                                         XX_PSW_FIELD("SpinCount", "ULONG_PTR32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                         XX_PSW_FIELD("SpinCount.NoDebugInfo", "bitfield:24:1", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                         XX_PSW_FIELD("SpinCount.DynamicSpin", "bitfield:25:1", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                         XX_PSW_FIELD("SpinCount.StaticInit", "bitfield:26:1", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                         XX_PSW_FIELD("SpinCount.ResourceType", "bitfield:27:1", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                         XX_PSW_FIELD("SpinCount.ForceDebugInfo", "bitfield:28:1", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)};
static const xx_data_struct_field_desc xx_pswr_cs64[] = {XX_PSW_FIELD("DebugInfo", "PRTL_CRITICAL_SECTION_DEBUG64", 0, 8, XX_PSW_PTR),
                                                         XX_PSW_FIELD("LockCount", "int32", 8, 4, 0),
                                                         XX_PSW_FIELD("RecursionCount", "int32", 12, 4, 0),
                                                         XX_PSW_FIELD("OwningThread", "HANDLE64", 16, 8, XX_PSW_ID),
                                                         XX_PSW_FIELD("LockSemaphore", "HANDLE64", 24, 8, XX_PSW_ID),
                                                         XX_PSW_FIELD("SpinCount", "ULONG_PTR64", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                         XX_PSW_FIELD("SpinCount.NoDebugInfo", "bitfield:24:1", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                         XX_PSW_FIELD("SpinCount.DynamicSpin", "bitfield:25:1", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                         XX_PSW_FIELD("SpinCount.StaticInit", "bitfield:26:1", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                         XX_PSW_FIELD("SpinCount.ResourceType", "bitfield:27:1", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                         XX_PSW_FIELD("SpinCount.ForceDebugInfo", "bitfield:28:1", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)};
static const xx_data_struct_field_desc xx_pswr_cs_debug32[] = {XX_PSW_FIELD("Type", "uint16", 0, 2, XX_PSW_ID),
                                                               XX_PSW_FIELD("CreatorBackTraceIndex", "uint16", 2, 2, XX_PSW_ID),
                                                               XX_PSW_FIELD("CriticalSection", "PRTL_CRITICAL_SECTION32", 4, 4, XX_PSW_PTR),
                                                               XX_PSW_FIELD("ProcessLocksList", "LIST_ENTRY32[1]", 8, 8, 0),
                                                               XX_PSW_FIELD("ProcessLocksList.Flink", "PLIST_ENTRY32", 8, 4, XX_PSW_PTR),
                                                               XX_PSW_FIELD("ProcessLocksList.Blink", "PLIST_ENTRY32", 12, 4, XX_PSW_PTR),
                                                               XX_PSW_FIELD("EntryCount", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
                                                               XX_PSW_FIELD("ContentionCount", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
                                                               XX_PSW_FIELD("Flags", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                               XX_PSW_FIELD("Flags.StaticInit", "bitfield:0:1", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                               XX_PSW_FIELD("CreatorBackTraceIndexHigh", "uint16", 28, 2, XX_PSW_ID),
                                                               XX_PSW_FIELD("Identifier", "uint16", 30, 2, XX_PSW_ID)};
static const xx_data_struct_field_desc xx_pswr_cs_debug64[] = {XX_PSW_FIELD("Type", "uint16", 0, 2, XX_PSW_ID),
                                                               XX_PSW_FIELD("CreatorBackTraceIndex", "uint16", 2, 2, XX_PSW_ID),
                                                               XX_PSW_FIELD("AlignmentPadding", "uint8[4]", 4, 4, XX_PSW_RESERVED),
                                                               XX_PSW_FIELD("CriticalSection", "PRTL_CRITICAL_SECTION64", 8, 8, XX_PSW_PTR),
                                                               XX_PSW_FIELD("ProcessLocksList", "LIST_ENTRY64[1]", 16, 16, 0),
                                                               XX_PSW_FIELD("ProcessLocksList.Flink", "PLIST_ENTRY64", 16, 8, XX_PSW_PTR),
                                                               XX_PSW_FIELD("ProcessLocksList.Blink", "PLIST_ENTRY64", 24, 8, XX_PSW_PTR),
                                                               XX_PSW_FIELD("EntryCount", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
                                                               XX_PSW_FIELD("ContentionCount", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
                                                               XX_PSW_FIELD("Flags", "uint32", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                               XX_PSW_FIELD("Flags.StaticInit", "bitfield:0:1", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                               XX_PSW_FIELD("CreatorBackTraceIndexHigh", "uint16", 44, 2, XX_PSW_ID),
                                                               XX_PSW_FIELD("Identifier", "uint16", 46, 2, XX_PSW_ID)};
static const xx_data_struct_field_desc xx_pswr_activation_frame32[] = {
    XX_PSW_FIELD("Previous", "PRTL_ACTIVATION_CONTEXT_STACK_FRAME32", 0, 4, XX_PSW_PTR),
    XX_PSW_FIELD("ActivationContext", "PACTIVATION_CONTEXT32", 4, 4, XX_PSW_PTR),
    XX_PSW_FIELD("Flags", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ReleaseOnDeactivation", "bitfield:0:1", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.NoDeactivate", "bitfield:1:1", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.OnFreeList", "bitfield:2:1", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.HeapAllocated", "bitfield:3:1", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.NotReallyActivated", "bitfield:4:1", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)};
static const xx_data_struct_field_desc xx_pswr_activation_frame64[] = {
    XX_PSW_FIELD("Previous", "PRTL_ACTIVATION_CONTEXT_STACK_FRAME64", 0, 8, XX_PSW_PTR),
    XX_PSW_FIELD("ActivationContext", "PACTIVATION_CONTEXT64", 8, 8, XX_PSW_PTR),
    XX_PSW_FIELD("Flags", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.ReleaseOnDeactivation", "bitfield:0:1", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.NoDeactivate", "bitfield:1:1", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.OnFreeList", "bitfield:2:1", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.HeapAllocated", "bitfield:3:1", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Flags.NotReallyActivated", "bitfield:4:1", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("AlignmentPadding", "uint8[4]", 20, 4, XX_PSW_RESERVED)};

static const xx_data_struct_field_desc *xx_pswr_field(const xx_psw_nt_layout *profile, const wchar_t *name)
{
    if (!profile) return NULL;
    for (size_t i = 0; i < profile->count; ++i)
        if (profile->fields[i].name && !wcscmp(profile->fields[i].name, name)) return profile->fields + i;
    return NULL;
}

static bool xx_pswr_span(uint64_t address, uint64_t size, unsigned width)
{
    uint64_t maximum = width == 4 ? UINT32_MAX : UINT64_MAX;
    return (width == 4 || width == 8) && address && address <= maximum && (!size || size - 1 <= maximum - address);
}

static bool xx_pswr_member_address(const xx_psw_nt_layout *profile, const wchar_t *name, uint64_t parent, unsigned width, uint64_t *address)
{
    const xx_data_struct_field_desc *field = xx_pswr_field(profile, name);
    if (!field || field->rel_offset < 0 || field->size <= 0 || (uint64_t)field->rel_offset > profile->size ||
        (uint64_t)field->size > profile->size - (size_t)field->rel_offset || (uint64_t)field->rel_offset > UINT64_MAX - parent)
        return false;
    *address = parent + (uint64_t)field->rel_offset;
    return xx_pswr_span(*address, (uint64_t)field->size, width);
}

static bool xx_pswr_member_pointer(xx_io_device *device, const xx_psw_nt_layout *profile, const wchar_t *name, uint64_t parent, unsigned width, uint64_t *value)
{
    const xx_data_struct_field_desc *field = xx_pswr_field(profile, name);
    uint64_t address;
    return field && field->size == (int64_t)width && xx_pswr_member_address(profile, name, parent, width, &address) && xx_psw_pointer(device, address, 0, width, value);
}

static bool xx_pswr_seen(const xx_ps_context *context, const char *name, uint64_t address, uint64_t thread)
{
    for (size_t i = 0; i < context->snapshot->structure_count; ++i) {
        const xx_process_structure *item = context->snapshot->structures + i;
        if (item->has_address && item->address == address && item->thread_id == thread && !strcmp(item->name, name)) return true;
    }
    return false;
}

/* A directly reachable record is known to be omitted at this call site. */
static bool xx_pswr_room(xx_ps_context *context)
{
    if (context->status != XX_PROCESS_STRUCTS_OK) return false;
    if (!xx_psw_full(context)) return true;
    context->snapshot->truncated = true;
    xx_psw_warn_once(context, "The structure limit was reached.");
    return false;
}

/* Derive a child view from its concrete parent's named members. Names/types
 * point into static parent literals; only the temporary descriptor array is
 * allocated. xx_psw_memory copies descriptors into snapshot-owned fields. */
static bool xx_pswr_embedded(xx_ps_context *context, xx_io_device *device, const xx_psw_nt_layout *profile, const wchar_t *member, const char *name, uint64_t address,
                             unsigned width, uint64_t thread)
{
    const xx_data_struct_field_desc *parent = xx_pswr_field(profile, member);
    xx_data_struct_field_desc *fields;
    size_t prefix = wcslen(member), count = 0;
    bool result;
    if (!parent || parent->rel_offset < 0 || parent->size <= 0 || parent->size > 65536) return true;
    if (!xx_pswr_span(address, (uint64_t)parent->size, width)) {
        xx_psw_warn_once(context, "A related structure address was outside the target ABI range.");
        return true;
    }
    if (xx_pswr_seen(context, name, address, thread)) return true;
    if (!xx_pswr_room(context)) return false;
    if (profile->count > SIZE_MAX / sizeof(*fields)) {
        context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
        return false;
    }
    fields = (xx_data_struct_field_desc *)calloc(profile->count, sizeof(*fields));
    if (!fields) {
        context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
        return false;
    }
    for (size_t i = 0; i < profile->count; ++i) {
        const xx_data_struct_field_desc *field = profile->fields + i;
        if (!field->name || wcsncmp(field->name, member, prefix) || field->name[prefix] != L'.') continue;
        if (field->rel_offset < parent->rel_offset || field->size < 0 || (uint64_t)(field->rel_offset - parent->rel_offset) > (uint64_t)parent->size ||
            (uint64_t)field->size > (uint64_t)parent->size - (uint64_t)(field->rel_offset - parent->rel_offset)) {
            context->status = XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
            free(fields);
            return false;
        }
        fields[count] = *field;
        fields[count].name = field->name + prefix + 1;
        fields[count].rel_offset -= parent->rel_offset;
        ++count;
    }
    if (!count) {
        free(fields);
        return true;
    }
    result = xx_psw_memory(context, device, name, "Child view from selected parent structure profile", address, (size_t)parent->size, thread, fields, count) != NULL;
    free(fields);
    return result;
}

static bool xx_pswr_critical_section(xx_ps_context *context, xx_io_device *device, uint64_t address, unsigned width, const char *label)
{
    char name[96];
    uint64_t debug = 0, back = 0;
    size_t size = width == 8 ? 40 : 24;
    if (!xx_pswr_span(address, size, width) || (address & (width - 1u))) {
        xx_psw_warn_once(context, "A related critical-section pointer was invalid.");
        return true;
    }
    (void)snprintf(name, sizeof(name), "RTL_CRITICAL_SECTION%u (%s)", width * 8, label);
    if (!xx_psw_memory(context, device, name, "Published RTL_CRITICAL_SECTION target ABI", address, size, 0, width == 8 ? xx_pswr_cs64 : xx_pswr_cs32,
                       width == 8 ? XX_PSW_COUNT(xx_pswr_cs64) : XX_PSW_COUNT(xx_pswr_cs32)))
        return false;
    if (!xx_psw_pointer(device, address, 0, width, &debug) || !debug || debug == (width == 4 ? UINT32_MAX : UINT64_MAX)) return true;
    if (!xx_pswr_span(debug, width == 8 ? 48 : 32, width) || (debug & (width - 1u))) {
        xx_psw_warn_once(context, "A related critical-section debug pointer was invalid.");
        return true;
    }
    if (xx_psw_pointer(device, debug, width == 8 ? 8 : 4, width, &back) && back != address) {
        xx_psw_warn_once(context, "A critical-section debug backlink changed or was inconsistent.");
        return true;
    }
    (void)snprintf(name, sizeof(name), "RTL_CRITICAL_SECTION_DEBUG%u (%s)", width * 8, label);
    return xx_psw_memory(context, device, name, "Published RTL_CRITICAL_SECTION_DEBUG target ABI", debug, width == 8 ? 48 : 32, 0,
                         width == 8 ? xx_pswr_cs_debug64 : xx_pswr_cs_debug32, width == 8 ? XX_PSW_COUNT(xx_pswr_cs_debug64) : XX_PSW_COUNT(xx_pswr_cs_debug32)) != NULL;
}

static bool xx_pswr_activation(xx_ps_context *context, xx_io_device *device, const xx_psw_nt_layout *profile, uint64_t address, unsigned width, uint64_t thread)
{
    char name[96];
    uint64_t frame = 0;
    (void)snprintf(name, sizeof(name), "ACTIVATION_CONTEXT_STACK%u", width * 8);
    if (!xx_pswr_embedded(context, device, profile, L"_ActivationStack", name, address, width, thread)) return false;
    if (!xx_pswr_span(address, width, width) || !xx_psw_pointer(device, address, 0, width, &frame) || !frame) return true;
    if (!xx_pswr_span(frame, width == 8 ? 24 : 12, width) || (frame & (width - 1u))) {
        xx_psw_warn_once(context, "An active activation-frame pointer was invalid.");
        return true;
    }
    (void)snprintf(name, sizeof(name), "RTL_ACTIVATION_CONTEXT_STACK_FRAME%u (active)", width * 8);
    if (xx_pswr_seen(context, name, frame, thread)) return true;
    return xx_psw_memory(context, device, name, "Active frame head; Previous remains a visible pointer", frame, width == 8 ? 24 : 12, thread,
                         width == 8 ? xx_pswr_activation_frame64 : xx_pswr_activation_frame32,
                         width == 8 ? XX_PSW_COUNT(xx_pswr_activation_frame64) : XX_PSW_COUNT(xx_pswr_activation_frame32)) != NULL;
}

static void xx_pswr_peb_children(xx_ps_context *context, xx_io_device *device, uint64_t address, unsigned width, const xx_psw_target *target)
{
    const xx_psw_nt_layout *profile = xx_psw_profile(target, width, false);
    uint64_t lock = 0;
    if (!profile || context->status != XX_PROCESS_STRUCTS_OK) return;
    if (xx_pswr_member_pointer(device, profile, L"FastPebLock", address, width, &lock) && lock && !xx_pswr_critical_section(context, device, lock, width, "FastPebLock"))
        return;
    if (xx_pswr_member_pointer(device, profile, L"LoaderLock", address, width, &lock) && lock) (void)xx_pswr_critical_section(context, device, lock, width, "LoaderLock");
}

static void xx_pswr_teb_children(xx_ps_context *context, xx_io_device *device, uint64_t address, unsigned width, uint64_t thread, const xx_psw_target *target)
{
    const xx_psw_nt_layout *profile = xx_psw_profile(target, width, true);
    uint64_t child = 0, embedded_stack = 0, pointed_stack = 0;
    char name[96];
    if (!profile || context->status != XX_PROCESS_STRUCTS_OK) return;
    (void)snprintf(name, sizeof(name), "CLIENT_ID%u", width * 8);
    if (xx_pswr_member_address(profile, L"ClientId", address, width, &child) && !xx_pswr_embedded(context, device, profile, L"ClientId", name, child, width, thread))
        return;
    (void)snprintf(name, sizeof(name), "CLIENT_ID%u (RealClientId)", width * 8);
    if (xx_pswr_member_address(profile, L"RealClientId", address, width, &child) &&
        !xx_pswr_embedded(context, device, profile, L"RealClientId", name, child, width, thread))
        return;
    (void)snprintf(name, sizeof(name), "GDI_TEB_BATCH%u", width * 8);
    if (xx_pswr_member_address(profile, L"GdiTebBatch", address, width, &child) &&
        !xx_pswr_embedded(context, device, profile, L"GdiTebBatch", name, child, width, thread))
        return;
    if (xx_pswr_member_address(profile, L"_ActivationStack", address, width, &embedded_stack) &&
        !xx_pswr_activation(context, device, profile, embedded_stack, width, thread))
        return;
    if (xx_pswr_member_pointer(device, profile, L"ActivationContextStackPointer", address, width, &pointed_stack) && pointed_stack && pointed_stack != embedded_stack)
        (void)xx_pswr_activation(context, device, profile, pointed_stack, width, thread);
}

#endif /* XX_PROCESS_RELATED_LAYOUTS_H */
