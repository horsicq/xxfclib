/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_PROCESS_HEAP_LEGACY_CHILDREN_H
#define XX_PROCESS_HEAP_LEGACY_CHILDREN_H

/* Standalone views derived from the selected complete legacy HEAP profile.
 * Include after the native/WOW64 full profiles and the xx_pswr_* helpers.
 * Prefix-only classification schemas deliberately have no usable Segment
 * parent and are not expanded. No host sizeof/offsetof determines target ABI.
 *
 * Native symbol facts and their primary symbol-derived source:
 * https://www.vergiliusproject.com/kernels/x86/windows-10/22h2/_HEAP_SEGMENT
 * https://www.vergiliusproject.com/kernels/x64/windows-10/22h2/_HEAP_SEGMENT
 * https://www.vergiliusproject.com/kernels/x64/windows-11/24h2/_HEAP_SEGMENT
 * The exact source/proof for all Entry overlays and WOW64 NTDLL PDB identities
 * is retained by xx_process_heap_layouts.h/xx_process_wow64_heap_layouts.h.
 * HEAP_SEGMENT is 0x40/0x70 bytes; SegmentListEntry is its 0x10/0x18 member.
 * The linked segment's FFEEFFEE marker and Heap owner are checked before and
 * after reading its children. Entries remain opaque stored bytes; Encoding
 * is decoding-key storage. No encoded Size is used and no allocations, free
 * lists, UCR lists, LFH internals or LastValidEntry boundary are walked.
 */

static bool xx_pswhlc_equal(const unsigned char *left, const unsigned char *right, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        if (left[i] != right[i]) return false;
    return true;
}

static void xx_pswhlc_note(xx_ps_context *context, size_t first, const char *message)
{
    for (size_t i = first; i < context->snapshot->structure_count; ++i) {
        xx_process_structure *item = context->snapshot->structures + i;
        for (size_t j = 0; j < item->field_count; ++j) (void)snprintf(item->fields[j].comment, sizeof(item->fields[j].comment), "%s", message);
    }
}

/* Snapshot storage may move on every add; only indices survive child adds. */
static void xx_pswhlc_clear(xx_process_structure *item, const char *message)
{
    for (size_t j = 0; j < item->field_count; ++j) {
        xx_process_struct_field *field = item->fields + j;
        field->available = false;
        (void)snprintf(field->raw, sizeof(field->raw), "<unavailable>");
        (void)snprintf(field->value, sizeof(field->value), "<unavailable>");
        (void)snprintf(field->comment, sizeof(field->comment), "%s", message);
    }
}

static void xx_pswhlc_discard(xx_ps_context *context, size_t first, const char *message)
{
    for (size_t i = first; i < context->snapshot->structure_count; ++i) xx_pswhlc_clear(context->snapshot->structures + i, message);
}

static bool xx_pswhlc_entry(xx_ps_context *context, xx_io_device *device, const xx_psw_nt_layout *selected, const wchar_t *member, uint64_t address, unsigned width,
                            const char *source)
{
    size_t first = context->snapshot->structure_count;
    bool result = xx_pswr_embedded(context, device, selected, member, width == 8 ? "HEAP_ENTRY64" : "HEAP_ENTRY32", address, width, 0);
    xx_pswhlc_note(context, first, source);
    return result;
}

/* Project static descriptor literals, not target bytes. Native HEAP publishes
 * the segment's union members at the top level; the PDB-derived WOW64 schema
 * additionally publishes Segment.*. Select exactly one representation. */
