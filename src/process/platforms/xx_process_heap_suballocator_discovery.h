/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_PROCESS_HEAP_SUBALLOCATOR_DISCOVERY_H
#define XX_PROCESS_HEAP_SUBALLOCATOR_DISCOVERY_H

/* Read-only discovery from a previously validated complete SEGMENT_HEAP.
 * Include after related helpers and both native/WOW suballocator schemas.
 * Selectors accept exact parent arrays, preserving their architecture and
 * loaded-WOW-NTDLL identity gates. No layout is inferred from pointer width.
 *
 * Primary reachability evidence: Mark Vincent Yason, IBM X-Force,
 * Windows 10 Segment Heap Internals (2016), sections 2.3/2.4:
 * https://www.blackhat.com/docs/us-16/materials/us-16-Yason-Windows-10-Segment-Heap-Internals-wp.pdf
 * VS SubsegmentList links ListEntry; LFH bucket bit0 means an in-place
 * ONDEMAND usage counter, not an address to mask and dereference. Activated
 * old buckets provide AffinitySlots, bounded by State.SlotCount; owner
 * Available/Full lists link LFH subsegment ListEntry and old Owner is a
 * backlink. Current exact symbol/PDB schemas corroborate the stored links.
 * Modern ordinary owner lists remain usable; matching stored BucketRef is
 * an additional guard, not a decoded pointer. Private slot/ref graphs and
 * ActiveSubsegment FAST_REF stay opaque. Their active-only objects may be
 * omitted, and this limitation is reported. Variable block/bitmap payloads,
 * encoded allocation headers and callback targets are never followed.
 *
 * Traversal checks target spans, list backlinks, owner identity and visible
 * changes. Dynamic visited sets have no fixed record-count limit. Only
 * snapshot indices survive adds because xx_ps_add may move its allocation.
 * The caller also rechecks the enclosing heap marker after all child reads.
 */

/* Every selected fixed object header is <= 0x68 bytes. This scratch bound
 * limits a single metadata read, never the number of discovered objects. */
#define XX_PSWHSD_CAPTURE_MAX 128u

