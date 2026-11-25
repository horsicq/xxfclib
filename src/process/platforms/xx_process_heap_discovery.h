/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_PROCESS_HEAP_DISCOVERY_H
#define XX_PROCESS_HEAP_DISCOVERY_H

/* Include after the heap layout selectors and xx_process_related_layouts.h.
 * Private heaps are inspected through the existing read-only process device.
 * Enumeration follows PEB.ProcessHeap and the initial PEB.ProcessHeaps array;
 * it does not invoke target heap functions or walk individual allocation blocks.
 * Verified parent schemas also provide separate embedded contexts, segment
 * headers, stored entries and reachable suballocator headers through the
 * child helpers included before it.
 *
 * Full WOW64 layouts come from the exact guest PDB profiles included before
 * this header, selected using the loaded NTDLL's PE RSDS identity. The short
 * prefixes below remain fallback schemas for unmatched guest modules.
 * Signature values and these explicitly limited fallback prefixes are supported by:
 * https://raw.githubusercontent.com/winsiderss/systeminformer/master/SystemInformer/include/heapstruct.h
 * https://raw.githubusercontent.com/winsiderss/systeminformer/master/SystemInformer/memprv.c
 * Prefix facts were independently expressed as target-offset descriptors.
 * HEAP.Signature (EEFFEEFF) differs from HEAP.SegmentSignature (FFEEFFEE).
 */

