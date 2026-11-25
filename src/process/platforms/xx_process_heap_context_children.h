/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_PROCESS_HEAP_CONTEXT_CHILDREN_H
#define XX_PROCESS_HEAP_CONTEXT_CHILDREN_H

/* Standalone views of the embedded VS and LFH contexts of a validated full
 * SEGMENT_HEAP. Include after related-layout helpers and the native/WOW64
 * segment-heap descriptor headers, before heap discovery.
 *
 * The complete selected parent supplies offsets, sizes, existing nested
 * descriptors and aliases. Static supplements below complete every named
 * top-level native context member, plus callback slots/configuration flags.
 * All field names/types remain static literals or suffixes of static parent
 * literals. Only the temporary descriptor vector is allocated and freed.
 *
 * Native symbol profiles are I386 19045 and AMD64 19045/26100. The separate
 * WOW64 parent profiles retain their exact loaded-NTDLL GUID/age gates and
 * complete PDB-derived embedded fields. No profile is inferred from width.
 * Primary symbol sources:
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_HEAP_VS_CONTEXT
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_HEAP_LFH_CONTEXT
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_HEAP_VS_CONTEXT
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_HEAP_LFH_CONTEXT
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_HEAP_VS_CONTEXT
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_HEAP_LFH_CONTEXT
 * The same families supply HEAP_VS_DELAY_FREE_CONTEXT, HEAP_SUBALLOCATOR_CALLBACKS,
 * RTL_HP_VS_CONFIG, RTL_HP_LFH_CONFIG and HEAP_LFH_SUBSEGMENT_STATS; the 26100
 * family additionally supplies HEAP_LFH_CONFIG, HEAP_LFH_HEAT_MAP and SLOT_MAP.
 * The WOW64 descriptor header records both exact Microsoft PDB URLs.
 *
 * Large bucket/heat-map/slot arrays retain complete typed spans. Encoded
 * links, callback slots and locks remain opaque stored data. This helper
 * follows no allocation graph and never treats a legacy LFH_HEAP as this
 * distinct HEAP_LFH_CONTEXT type. Parent and child bytes are best-effort
 * snapshots; the caller rechecks heap markers and discards changed ranges.
 */