static bool xx_pswhlc_segment_schema(xx_ps_context *context, const xx_psw_nt_layout *selected, unsigned width, xx_psw_nt_layout *segment,
                                     xx_data_struct_field_desc **owned)
{
    const xx_data_struct_field_desc *parent = xx_pswr_field(selected, L"Segment");
    const xx_data_struct_field_desc *entry = xx_pswr_field(selected, L"Entry");
    const xx_data_struct_field_desc *encoding = xx_pswr_field(selected, L"Encoding");
    bool prefixed = xx_pswr_field(selected, L"Segment.Entry") != NULL;
    size_t count = 0;
    *owned = NULL;
    if (!selected || (width != 4 && width != 8) || !parent || parent->rel_offset != 0 || parent->size != (width == 8 ? 0x70 : 0x40) || !entry || entry->rel_offset != 0 ||
        entry->size != (int64_t)(2u * width) || !encoding || encoding->rel_offset < parent->size || encoding->size != entry->size ||
        selected->size <= (size_t)parent->size)
        return false;
    if (!xx_pswr_room(context)) return false;
    if (selected->count > SIZE_MAX / sizeof(**owned)) {
        context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
        return false;
    }
    *owned = (xx_data_struct_field_desc *)calloc(selected->count, sizeof(**owned));
    if (!*owned) {
        context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
        return false;
    }
    for (size_t i = 0; i < selected->count; ++i) {
        const xx_data_struct_field_desc *field = selected->fields + i;
        if (!field->name || field == parent) continue;
        if (prefixed) {
            if (wcsncmp(field->name, L"Segment.", 8)) continue;
        } else if (field->rel_offset < 0 || field->size <= 0 || (uint64_t)field->rel_offset >= (uint64_t)parent->size ||
                   (uint64_t)field->size > (uint64_t)parent->size - (uint64_t)field->rel_offset)
            continue;
        if (field->rel_offset < 0 || field->size <= 0 || (uint64_t)field->rel_offset > (uint64_t)parent->size ||
            (uint64_t)field->size > (uint64_t)parent->size - (uint64_t)field->rel_offset) {
            context->status = XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
            free(*owned);
            *owned = NULL;
            return false;
        }
        (*owned)[count] = *field;
        if (prefixed) (*owned)[count].name = field->name + 8;
        ++count;
    }
    *segment = *selected;
    segment->fields = *owned;
    segment->count = count;
    segment->size = (size_t)parent->size;
    if (!count || !xx_pswr_field(segment, L"SegmentSignature") || !xx_pswr_field(segment, L"Heap") || !xx_pswr_field(segment, L"SegmentListEntry")) {
        free(*owned);
        *owned = NULL;
        return false;
    }
    return true;
}

static bool xx_pswhlc_value(const xx_psw_nt_layout *segment, const unsigned char *bytes, const wchar_t *name, unsigned size, uint64_t *value)
{
    const xx_data_struct_field_desc *field = xx_pswr_field(segment, name);
    if (!field || field->rel_offset < 0 || field->size != (int64_t)size || (size != 4 && size != 8) || (uint64_t)field->rel_offset > segment->size ||
        size > segment->size - (size_t)field->rel_offset)
        return false;
    *value = xx_psw_le(bytes + (size_t)field->rel_offset, size);
    return true;
}

static bool xx_pswhlc_first_entry(xx_ps_context *context, xx_io_device *device, const xx_psw_nt_layout *selected, const xx_psw_nt_layout *segment, uint64_t heap,
                                  uint64_t address, unsigned width, const unsigned char *bytes, uint64_t page_size)
{
    uint64_t first, last, base, pages, extent, end;
    unsigned char probe[16];
    size_t entry_size = 2u * width;
    size_t header_size = address == heap ? selected->size : segment->size;
    if (!xx_pswhlc_value(segment, bytes, L"FirstEntry", width, &first) || !xx_pswhlc_value(segment, bytes, L"LastValidEntry", width, &last) ||
        !xx_pswhlc_value(segment, bytes, L"BaseAddress", width, &base) || !xx_pswhlc_value(segment, bytes, L"NumberOfPages", 4, &pages))
        return true;
    if (!first) return true;
    if (!page_size || !pages || pages > UINT64_MAX / page_size) goto invalid;
    extent = pages * page_size;
    if (!xx_pswr_span(base, extent, width) || extent > UINT64_MAX - base) goto invalid;
    end = base + extent;
    if (base > address || header_size > UINT64_MAX - address || first < address + header_size || first < base || (first & (entry_size - 1u)) || first >= last ||
        last > end || entry_size > last - first || !xx_pswr_span(first, entry_size, width))
        goto invalid;
    if (!xx_ps_read(device, first, probe, entry_size)) {
        xx_psw_warn_once(context, "A legacy segment FirstEntry was unreadable; its stored entry was skipped.");
        return true;
    }
    return xx_pswhlc_entry(context, device, selected, L"Entry", first, width,
                           "Segment.FirstEntry: stored opaque/possibly encoded HEAP_ENTRY bytes; not decrypted, sized or followed.");
invalid:
    xx_psw_warn_once(context, "A legacy segment FirstEntry was outside its validated segment bounds and was not followed.");
    return true;
}