static const xx_data_struct_field_desc xx_pswhd_wow_heap_prefix[] = {XX_PSW_FIELD("Entry", "HEAP_ENTRY32[1]", 0, 8, 0),
                                                                     XX_PSW_FIELD("SegmentSignature", "ULONG", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIGNATURE),
                                                                     XX_PSW_FIELD("SegmentFlags", "ULONG", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                                     XX_PSW_FIELD("SegmentListEntry.Flink", "PLIST_ENTRY32", 16, 4, XX_PSW_PTR),
                                                                     XX_PSW_FIELD("SegmentListEntry.Blink", "PLIST_ENTRY32", 20, 4, XX_PSW_PTR),
                                                                     XX_PSW_FIELD("Heap", "PHEAP32", 24, 4, XX_PSW_PTR),
                                                                     XX_PSW_FIELD("BaseAddress", "PVOID32", 28, 4, XX_PSW_PTR),
                                                                     XX_PSW_FIELD("NumberOfPages", "ULONG", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
                                                                     XX_PSW_FIELD("FirstEntry", "PHEAP_ENTRY32", 36, 4, XX_PSW_PTR),
                                                                     XX_PSW_FIELD("LastValidEntry", "PHEAP_ENTRY32", 40, 4, XX_PSW_PTR),
                                                                     XX_PSW_FIELD("NumberOfUnCommittedPages", "ULONG", 44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
                                                                     XX_PSW_FIELD("NumberOfUnCommittedRanges", "ULONG", 48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
                                                                     XX_PSW_FIELD("SegmentAllocatorBackTraceIndex", "USHORT", 52, 2, XX_PSW_ID),
                                                                     XX_PSW_FIELD("Reserved", "USHORT", 54, 2, XX_PSW_RESERVED),
                                                                     XX_PSW_FIELD("UCRSegmentList.Flink", "PLIST_ENTRY32", 56, 4, XX_PSW_PTR),
                                                                     XX_PSW_FIELD("UCRSegmentList.Blink", "PLIST_ENTRY32", 60, 4, XX_PSW_PTR),
                                                                     XX_PSW_FIELD("Flags", "ULONG", 64, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                                     XX_PSW_FIELD("ForceFlags", "ULONG", 68, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                                     XX_PSW_FIELD("CompatibilityFlags", "ULONG", 72, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                                     XX_PSW_FIELD("EncodeFlagMask", "ULONG", 76, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                                     XX_PSW_FIELD("Encoding", "HEAP_ENTRY32[1]", 80, 8, 0),
                                                                     XX_PSW_FIELD("Interceptor", "ULONG", 88, 4, 0),
                                                                     XX_PSW_FIELD("VirtualMemoryThreshold", "ULONG", 92, 4, 0),
                                                                     XX_PSW_FIELD("Signature", "ULONG", 96, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIGNATURE)};
static const xx_data_struct_field_desc xx_pswhd_wow_segment_prefix[] = {XX_PSW_FIELD("ReservedPrefix", "BYTE[8]", 0, 8, XX_PSW_RESERVED),
                                                                        XX_PSW_FIELD("Signature", "ULONG", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIGNATURE),
                                                                        XX_PSW_FIELD("GlobalFlags", "ULONG", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)};
static const xx_psw_nt_layout xx_pswhd_wow_heap = {
    0, 0x014c, "WOW64 HEAP32 classification prefix; full private tail unavailable", xx_pswhd_wow_heap_prefix, XX_PSW_COUNT(xx_pswhd_wow_heap_prefix), 0x64};
static const xx_psw_nt_layout xx_pswhd_wow_segment = {
    0, 0x014c, "WOW64 SEGMENT_HEAP32 classification prefix; full private tail unavailable", xx_pswhd_wow_segment_prefix, XX_PSW_COUNT(xx_pswhd_wow_segment_prefix), 0x10};

static bool xx_pswhd_room(xx_ps_context *context)
{
    if (context->status != XX_PROCESS_STRUCTS_OK) return false;
    if (!xx_psw_full(context)) return true;
    context->snapshot->truncated = true;
    xx_psw_warn_once(context, "The structure limit was reached during heap discovery.");
    return false;
}

static bool xx_pswhd_read(xx_io_device *device, const xx_psw_nt_layout *profile, uint64_t parent, unsigned width, const wchar_t *name, unsigned size, uint64_t *value)
{
    const xx_data_struct_field_desc *field = xx_pswr_field(profile, name);
    uint64_t address;
    return field && field->size == (int64_t)size && (size == 4 || size == 8) && xx_pswr_member_address(profile, name, parent, width, &address) &&
           xx_psw_pointer(device, address, 0, size, value);
}

static bool xx_pswhd_seen(const xx_ps_context *context, uint64_t address, unsigned width)
{
    const char *heap = width == 8 ? "HEAP64" : "HEAP32";
    const char *segment = width == 8 ? "SEGMENT_HEAP64" : "SEGMENT_HEAP32";
    const char *header = width == 8 ? "HEAP_HEADER64" : "HEAP_HEADER32";
    for (size_t i = 0; i < context->snapshot->structure_count; ++i) {
        const xx_process_structure *item = context->snapshot->structures + i;
        if (item->has_address && item->address == address && (!strcmp(item->name, heap) || !strcmp(item->name, segment) || !strcmp(item->name, header))) return true;
    }
    return false;
}

static void xx_pswhd_unknown(xx_ps_context *context, xx_io_device *device, uint64_t address, unsigned width, const xx_psw_nt_layout *heap,
                             const xx_psw_nt_layout *segment, const char *source)
{
    static const wchar_t *const names[] = {L"LegacySegmentSignature", L"LegacyHeapSignature", L"SegmentHeapSignature"};
    const xx_data_struct_field_desc *probes[3] = {xx_pswr_field(heap, L"SegmentSignature"), xx_pswr_field(heap, L"Signature"), xx_pswr_field(segment, L"Signature")};
    xx_data_struct_field_desc fields[3];
    xx_process_structure *structure;
    size_t count = 0, extent = 0;
    for (size_t i = 0; i < XX_PSW_COUNT(probes); ++i) {
        const xx_data_struct_field_desc *probe = probes[i];
        if (!probe || probe->rel_offset < 0 || probe->size != 4) continue;
        fields[count] = *probe;
        fields[count].name = names[i];
        if ((uint64_t)probe->rel_offset + 4 > extent) extent = (size_t)probe->rel_offset + 4;
        ++count;
    }
    if (!count || !xx_pswr_span(address, extent, width)) return;
    structure = xx_psw_memory(context, device, width == 8 ? "HEAP_HEADER64" : "HEAP_HEADER32",
                              "Unclassified heap: signature probes only; private fields were not decoded", address, extent, 0, fields, count);
    if (structure && structure->field_count)
        (void)snprintf(structure->fields[0].comment, sizeof(structure->fields[0].comment),
                       "%s; signatures are unknown, inconsistent or unreadable. No heap layout was inferred.", source);
    xx_psw_warn_once(context, "Some heaps had unknown or unreadable signatures; only signature probes are shown.");
}

static void xx_pswhd_invalidate(xx_ps_context *context, size_t first, size_t last, const char *reason)
{
    if (last > context->snapshot->structure_count) last = context->snapshot->structure_count;
    for (size_t i = first; i < last; ++i) {
        xx_process_structure *structure = context->snapshot->structures + i;
        for (size_t j = 0; j < structure->field_count; ++j) {
            xx_process_struct_field *field = structure->fields + j;
            field->available = false;
            (void)snprintf(field->raw, sizeof(field->raw), "<unavailable>");
            (void)snprintf(field->value, sizeof(field->value), "<unavailable>");
            (void)snprintf(field->comment, sizeof(field->comment), "%s", reason);
        }
    }
}

static bool xx_pswhd_identity(xx_io_device *device, const xx_psw_nt_layout *selected, uint64_t address, unsigned width, bool legacy)
{
    uint64_t signature = 0, marker = 0;
    return xx_pswhd_read(device, selected, address, width, L"Signature", 4, &signature) && signature == (legacy ? UINT64_C(0xeeffeeff) : UINT64_C(0xddeeddee)) &&
           (!legacy || (xx_pswhd_read(device, selected, address, width, L"SegmentSignature", 4, &marker) && marker == UINT64_C(0xffeeffee)));
}

/* The result remains partial when a heap is destroyed or replaced while read.
 * Verify both legacy markers, rather than confusing the segment marker with
 * HEAP.Signature. No structure pointer survives another xx_ps_add call. */
static bool xx_pswhd_one(xx_ps_context *context, xx_io_device *device, uint64_t address, unsigned width, const xx_psw_nt_layout *heap, const xx_psw_nt_layout *segment,
                         const char *source)
{
    uint64_t signature = 0, marker = 0, segment_signature = 0;
    const xx_psw_nt_layout *selected = NULL;
    xx_process_structure *structure;
    size_t parent_index;
    bool legacy = false, consistent;
    if (!address || (address & (width - 1u)) || !xx_pswr_span(address, 1, width)) {
        xx_psw_warn_once(context, "An invalid heap pointer was not followed.");
        return true;
    }
    if (xx_pswhd_seen(context, address, width)) return true;
    if (!xx_pswhd_room(context)) return false;
    if (xx_pswhd_read(device, heap, address, width, L"Signature", 4, &signature) && signature == UINT64_C(0xeeffeeff) &&
        xx_pswhd_read(device, heap, address, width, L"SegmentSignature", 4, &marker) && marker == UINT64_C(0xffeeffee)) {
        selected = heap;
        legacy = true;
    }
    if (xx_pswhd_read(device, segment, address, width, L"Signature", 4, &segment_signature) && segment_signature == UINT64_C(0xddeeddee)) selected = segment;
    if (!selected) {
        xx_pswhd_unknown(context, device, address, width, heap, segment, source);
        return context->status == XX_PROCESS_STRUCTS_OK;
    }
    legacy = selected == heap;
    if (!xx_pswr_span(address, selected->size, width)) {
        xx_psw_warn_once(context, "A heap extent exceeded the target address width; private fields were skipped.");
        xx_pswhd_unknown(context, device, address, width, heap, segment, source);
        return context->status == XX_PROCESS_STRUCTS_OK;
    }
    parent_index = context->snapshot->structure_count;
    structure = xx_psw_memory(context, device, legacy ? (width == 8 ? "HEAP64" : "HEAP32") : (width == 8 ? "SEGMENT_HEAP64" : "SEGMENT_HEAP32"), selected->label, address,
                              selected->size, 0, selected->fields, selected->count);
    if (!structure) return false;
    consistent = xx_pswhd_identity(device, selected, address, width, legacy);
    for (size_t i = 0; i < structure->field_count; ++i) {
        xx_process_struct_field *field = structure->fields + i;
        if (!consistent) {
            field->available = false;
            (void)snprintf(field->raw, sizeof(field->raw), "<unavailable>");
            (void)snprintf(field->value, sizeof(field->value), "<unavailable>");
            (void)snprintf(field->comment, sizeof(field->comment), "Heap signature changed or became unreadable; field snapshot was discarded.");
        } else if (field->available && !wcscmp(field->desc.name, L"Signature"))
            (void)snprintf(field->comment, sizeof(field->comment), "%s; signature matched before and after this best-effort snapshot.", source);
    }
    if (!consistent) xx_psw_warn_once(context, "A heap signature changed during inspection; its decoded fields were discarded.");
    if (selected == &xx_pswhd_wow_heap || selected == &xx_pswhd_wow_segment)
        xx_psw_warn_once(context, "WOW64 heaps show verified classification prefixes; full private heap tails are unavailable.");
    else if (consistent) {
        /* Child additions can move snapshot->structures. Use the saved index,
         * never the parent pointer, after any child helper adds a record. */
        if (legacy) xx_pswhc_legacy_children(context, device, address, width, selected);
        else {
            xx_pswhc_context_children(context, device, address, width, selected);
            xx_pswhsa_children(context, device, address, width, selected);
            xx_pswhvc_children(context, device, address, width, selected);
        }
        if (context->status == XX_PROCESS_STRUCTS_OK && !xx_pswhd_identity(device, selected, address, width, legacy)) {
            xx_pswhd_invalidate(context, parent_index, context->snapshot->structure_count,
                                "Heap identity changed during child discovery; parent and child field snapshots were discarded.");
            xx_psw_warn_once(context, "A heap changed during child discovery; its parent and child decoded fields were discarded.");
        }
    }
    return context->status == XX_PROCESS_STRUCTS_OK;
}

static bool xx_pswhd_array_metadata(xx_io_device *device, const xx_psw_nt_layout *peb, uint64_t address, unsigned width, uint64_t *count, uint64_t *maximum,
                                    uint64_t *table)
{
    return xx_pswhd_read(device, peb, address, width, L"NumberOfHeaps", 4, count) && xx_pswhd_read(device, peb, address, width, L"MaximumNumberOfHeaps", 4, maximum) &&
           xx_pswhd_read(device, peb, address, width, L"ProcessHeaps", width, table);
}

static void xx_pswh_collect(xx_ps_context *context, xx_io_device *device, uint64_t address, unsigned width, const xx_psw_target *target)
{
    const xx_psw_nt_layout *peb = xx_psw_profile(target, width, false);
    const xx_psw_nt_layout *heap = xx_pswh_heap_profile(target, width);
    const xx_psw_nt_layout *segment = xx_pswh_segment_profile(target, width);
    xx_psw_target heap_target = *target;
    uint64_t primary = 0, count = 0, maximum = 0, table = 0, observed = 0;
    uint64_t after_count = 0, after_maximum = 0, after_table = 0;
    uint64_t *visited = NULL;
    size_t used = 0, capacity = 0;
    if (!peb || context->status != XX_PROCESS_STRUCTS_OK) return;
    if (width == 4 && target->native_machine == 0x8664 && target->process_machine == 0x014c) {
        const xx_psw_nt_layout *wow_heap, *wow_segment;
        xx_pswh_identify_wow_module(context, device, &heap_target);
        wow_heap = xx_pswh_wow_heap_profile(&heap_target, width);
        wow_segment = xx_pswh_wow_segment_profile(&heap_target, width);
        if (!heap) heap = wow_heap ? wow_heap : &xx_pswhd_wow_heap;
        if (!segment) segment = wow_segment ? wow_segment : &xx_pswhd_wow_segment;
    }
    if (!heap && !segment) {
        xx_psw_warn_once(context, "No verified heap layout matched this build/architecture; private heap fields were skipped.");
        return;
    }
    if (xx_pswhd_read(device, peb, address, width, L"ProcessHeap", width, &primary) && primary &&
        !xx_pswhd_one(context, device, primary, width, heap, segment, "PEB.ProcessHeap"))
        return;
    if (!xx_pswhd_array_metadata(device, peb, address, width, &count, &maximum, &table)) {
        xx_psw_warn_once(context, "The process heap-array metadata was unavailable.");
        return;
    }
    if (!count) return;
    if (count > maximum || !table || (table & (width - 1u)) || maximum > UINT64_MAX / width || !xx_pswr_span(table, maximum * width, width)) {
        xx_psw_warn_once(context, "The process heap-array count, capacity or address was inconsistent; it was not followed.");
        return;
    }
    for (uint64_t i = 0; i < count && context->status == XX_PROCESS_STRUCTS_OK; ++i) {
        char source[96];
        size_t j;
        if (!xx_psw_pointer(device, table, i * width, width, &observed)) {
            xx_psw_warn_once(context, "The process heap array became unreadable; remaining entries were skipped.");
            break;
        }
        if (!observed || (observed & (width - 1u))) {
            xx_psw_warn_once(context, "The process heap array contained an invalid pointer; remaining entries were skipped.");
            break;
        }
        for (j = 0; j < used; ++j)
            if (visited[j] == observed) break;
        if (j != used) {
            xx_psw_warn_once(context, "The process heap array contained a duplicate pointer; remaining entries were skipped.");
            break;
        }
        if (used == capacity) {
            size_t limit = SIZE_MAX / sizeof(*visited);
            size_t grown = capacity ? (capacity > limit / 2 ? limit : capacity * 2) : 32;
            uint64_t *items;
            if (grown > count) grown = (size_t)count;
            if (grown > limit) grown = limit;
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
        visited[used++] = observed;
        (void)snprintf(source, sizeof(source), "PEB.ProcessHeaps[%llu]", (unsigned long long)i);
        if (!xx_pswhd_one(context, device, observed, width, heap, segment, source)) break;
    }
    free(visited);
    if (context->status == XX_PROCESS_STRUCTS_OK && (!xx_pswhd_array_metadata(device, peb, address, width, &after_count, &after_maximum, &after_table) ||
                                                     count != after_count || maximum != after_maximum || table != after_table))
        xx_psw_warn_once(context, "The process heap-array metadata changed during inspection; heap discovery is partial.");
}

#endif /* XX_PROCESS_HEAP_DISCOVERY_H */