static bool xx_pswhsd_equal(const unsigned char *a, const unsigned char *b, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

static void xx_pswhsd_clear(xx_process_structure *item, const char *reason)
{
    for (size_t j = 0; j < item->field_count; ++j) {
        xx_process_struct_field *field = item->fields + j;
        field->available = false;
        (void)snprintf(field->raw, sizeof(field->raw), "<unavailable>");
        (void)snprintf(field->value, sizeof(field->value), "<unavailable>");
        (void)snprintf(field->comment, sizeof(field->comment), "%s", reason);
    }
}

static void xx_pswhsd_discard(xx_ps_context *context, size_t first, const char *reason)
{
    for (size_t i = first; i < context->snapshot->structure_count; ++i) xx_pswhsd_clear(context->snapshot->structures + i, reason);
}

static void xx_pswhsd_discard_one(xx_ps_context *context, const char *name, uint64_t address, const char *reason)
{
    for (size_t i = 0; i < context->snapshot->structure_count; ++i) {
        xx_process_structure *item = context->snapshot->structures + i;
        if (item->has_address && item->address == address && !strcmp(item->name, name)) xx_pswhsd_clear(item, reason);
    }
}

/* Read a scalar/bit member directly from an exact captured target header. */
static bool xx_pswhsd_value(const xx_psw_nt_layout *layout, const unsigned char *bytes, const wchar_t *name, uint64_t *value)
{
    const xx_data_struct_field_desc *field = xx_pswr_field(layout, name);
    const wchar_t *type;
    unsigned shift = 0, bits = 0;
    if (!field || !bytes || field->rel_offset < 0 || field->size <= 0 || field->size > 8 || (uint64_t)field->rel_offset > layout->size ||
        (uint64_t)field->size > layout->size - (size_t)field->rel_offset)
        return false;
    *value = xx_psw_le(bytes + (size_t)field->rel_offset, (size_t)field->size);
    type = field->type;
    if (!type || wcsncmp(type, L"bitfield:", 9)) return true;
    type += 9;
    if (*type < L'0' || *type > L'9') return false;
    while (*type >= L'0' && *type <= L'9') {
        if (shift > 64) return false;
        shift = shift * 10u + (unsigned)(*type++ - L'0');
    }
    if (*type++ != L':' || *type < L'0' || *type > L'9') return false;
    while (*type >= L'0' && *type <= L'9') {
        if (bits > 64) return false;
        bits = bits * 10u + (unsigned)(*type++ - L'0');
    }
    if (*type || !bits || bits > (unsigned)field->size * 8u || shift > (unsigned)field->size * 8u - bits) return false;
    *value >>= shift;
    if (bits < 64) *value &= (UINT64_C(1) << bits) - 1;
    return true;
}

static bool xx_pswhsd_capture(xx_io_device *device, const xx_psw_nt_layout *layout, uint64_t address, unsigned width, unsigned char *bytes)
{
    return layout && layout->size && layout->size <= XX_PSWHSD_CAPTURE_MAX && !(address & (width - 1u)) && xx_pswr_span(address, layout->size, width) &&
           xx_ps_read(device, address, bytes, layout->size);
}

static bool xx_pswhsd_add(xx_ps_context *context, xx_io_device *device, const xx_psw_nt_layout *layout, const char *name, uint64_t address, unsigned width)
{
    if (xx_pswr_seen(context, name, address, 0)) return true;
    if (!layout || !layout->size || layout->size > XX_PSWHSD_CAPTURE_MAX || !xx_pswr_span(address, layout->size, width)) {
        xx_psw_warn_once(context, "A heap suballocator header exceeded its verified target layout.");
        return true;
    }
    if (!xx_pswr_room(context)) return false;
    return xx_psw_memory(context, device, name, layout->label, address, layout->size, 0, layout->fields, layout->count) != NULL;
}

static bool xx_pswhsd_unreadable(xx_ps_context *context, xx_io_device *device, const xx_psw_nt_layout *layout, const char *name, uint64_t address, unsigned width)
{
    bool result = xx_pswhsd_add(context, device, layout, name, address, width);
    xx_pswhsd_discard_one(context, name, address, "The reachable fixed header was unreadable; its fields were not trusted and its links were not followed.");
    xx_psw_warn_once(context, "A reachable heap suballocator header was unreadable.");
    return result;
}

/* Always run this after child adds, including an explicit-limit failure. */
static bool xx_pswhsd_post(xx_ps_context *context, xx_io_device *device, const xx_psw_nt_layout *layout, const char *name, uint64_t address, unsigned width,
                           const unsigned char *before, size_t first)
{
    unsigned char after[XX_PSWHSD_CAPTURE_MAX];
    if (xx_pswhsd_capture(device, layout, address, width, after) && xx_pswhsd_equal(before, after, layout->size)) return true;
    xx_pswhsd_discard(context, first, "The suballocator header changed during inspection; this object and its new child fields were discarded.");
    xx_pswhsd_discard_one(context, name, address, "The suballocator header changed during inspection; this object snapshot was discarded.");
    xx_psw_warn_once(context, "A heap suballocator header changed during inspection; its child snapshot was discarded.");
    return false;
}

/* Returns 1 for a new address, 0 for a duplicate, -1 for allocation failure. */
static int xx_pswhsd_visit(xx_ps_context *context, uint64_t **items, size_t *count, size_t *capacity, uint64_t address)
{
    for (size_t i = 0; i < *count; ++i)
        if ((*items)[i] == address) return 0;
    if (*count == *capacity) {
        const size_t maximum = SIZE_MAX / sizeof(**items);
        size_t grown = *capacity ? (*capacity > maximum / 2 ? maximum : *capacity * 2) : 16;
        uint64_t *next;
        if (grown > maximum) grown = maximum;
        if (grown <= *capacity) {
            context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
            return -1;
        }
        next = (uint64_t *)calloc(grown, sizeof(*next));
        if (!next) {
            context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
            return -1;
        }
        if (*count) memcpy(next, *items, *count * sizeof(*next));
        free(*items);
        *items = next;
        *capacity = grown;
    }
    (*items)[(*count)++] = address;
    return 1;
}

/* A normal LIST_ENTRY head and fixed record ListEntry only. VS records have
 * no Owner member; their exact parent's embedded context anchors the list.
 * Old LFH records require the direct Owner backlink. Modern refs are left
 * encoded: normal list membership plus equal stored BucketRef is checked. */
static bool xx_pswhsd_list(xx_ps_context *context, xx_io_device *device, uint64_t head, unsigned width, const xx_psw_nt_layout *layout, const char *name, uint64_t owner,
                           bool modern_owner, uint64_t bucket_ref)
{
    const xx_data_struct_field_desc *entry = xx_pswr_field(layout, L"ListEntry");
    unsigned char initial[16], final[16], bytes[XX_PSWHSD_CAPTURE_MAX];
    uint64_t next, previous, link, tail, value, address;
    uint64_t *visited = NULL;
    size_t count = 0, capacity = 0, first = context->snapshot->structure_count;
    bool keep = true, ended = false;
    if (!entry || entry->rel_offset < 0 || entry->size != (int64_t)(2u * width) || (uint64_t)entry->rel_offset > layout->size ||
        (uint64_t)entry->size > layout->size - (size_t)entry->rel_offset || !xx_pswr_span(head, 2u * width, width) || (head & (width - 1u)) ||
        !xx_ps_read(device, head, initial, 2u * width)) {
        xx_psw_warn_once(context, "A heap suballocator list head was unreadable or invalid.");
        return true;
    }
    link = xx_psw_le(initial, width);
    tail = xx_psw_le(initial + width, width);
    previous = head;
    while (link && link != head && context->status == XX_PROCESS_STRUCTS_OK) {
        size_t object_first;
        int seen;
        if (link < (uint64_t)entry->rel_offset || (link & (width - 1u))) goto invalid_link;
        address = link - (uint64_t)entry->rel_offset;
        if (!xx_pswr_span(address, layout->size, width)) goto invalid_link;
        if (!xx_pswr_seen(context, name, address, 0) && !xx_pswr_room(context)) {
            keep = false;
            break;
        }
        seen = xx_pswhsd_visit(context, &visited, &count, &capacity, address);
        if (seen < 0) {
            keep = false;
            break;
        }
        if (!seen) goto invalid_link;
        if (!xx_pswhsd_capture(device, layout, address, width, bytes)) {
            keep = xx_pswhsd_unreadable(context, device, layout, name, address, width);
            break;
        }
        if (!xx_pswhsd_value(layout, bytes, L"ListEntry.Flink", &next) || !xx_pswhsd_value(layout, bytes, L"ListEntry.Blink", &value) || value != previous || !next)
            goto invalid_link;
        if (owner && ((!modern_owner && (!xx_pswhsd_value(layout, bytes, L"Owner", &value) || value != owner)) ||
                      (modern_owner && (!xx_pswhsd_value(layout, bytes, L"BucketRef", &value) || value != bucket_ref)))) {
            xx_psw_warn_once(context, "A heap LFH subsegment did not match its list owner; its links were not followed.");
            break;
        }
        object_first = context->snapshot->structure_count;
        keep = xx_pswhsd_add(context, device, layout, name, address, width);
        if (!xx_pswhsd_post(context, device, layout, name, address, width, bytes, object_first)) break;
        if (!keep) break;
        previous = link;
        link = next;
    }
    ended = link == head;
    if (!link) xx_psw_warn_once(context, "A heap suballocator list ended at a null link.");
    if (!xx_ps_read(device, head, final, 2u * width) || !xx_pswhsd_equal(initial, final, 2u * width) || (ended && previous != tail)) {
        xx_pswhsd_discard(context, first, "The owning suballocator list changed during inspection; its new member snapshots were discarded.");
        xx_psw_warn_once(context, "A heap suballocator list changed during inspection.");
    }
    free(visited);
    return keep && context->status == XX_PROCESS_STRUCTS_OK;
invalid_link:
    xx_psw_warn_once(context, "A heap suballocator list contained a cycle, invalid link or inconsistent backlink.");
    /* Even malformed/changing lists retain a final head check. */
    if (!xx_ps_read(device, head, final, 2u * width) || !xx_pswhsd_equal(initial, final, 2u * width))
        xx_pswhsd_discard(context, first, "The owning list changed; its new member snapshots were discarded.");
    free(visited);
    return context->status == XX_PROCESS_STRUCTS_OK;
}

static bool xx_pswhsd_owner_identity(const xx_psw_nt_layout *layout, const unsigned char *bytes, uint64_t bucket_index, bool bucket, uint64_t slot_index)
{
    uint64_t value;
    if (!xx_pswhsd_value(layout, bytes, L"IsBucket", &value) || value != (bucket ? 1u : 0u) || !xx_pswhsd_value(layout, bytes, L"BucketIndex", &value) ||
        value != bucket_index)
        return false;
    if (!bucket && xx_pswr_field(layout, L"SlotIndex") && (!xx_pswhsd_value(layout, bytes, L"SlotIndex", &value) || value != slot_index)) return false;
    return true;
}

static bool xx_pswhsd_owner(xx_ps_context *context, xx_io_device *device, uint64_t address, unsigned width, const xx_psw_nt_layout *layout,
                            const xx_psw_nt_layout *subsegment, uint64_t bucket_index, bool bucket, uint64_t slot_index)
{
    unsigned char bytes[XX_PSWHSD_CAPTURE_MAX];
    uint64_t head, bucket_ref = 0;
    size_t first = context->snapshot->structure_count;
    const char *name = width == 8 ? "HEAP_LFH_SUBSEGMENT_OWNER64" : "HEAP_LFH_SUBSEGMENT_OWNER32";
    bool modern = xx_pswr_field(subsegment, L"Owner") == NULL, keep;
    if (!xx_pswhsd_capture(device, layout, address, width, bytes)) return xx_pswhsd_unreadable(context, device, layout, name, address, width);
    if (!xx_pswhsd_owner_identity(layout, bytes, bucket_index, bucket, slot_index) || (modern && !xx_pswhsd_value(layout, bytes, L"BucketRef", &bucket_ref))) {
        xx_psw_warn_once(context, "An LFH owner did not match its bucket/slot identity.");
        return true;
    }
    keep = xx_pswhsd_add(context, device, layout, name, address, width);
    if (keep && xx_pswr_member_address(layout, L"AvailableSubsegmentList", address, width, &head))
        keep = xx_pswhsd_list(context, device, head, width, subsegment, width == 8 ? "HEAP_LFH_SUBSEGMENT64" : "HEAP_LFH_SUBSEGMENT32", address, modern, bucket_ref);
    if (keep && xx_pswr_member_address(layout, L"FullSubsegmentList", address, width, &head))
        keep = xx_pswhsd_list(context, device, head, width, subsegment, width == 8 ? "HEAP_LFH_SUBSEGMENT64" : "HEAP_LFH_SUBSEGMENT32", address, modern, bucket_ref);
    (void)xx_pswhsd_post(context, device, layout, name, address, width, bytes, first);
    return keep && context->status == XX_PROCESS_STRUCTS_OK;
}

static bool xx_pswhsd_slot(xx_ps_context *context, xx_io_device *device, uint64_t address, unsigned width, const xx_psw_nt_layout *layout, const xx_psw_nt_layout *owner,
                           const xx_psw_nt_layout *subsegment, uint64_t bucket_index, uint64_t slot_index)
{
    unsigned char bytes[XX_PSWHSD_CAPTURE_MAX];
    uint64_t state_address, value;
    const xx_data_struct_field_desc *state = xx_pswr_field(layout, L"State");
    const char *name = width == 8 ? "HEAP_LFH_AFFINITY_SLOT64" : "HEAP_LFH_AFFINITY_SLOT32";
    size_t first = context->snapshot->structure_count;
    bool keep;
    if (!state || state->size != (int64_t)owner->size || !xx_pswr_member_address(layout, L"State", address, width, &state_address)) return true;
    if (!xx_pswhsd_capture(device, layout, address, width, bytes)) return xx_pswhsd_unreadable(context, device, layout, name, address, width);
    if (!xx_pswhsd_owner_identity(owner, bytes + (size_t)state->rel_offset, bucket_index, false, slot_index)) {
        xx_psw_warn_once(context, "An LFH affinity slot did not match its bucket or array index.");
        return true;
    }
    keep = xx_pswhsd_add(context, device, layout, name, address, width);
    if (keep) keep = xx_pswhsd_owner(context, device, state_address, width, owner, subsegment, bucket_index, false, slot_index);
    if (xx_pswhsd_value(layout, bytes, L"ActiveSubsegment.Value", &value) && value)
        xx_psw_warn_once(context, "LFH ActiveSubsegment fast references remain opaque; active-only subsegments may be omitted.");
    (void)xx_pswhsd_post(context, device, layout, name, address, width, bytes, first);
    return keep && context->status == XX_PROCESS_STRUCTS_OK;
}

static bool xx_pswhsd_bucket(xx_ps_context *context, xx_io_device *device, uint64_t address, unsigned width, const xx_psw_nt_layout *layout, const xx_psw_nt_layout *slot,
                             const xx_psw_nt_layout *owner, const xx_psw_nt_layout *subsegment, uint64_t bucket_index)
{
    unsigned char bytes[XX_PSWHSD_CAPTURE_MAX], pointer_bytes[8], pointer_after[8];
    const xx_data_struct_field_desc *state = xx_pswr_field(layout, L"State");
    const char *name = width == 8 ? "HEAP_LFH_BUCKET64" : "HEAP_LFH_BUCKET32";
    uint64_t state_address, table = 0, slots = 0, slot_address, cell;
    uint64_t *visited = NULL;
    size_t count = 0, capacity = 0, first = context->snapshot->structure_count;
    bool keep;
    if (!state || state->size != (int64_t)owner->size || !xx_pswr_member_address(layout, L"State", address, width, &state_address)) return true;
    if (!xx_pswhsd_capture(device, layout, address, width, bytes)) return xx_pswhsd_unreadable(context, device, layout, name, address, width);
    if (!xx_pswhsd_owner_identity(owner, bytes + (size_t)state->rel_offset, bucket_index, true, 0)) {
        xx_psw_warn_once(context, "An activated LFH bucket did not match its table index.");
        return true;
    }
    keep = xx_pswhsd_add(context, device, layout, name, address, width);
    if (keep) keep = xx_pswhsd_owner(context, device, state_address, width, owner, subsegment, bucket_index, true, 0);
    if (!xx_pswr_field(layout, L"AffinitySlots")) {
        xx_psw_warn_once(context, "Modern LFH private-slot references remain opaque; only ordinary bucket-owned subsegment lists were followed.");
        goto post;
    }
    if (!keep) goto post;
    if (!xx_pswhsd_value(layout, bytes, L"AffinitySlots", &table) || !xx_pswhsd_value(layout, bytes, L"State.SlotCount", &slots) ||
        (slots && (!table || slots > UINT64_MAX / width || !xx_pswr_span(table, slots * width, width) || (table & (width - 1u))))) {
        xx_psw_warn_once(context, "An LFH affinity-slot table had invalid count or target bounds.");
        goto post;
    }
    for (uint64_t i = 0; i < slots && context->status == XX_PROCESS_STRUCTS_OK; ++i) {
        int seen;
        size_t child_first;
        cell = table + i * width; /* The full checked table span includes this cell. */
        if (!xx_ps_read(device, cell, pointer_bytes, width)) {
            xx_psw_warn_once(context, "An LFH affinity-slot pointer was unreadable.");
            break;
        }
        slot_address = xx_psw_le(pointer_bytes, width);
        if (!slot_address || (slot_address & (width - 1u)) || !xx_pswr_span(slot_address, slot->size, width)) {
            xx_psw_warn_once(context, "An LFH affinity-slot pointer was null or outside the target ABI.");
            break;
        }
        if (!xx_pswr_seen(context, width == 8 ? "HEAP_LFH_AFFINITY_SLOT64" : "HEAP_LFH_AFFINITY_SLOT32", slot_address, 0) && !xx_pswr_room(context)) {
            keep = false;
            break;
        }
        seen = xx_pswhsd_visit(context, &visited, &count, &capacity, slot_address);
        if (seen < 0) {
            keep = false;
            break;
        }
        if (!seen) {
            xx_psw_warn_once(context, "An LFH affinity-slot table contained a duplicate pointer.");
            break;
        }
        child_first = context->snapshot->structure_count;
        keep = xx_pswhsd_slot(context, device, slot_address, width, slot, owner, subsegment, bucket_index, i);
        if (!xx_ps_read(device, cell, pointer_after, width) || !xx_pswhsd_equal(pointer_bytes, pointer_after, width)) {
            xx_pswhsd_discard(context, child_first, "The owning affinity-slot pointer changed; its new child fields were discarded.");
            xx_psw_warn_once(context, "An LFH affinity-slot pointer changed during inspection.");
            break;
        }
        if (!keep) break;
    }
post:
    free(visited);
    (void)xx_pswhsd_post(context, device, layout, name, address, width, bytes, first);
    return keep && context->status == XX_PROCESS_STRUCTS_OK;
}

static void xx_pswhsa_children(xx_ps_context *context, xx_io_device *device, uint64_t heap_address, unsigned width, const xx_psw_nt_layout *selected)
{
    const xx_psw_nt_layout *vs, *subsegment, *bucket, *slot, *owner;
    const xx_data_struct_field_desc *buckets;
    uint64_t backend = 0, check, head, table, max_affinity = 0, max_address = 0;
    uint64_t *visited = NULL;
    size_t count = 0, capacity = 0, first;
    unsigned char pointer_bytes[8], pointer_after[8], scalar[8];
    bool keep = true, have_backend, have_max_affinity = false;
    if (!context || !context->snapshot || !device || !selected || context->status != XX_PROCESS_STRUCTS_OK || (width != 4 && width != 8)) return;
    vs = xx_pswhsa_native_vs(selected, width);
    subsegment = xx_pswhsa_native_lfh_subsegment(selected, width);
    bucket = xx_pswhsa_native_lfh_bucket(selected, width);
    slot = xx_pswhsa_native_lfh_slot(selected, width);
    owner = xx_pswhsa_native_lfh_owner(selected, width);
    if (!vs) {
        vs = xx_pswhsa_wow_vs(selected, width);
        subsegment = xx_pswhsa_wow_lfh_subsegment(selected, width);
        bucket = xx_pswhsa_wow_lfh_bucket(selected, width);
        slot = xx_pswhsa_wow_lfh_slot(selected, width);
        owner = xx_pswhsa_wow_lfh_owner(selected, width);
    }
    if (!vs || !subsegment || !bucket || !slot || !owner) return;
    first = context->snapshot->structure_count;
    /* The embedded offset is established by the complete exact parent.
     * BackendCtx is callback-specific; old publications name HeapBase, but
     * later implementations may pass a different backend context address. */
    have_backend = xx_pswr_member_pointer(device, selected, L"VsContext.BackendCtx", heap_address, width, &backend);
    if (xx_pswr_member_address(selected, L"VsContext.SubsegmentList", heap_address, width, &head)) {
        keep = xx_pswhsd_list(context, device, head, width, vs, width == 8 ? "HEAP_VS_SUBSEGMENT64" : "HEAP_VS_SUBSEGMENT32", 0, false, 0);
        if (have_backend && (!xx_pswr_member_pointer(device, selected, L"VsContext.BackendCtx", heap_address, width, &check) || check != backend)) {
            xx_pswhsd_discard(context, first, "The VS context owner changed; its new subsegment fields were discarded.");
            xx_psw_warn_once(context, "The VS callback backend context changed during inspection.");
        }
    } else xx_psw_warn_once(context, "The verified embedded VS subsegment-list address was unavailable.");
    if (!keep || context->status != XX_PROCESS_STRUCTS_OK) return;
    first = context->snapshot->structure_count;
    have_backend = xx_pswr_member_pointer(device, selected, L"LfhContext.BackendCtx", heap_address, width, &backend);
    buckets = xx_pswr_field(selected, L"LfhContext.Buckets");
    if (!buckets || buckets->size <= 0 || (uint64_t)buckets->size % width || !xx_pswr_member_address(selected, L"LfhContext.Buckets", heap_address, width, &table))
        return;
    if (xx_pswr_member_address(selected, L"LfhContext.MaxAffinity", heap_address, width, &max_address) && xx_ps_read(device, max_address, scalar, 1)) {
        max_affinity = scalar[0];
        have_max_affinity = true;
    }
    for (uint64_t i = 0; i < (uint64_t)buckets->size / width && context->status == XX_PROCESS_STRUCTS_OK; ++i) {
        uint64_t cell = table + i * width, address;
        size_t child_first;
        int seen;
        if (!xx_ps_read(device, cell, pointer_bytes, width)) {
            xx_psw_warn_once(context, "An LFH bucket table entry was unreadable.");
            break;
        }
        address = xx_psw_le(pointer_bytes, width);
        if (!address || (address & 1u)) continue; /* ONDEMAND counter: never mask into a pointer. */
        if ((address & (width - 1u)) || !xx_pswr_span(address, bucket->size, width)) {
            xx_psw_warn_once(context, "An LFH bucket table entry was outside the target ABI.");
            continue;
        }
        if (!xx_pswr_seen(context, width == 8 ? "HEAP_LFH_BUCKET64" : "HEAP_LFH_BUCKET32", address, 0) && !xx_pswr_room(context)) break;
        seen = xx_pswhsd_visit(context, &visited, &count, &capacity, address);
        if (seen < 0) break;
        if (!seen) {
            xx_psw_warn_once(context, "The LFH bucket table contained duplicate activated bucket pointers.");
            continue;
        }
        child_first = context->snapshot->structure_count;
        keep = xx_pswhsd_bucket(context, device, address, width, bucket, slot, owner, subsegment, i);
        if (!xx_ps_read(device, cell, pointer_after, width) || !xx_pswhsd_equal(pointer_bytes, pointer_after, width)) {
            xx_pswhsd_discard(context, child_first, "The owning LFH bucket pointer changed; its new child fields were discarded.");
            xx_psw_warn_once(context, "An LFH bucket table entry changed during inspection.");
        }
        if (!keep) break;
    }
    free(visited);
    if (have_backend && (!xx_pswr_member_pointer(device, selected, L"LfhContext.BackendCtx", heap_address, width, &check) || check != backend)) {
        xx_pswhsd_discard(context, first, "The LFH context owner changed; its new child fields were discarded.");
        xx_psw_warn_once(context, "The LFH callback backend context changed during inspection.");
    }
    if (have_max_affinity && (!xx_ps_read(device, max_address, scalar, 1) || scalar[0] != max_affinity)) {
        xx_pswhsd_discard(context, first, "The LFH MaxAffinity metadata changed; its new child fields were discarded.");
        xx_psw_warn_once(context, "The LFH MaxAffinity metadata changed during inspection.");
    }
}

#undef XX_PSWHSD_CAPTURE_MAX
#endif /* XX_PROCESS_HEAP_SUBALLOCATOR_DISCOVERY_H */