/* Return false on an invalid/changing segment or a collection failure. The
 * caller may stop a changing list without trusting the next pointer. */
static bool xx_pswhlc_segment(xx_ps_context *context, xx_io_device *device, const xx_psw_nt_layout *selected, const xx_psw_nt_layout *segment, uint64_t heap,
                              uint64_t address, unsigned width, uint64_t page_size, bool list_member, uint64_t expected_next, uint64_t expected_previous)
{
    unsigned char before[0x70], after[0x70];
    uint64_t marker, owner, next, previous;
    size_t first = context->snapshot->structure_count;
    xx_process_structure *item;
    const char *name = width == 8 ? "HEAP_SEGMENT64" : "HEAP_SEGMENT32";
    bool children;
    if ((address & (width - 1u)) || !xx_pswr_span(address, segment->size, width) || !xx_ps_read(device, address, before, segment->size) ||
        !xx_pswhlc_value(segment, before, L"SegmentSignature", 4, &marker) || marker != UINT64_C(0xffeeffee) ||
        !xx_pswhlc_value(segment, before, L"Heap", width, &owner) || owner != heap ||
        (list_member && (!xx_pswhlc_value(segment, before, L"SegmentListEntry.Flink", width, &next) ||
                         !xx_pswhlc_value(segment, before, L"SegmentListEntry.Blink", width, &previous) || next != expected_next || previous != expected_previous))) {
        xx_psw_warn_once(context, "A legacy heap segment was unreadable or its marker, owner or list links were invalid.");
        return false;
    }
    if (xx_pswr_seen(context, name, address, 0)) return true;
    if (!xx_pswr_room(context)) return false;
    item = xx_psw_memory(context, device, name, selected->label, address, segment->size, 0, segment->fields, segment->count);
    if (!item) return false;
    if (item->field_count)
        (void)snprintf(item->fields[0].comment, sizeof(item->fields[0].comment), "%s; complete HEAP_SEGMENT view; Entry retains opaque stored header bytes.",
                       list_member ? "Heap.SegmentList member" : "Heap.Segment embedded overlay");
    children = xx_pswhlc_entry(context, device, selected, L"Entry", address, width,
                               "Segment.Entry: stored opaque/possibly encoded segment header bytes; plaintext allocation fields are not inferred.");
    if (children) children = xx_pswhlc_first_entry(context, device, selected, segment, heap, address, width, before, page_size);
    if (!xx_ps_read(device, address, after, segment->size) || !xx_pswhlc_equal(before, after, segment->size)) {
        const char *entry_name = width == 8 ? "HEAP_ENTRY64" : "HEAP_ENTRY32";
        xx_pswhlc_discard(context, first, "Segment header changed or became unreadable during inspection; segment and entry fields were discarded.");
        /* Heap.Entry may already exist before this checkpoint and deduplicate
         * the segment overlay. Invalidate that same physical entry as well. */
        for (size_t i = 0; i < first; ++i) {
            xx_process_structure *prior = context->snapshot->structures + i;
            if (prior->has_address && prior->address == address && !strcmp(prior->name, entry_name))
                xx_pswhlc_clear(prior, "Segment header changed during inspection; this stored entry snapshot was discarded.");
        }
        xx_psw_warn_once(context, "A legacy heap segment changed during inspection; its child snapshot was discarded.");
        return false;
    }
    return children && context->status == XX_PROCESS_STRUCTS_OK;
}

static bool xx_pswhlc_visit(xx_ps_context *context, uint64_t **visited, size_t *count, size_t *capacity, uint64_t link)
{
    if (*count == *capacity) {
        const size_t maximum = SIZE_MAX / sizeof(**visited);
        size_t grown = *capacity ? (*capacity > maximum / 2 ? maximum : *capacity * 2) : 16;
        uint64_t *items;
        if (grown > maximum) grown = maximum;
        if (grown <= *capacity) {
            context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
            return false;
        }
        items = (uint64_t *)calloc(grown, sizeof(*items));
        if (!items) {
            context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
            return false;
        }
        if (*count) memcpy(items, *visited, *count * sizeof(*items));
        free(*visited);
        *visited = items;
        *capacity = grown;
    }
    (*visited)[(*count)++] = link;
    return true;
}