static const xx_data_struct_field_desc xx_pswhcc_vs32_19045_extra[] = {
    XX_PSW_FIELD("DelayFreeContext", "HEAP_VS_DELAY_FREE_CONTEXT32[1]", 0x40, 8, 0),
    XX_PSW_FIELD("DelayFreeContext.ListHead", "SLIST_HEADER32[1]", 0x40, 8, 0),
    XX_PSW_FIELD("Callbacks", "HEAP_SUBALLOCATOR_CALLBACKS32_19045[1]", 0x84, 20, 0),
    XX_PSW_FIELD("Callbacks.Allocate", "uint32", 0x84, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Free", "uint32", 0x88, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Commit", "uint32", 0x8C, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Decommit", "uint32", 0x90, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.ExtendContext", "uint32", 0x94, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config", "RTL_HP_VS_CONFIG32_19045[1]", 0x98, 4, 0),
    XX_PSW_FIELD("Config.Flags", "uint32", 0x98, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config.Flags.PageAlignLargeAllocs", "bitfield:0:1", 0x98, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config.Flags.FullDecommit", "bitfield:1:1", 0x98, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config.Flags.EnableDelayFree", "bitfield:2:1", 0x98, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhcc_lfh32_19045_extra[] = {
    XX_PSW_FIELD("Callbacks", "HEAP_SUBALLOCATOR_CALLBACKS32_19045[1]", 0x4, 20, 0),
    XX_PSW_FIELD("Callbacks.Allocate", "uint32", 0x4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Free", "uint32", 0x8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Commit", "uint32", 0xC, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Decommit", "uint32", 0x10, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.ExtendContext", "uint32", 0x14, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config", "RTL_HP_LFH_CONFIG32_19045[1]", 0x20, 4, 0),
    XX_PSW_FIELD("Config.MaxBlockSize", "uint16", 0x20, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("Config.FlagsStorage", "uint16", 0x22, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config.WitholdPageCrossingBlocks", "bitfield:0:1", 0x22, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config.DisableRandomization", "bitfield:1:1", 0x22, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("BucketStats", "HEAP_LFH_SUBSEGMENT_STATS32[1]", 0x40, 4, 0),
    XX_PSW_FIELD("BucketStats.Buckets", "HEAP_LFH_SUBSEGMENT_STAT32[2]", 0x40, 4, 0),
    XX_PSW_FIELD("BucketStats.AllStats", "STORED_STATS32", 0x40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhcc_vs64_19045_extra[] = {
    XX_PSW_FIELD("DelayFreeContext", "HEAP_VS_DELAY_FREE_CONTEXT64[1]", 0x40, 16, 0),
    XX_PSW_FIELD("DelayFreeContext.ListHead", "SLIST_HEADER64[1]", 0x40, 16, 0),
    XX_PSW_FIELD("Callbacks", "HEAP_SUBALLOCATOR_CALLBACKS64_19045[1]", 0x88, 40, 0),
    XX_PSW_FIELD("Callbacks.Allocate", "uint64", 0x88, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Free", "uint64", 0x90, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Commit", "uint64", 0x98, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Decommit", "uint64", 0xA0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.ExtendContext", "uint64", 0xA8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config", "RTL_HP_VS_CONFIG64_19045[1]", 0xB0, 4, 0),
    XX_PSW_FIELD("Config.Flags", "uint32", 0xB0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config.Flags.PageAlignLargeAllocs", "bitfield:0:1", 0xB0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config.Flags.FullDecommit", "bitfield:1:1", 0xB0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config.Flags.EnableDelayFree", "bitfield:2:1", 0xB0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhcc_lfh64_19045_extra[] = {
    XX_PSW_FIELD("Callbacks", "HEAP_SUBALLOCATOR_CALLBACKS64_19045[1]", 0x8, 40, 0),
    XX_PSW_FIELD("Callbacks.Allocate", "uint64", 0x8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Free", "uint64", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Commit", "uint64", 0x18, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Decommit", "uint64", 0x20, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.ExtendContext", "uint64", 0x28, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config", "RTL_HP_LFH_CONFIG64_19045[1]", 0x3C, 4, 0),
    XX_PSW_FIELD("Config.MaxBlockSize", "uint16", 0x3C, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("Config.FlagsStorage", "uint16", 0x3E, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config.WitholdPageCrossingBlocks", "bitfield:0:1", 0x3E, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config.DisableRandomization", "bitfield:1:1", 0x3E, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("BucketStats", "HEAP_LFH_SUBSEGMENT_STATS64[1]", 0x40, 8, 0),
    XX_PSW_FIELD("BucketStats.Buckets", "HEAP_LFH_SUBSEGMENT_STAT64[4]", 0x40, 8, 0),
    XX_PSW_FIELD("BucketStats.AllStats", "STORED_STATS64", 0x40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhcc_vs64_26100_extra[] = {
    XX_PSW_FIELD("DelayFreeContext", "HEAP_VS_DELAY_FREE_CONTEXT64[1]", 0x40, 16, 0),
    XX_PSW_FIELD("DelayFreeContext.ListHead", "SLIST_HEADER64[1]", 0x40, 16, 0),
    XX_PSW_FIELD("Callbacks", "HEAP_SUBALLOCATOR_CALLBACKS64_26100[1]", 0x88, 48, 0),
    XX_PSW_FIELD("Callbacks.Allocate", "uint64", 0x88, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Free", "uint64", 0x90, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Commit", "uint64", 0x98, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Decommit", "uint64", 0xA0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.ExtendContext", "uint64", 0xA8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.TlsCleanup", "uint64", 0xB0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config", "RTL_HP_VS_CONFIG64_26100[1]", 0xB8, 4, 0),
    XX_PSW_FIELD("Config.Flags", "uint32", 0xB8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config.Flags.PageAlignLargeAllocs", "bitfield:0:1", 0xB8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config.Flags.EnableDelayFree", "bitfield:1:1", 0xB8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
};

static const xx_data_struct_field_desc xx_pswhcc_lfh64_26100_extra[] = {
    XX_PSW_FIELD("Callbacks", "HEAP_SUBALLOCATOR_CALLBACKS64_26100[1]", 0x8, 48, 0),
    XX_PSW_FIELD("Callbacks.Allocate", "uint64", 0x8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Free", "uint64", 0x10, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Commit", "uint64", 0x18, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.Decommit", "uint64", 0x20, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.ExtendContext", "uint64", 0x28, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Callbacks.TlsCleanup", "uint64", 0x30, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config", "HEAP_LFH_CONFIG64_26100[1]", 0x44, 8, 0),
    XX_PSW_FIELD("Config.Global", "RTL_HP_LFH_CONFIG64_26100[1]", 0x44, 4, 0),
    XX_PSW_FIELD("Config.Global.MaxBlockSize", "uint16", 0x44, 2, XX_PSW_SIZE),
    XX_PSW_FIELD("Config.Global.FlagsStorage", "uint16", 0x46, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config.Global.WitholdPageCrossingBlocks", "bitfield:0:1", 0x46, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config.Global.AllFields", "uint32", 0x44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config.FlagsStorage", "uint32", 0x48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("Config.EnablePrivateSlots", "bitfield:0:1", 0x48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("MetadataList", "SINGLE_LIST_ENTRY64[4]", 0x88, 32, 0),
    XX_PSW_FIELD("MetadataList[0]", "SINGLE_LIST_ENTRY64[1]", 0x88, 8, 0),
    XX_PSW_FIELD("MetadataList[0].Next", "STORED_LINK64", 0x88, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("MetadataList[1]", "SINGLE_LIST_ENTRY64[1]", 0x90, 8, 0),
    XX_PSW_FIELD("MetadataList[1].Next", "STORED_LINK64", 0x90, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("MetadataList[2]", "SINGLE_LIST_ENTRY64[1]", 0x98, 8, 0),
    XX_PSW_FIELD("MetadataList[2].Next", "STORED_LINK64", 0x98, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("MetadataList[3]", "SINGLE_LIST_ENTRY64[1]", 0xA0, 8, 0),
    XX_PSW_FIELD("MetadataList[3].Next", "STORED_LINK64", 0xA0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_PSW_FIELD("HeatMap", "HEAP_LFH_HEAT_MAP64[1]", 0xC0, 256, 0),
    XX_PSW_FIELD("HeatMap.Counts", "uint16[126]", 0xC0, 252, 0),
    XX_PSW_FIELD("HeatMap.LastDecayPeriod", "uint32", 0x1BC, 4, 0),
    XX_PSW_FIELD("SlotMaps", "HEAP_LFH_SLOT_MAP64[1]", 0x5C0, 256, 0),
    XX_PSW_FIELD("SlotMaps[0]", "HEAP_LFH_SLOT_MAP64[1]", 0x5C0, 256, 0),
    XX_PSW_FIELD("SlotMaps[0].Map", "uint16[128]", 0x5C0, 256, 0),
};

static bool xx_pswhcc_one(xx_ps_context *context, xx_io_device *device, uint64_t heap_address, unsigned width, const xx_psw_nt_layout *selected, const wchar_t *member,
                          const char *name, const xx_data_struct_field_desc *extra, size_t extra_count)
{
    const xx_data_struct_field_desc *parent = xx_pswr_field(selected, member);
    xx_data_struct_field_desc *fields;
    uint64_t address;
    size_t prefix = wcslen(member), capacity, count = 0;
    bool result;
    if (!parent || parent->rel_offset < 0 || parent->size <= 0 || parent->size > 65536 || (uint64_t)parent->rel_offset > (uint64_t)selected->size ||
        (uint64_t)parent->size > (uint64_t)selected->size - (uint64_t)parent->rel_offset) {
        xx_psw_warn_once(context, "A selected heap context descriptor had an invalid extent.");
        return true;
    }
    if (!xx_pswr_member_address(selected, member, heap_address, width, &address) || !xx_pswr_span(address, (uint64_t)parent->size, width)) {
        xx_psw_warn_once(context, "An embedded heap context exceeded the target address range.");
        return true;
    }
    if (xx_pswr_seen(context, name, address, 0)) return true;
    if (!xx_pswr_room(context)) return false;
    if (selected->count > SIZE_MAX - extra_count || selected->count + extra_count > SIZE_MAX / sizeof(*fields)) {
        context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
        return false;
    }
    capacity = selected->count + extra_count;
    fields = (xx_data_struct_field_desc *)calloc(capacity, sizeof(*fields));
    if (!fields) {
        context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
        return false;
    }
    for (size_t i = 0; i < selected->count; ++i) {
        const xx_data_struct_field_desc *field = selected->fields + i;
        uint64_t relative;
        if (!field->name || wcsncmp(field->name, member, prefix) || field->name[prefix] != L'.') continue;
        if (field->rel_offset < parent->rel_offset || field->size < 0) goto invalid_descriptor;
        relative = (uint64_t)(field->rel_offset - parent->rel_offset);
        if (relative > (uint64_t)parent->size || (uint64_t)field->size > (uint64_t)parent->size - relative) goto invalid_descriptor;
        fields[count] = *field;
        fields[count].name = field->name + prefix + 1;
        fields[count].rel_offset -= parent->rel_offset;
        ++count;
    }
    for (size_t i = 0; i < extra_count; ++i) {
        const xx_data_struct_field_desc *field = extra + i;
        size_t duplicate;
        if (!field->name || field->rel_offset < 0 || field->size < 0 || (uint64_t)field->rel_offset > (uint64_t)parent->size ||
            (uint64_t)field->size > (uint64_t)parent->size - (uint64_t)field->rel_offset)
            goto invalid_descriptor;
        for (duplicate = 0; duplicate < count; ++duplicate)
            if (!wcscmp(fields[duplicate].name, field->name)) break;
        if (duplicate != count) continue; /* Existing parent metadata wins. */
        fields[count++] = *field;
    }
    if (!count) {
        free(fields);
        return true;
    }
    result = xx_psw_memory(context, device, name, selected->label, address, (size_t)parent->size, 0, fields, count) != NULL;
    free(fields);
    return result;

invalid_descriptor:
    free(fields);
    context->status = XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
    return false;
}

static void xx_pswhc_context_children(xx_ps_context *context, xx_io_device *device, uint64_t heap_address, unsigned width, const xx_psw_nt_layout *selected)
{
    const xx_data_struct_field_desc *vs_extra = NULL, *lfh_extra = NULL;
    size_t vs_count = 0, lfh_count = 0;
    if (!context || !context->snapshot || !device || !selected || context->status != XX_PROCESS_STRUCTS_OK || (width != 4 && width != 8)) return;
    if (width == 4 && selected->fields == xx_pswh_segment32_19045) {
        vs_extra = xx_pswhcc_vs32_19045_extra;
        vs_count = XX_PSW_COUNT(xx_pswhcc_vs32_19045_extra);
        lfh_extra = xx_pswhcc_lfh32_19045_extra;
        lfh_count = XX_PSW_COUNT(xx_pswhcc_lfh32_19045_extra);
    } else if (width == 8 && selected->fields == xx_pswh_segment64_19045) {
        vs_extra = xx_pswhcc_vs64_19045_extra;
        vs_count = XX_PSW_COUNT(xx_pswhcc_vs64_19045_extra);
        lfh_extra = xx_pswhcc_lfh64_19045_extra;
        lfh_count = XX_PSW_COUNT(xx_pswhcc_lfh64_19045_extra);
    } else if (width == 8 && selected->fields == xx_pswh_segment64_26100) {
        vs_extra = xx_pswhcc_vs64_26100_extra;
        vs_count = XX_PSW_COUNT(xx_pswhcc_vs64_26100_extra);
        lfh_extra = xx_pswhcc_lfh64_26100_extra;
        lfh_count = XX_PSW_COUNT(xx_pswhcc_lfh64_26100_extra);
    } else if (width != 4 || (selected->fields != xx_pswh_wow_segment32_19045 && selected->fields != xx_pswh_wow_segment32_26100)) {
        return; /* Prefix fallbacks and unrelated heap families have no child view. */
    }
    if (!xx_pswhcc_one(context, device, heap_address, width, selected, L"VsContext", width == 8 ? "HEAP_VS_CONTEXT64" : "HEAP_VS_CONTEXT32", vs_extra, vs_count)) return;
    (void)xx_pswhcc_one(context, device, heap_address, width, selected, L"LfhContext", width == 8 ? "HEAP_LFH_CONTEXT64" : "HEAP_LFH_CONTEXT32", lfh_extra, lfh_count);
}

#endif /* XX_PROCESS_HEAP_CONTEXT_CHILDREN_H */