static void xx_pswhc_legacy_children(xx_ps_context *context, xx_io_device *device, uint64_t heap_address, unsigned width, const xx_psw_nt_layout *selected)
{
    xx_data_struct_field_desc *owned = NULL;
    xx_psw_nt_layout segment;
    const xx_data_struct_field_desc *link_field;
    SYSTEM_INFO system;
    uint64_t child, head, link, tail, previous, next, backward;
    uint64_t *visited = NULL;
    size_t count = 0, capacity = 0, link_offset;
    unsigned char initial_head[16], final_head[16], links[16];
    bool ended = false;
    if (!context || !device || context->status != XX_PROCESS_STRUCTS_OK || !xx_pswhlc_segment_schema(context, selected, width, &segment, &owned)) return;
    if (!xx_pswr_span(heap_address, selected->size, width)) goto done;
    if (xx_pswr_member_address(selected, L"Entry", heap_address, width, &child) &&
        !xx_pswhlc_entry(context, device, selected, L"Entry", child, width,
                         "Heap.Entry: stored opaque/possibly encoded header bytes; plaintext allocation fields are not inferred."))
        goto done;
    if (xx_pswr_member_address(selected, L"Encoding", heap_address, width, &child) &&
        !xx_pswhlc_entry(context, device, selected, L"Encoding", child, width,
                         "Heap.Encoding: allocator decoding-key storage in a HEAP_ENTRY overlay; not a decoded allocation header."))
        goto done;
    memset(&system, 0, sizeof(system));
    GetNativeSystemInfo(&system); /* Same Windows kernel as the inspected target. */
    if (!xx_pswhlc_segment(context, device, selected, &segment, heap_address, heap_address, width, system.dwPageSize, false, 0, 0)) goto done;
    if (!xx_pswr_member_address(selected, L"SegmentList", heap_address, width, &head) || !xx_pswr_span(head, 2u * width, width) ||
        !xx_ps_read(device, head, initial_head, 2u * width)) {
        xx_psw_warn_once(context, "The legacy heap segment-list head was unreadable.");
        goto done;
    }
    link_field = xx_pswr_field(&segment, L"SegmentListEntry");
    if (!link_field || link_field->rel_offset < 0 || link_field->size != (int64_t)(2u * width)) goto done;
    link_offset = (size_t)link_field->rel_offset;
    link = xx_psw_le(initial_head, width);
    tail = xx_psw_le(initial_head + width, width);
    previous = head;
    while (link != head && link && context->status == XX_PROCESS_STRUCTS_OK) {
        size_t i;
        for (i = 0; i < count; ++i)
            if (visited[i] == link) break;
        if (i != count || link < link_offset || (link & (width - 1u)) || !xx_pswr_span(link, 2u * width, width)) {
            xx_psw_warn_once(context, "The legacy heap segment list contained a cycle or an invalid link.");
            break;
        }
        if (!xx_ps_read(device, link, links, 2u * width)) {
            xx_psw_warn_once(context, "The legacy heap segment list contained an unreadable link.");
            break;
        }
        next = xx_psw_le(links, width);
        backward = xx_psw_le(links + width, width);
        if (backward != previous) {
            xx_psw_warn_once(context, "The legacy heap segment list changed or had an inconsistent backlink.");
            break;
        }
        if (!xx_pswr_seen(context, width == 8 ? "HEAP_SEGMENT64" : "HEAP_SEGMENT32", link - link_offset, 0) && !xx_pswr_room(context)) break;
        if (!xx_pswhlc_visit(context, &visited, &count, &capacity, link) ||
            !xx_pswhlc_segment(context, device, selected, &segment, heap_address, link - link_offset, width, system.dwPageSize, true, next, previous))
            break;
        previous = link;
        link = next;
    }
    ended = link == head;
    if (!link) xx_psw_warn_once(context, "The legacy heap segment list ended at a null link.");
    if (!xx_ps_read(device, head, final_head, 2u * width) || !xx_pswhlc_equal(initial_head, final_head, 2u * width) || (ended && previous != tail))
        xx_psw_warn_once(context, "The legacy heap segment-list head changed during inspection; enumeration is best effort.");
done:
    free(visited);
    free(owned);
}

#endif /* XX_PROCESS_HEAP_LEGACY_CHILDREN_H */
