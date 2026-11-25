/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_PROCESS_HEAP_VS_CHUNK_DISCOVERY_H
#define XX_PROCESS_HEAP_VS_CHUNK_DISCOVERY_H

/* Fixed stored VS chunk views reached through a verified unencoded free tree.
 * Include after native/WOW chunk schemas and suballocator discovery helpers.
 * The enclosing selected SEGMENT_HEAP retains its exact ABI/NTDLL gate.
 *
 * Primary reachability/layout evidence:
 * https://www.blackhat.com/docs/us-16/materials/us-16-Yason-Windows-10-Segment-Heap-Internals-wp.pdf
 * Sections 2.3 and 3.2 identify FreeChunkTree nodes as the embedded Node of
 * HEAP_VS_CHUNK_FREE_HEADER, distinct from its encoded Sizes storage.
 * Exact current native symbol/WOW PDB sources are recorded by the schemas.
 * https://raw.githubusercontent.com/microsoft/win32metadata/main/generation/WinSDK/RecompiledIdlHeaders/shared/ntdef.h
 * Microsoft's RTL_BALANCED_NODE_GET_PARENT_POINTER removes only reserved
 * ParentValue bits0..1. Left/Right and the tree Root are never tag-masked.
 * An RTL_RB_TREE with Encoded bit0 set is left opaque, rather than applying
 * a decoding formula verified only on a different allocator/build.
 *
 * Every reachable free node yields the complete fixed free header plus its
 * overlapping stored HEAP_VS_CHUNK_HEADER view. Encoded sizes, allocation
 * flags and page offsets are displayed as stored bits, not allocator values.
 * No encoded Size is used for a length/next address or allocation walk.
 * First/busy chunk placement is not inferred from sizeof(VS_SUBSEGMENT),
 * which does not establish alignment/padding for these concrete builds.
 * Dynamic traversal has no fixed node-count limit. Tree/node rereads detect
 * visible changes; only indices survive additions to the shared snapshot.
 */

#define XX_PSWHVD_HEADER_MAX 32u

typedef struct xx_pswhvd_node {
    uint64_t address; /* RTL_BALANCED_NODE address, not container */
    uint64_t parent;  /* Expected untagged parent node address */
    bool captured;
    unsigned char bytes[XX_PSWHVD_HEADER_MAX];
} xx_pswhvd_node;

/* Both pending and processed nodes remain in this checked vector, providing
 * cycle/shared-child detection and the final stable-header comparison. */
static bool xx_pswhvd_push(xx_ps_context *context, xx_pswhvd_node **nodes, size_t *count, size_t *capacity, uint64_t address, uint64_t parent, unsigned width,
                           const xx_psw_nt_layout *free_layout, size_t node_offset)
{
    uint64_t container;
    const char *name = width == 8 ? "HEAP_VS_CHUNK_FREE_HEADER64" : "HEAP_VS_CHUNK_FREE_HEADER32";
    if (!address) return true;
    if ((address & (width - 1u)) || address < node_offset) goto invalid;
    container = address - node_offset;
    if (!xx_pswr_span(container, free_layout->size, width)) goto invalid;
    for (size_t i = 0; i < *count; ++i)
        if ((*nodes)[i].address == address) {
            xx_psw_warn_once(context, "The VS free tree contained a cycle or a shared child node.");
            return false;
        }
    if (!xx_pswr_seen(context, name, container, 0) && !xx_pswr_room(context)) return false;
    if (*count == *capacity) {
        const size_t maximum = SIZE_MAX / sizeof(**nodes);
        size_t grown = *capacity ? (*capacity > maximum / 2 ? maximum : *capacity * 2) : 16;
        xx_pswhvd_node *next;
        if (grown > maximum) grown = maximum;
        if (grown <= *capacity) {
            context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
            return false;
        }
        next = (xx_pswhvd_node *)calloc(grown, sizeof(*next));
        if (!next) {
            context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
            return false;
        }
        if (*count) memcpy(next, *nodes, *count * sizeof(*next));
        free(*nodes);
        *nodes = next;
        *capacity = grown;
    }
    (*nodes)[*count].address = address;
    (*nodes)[*count].parent = parent;
    (*nodes)[*count].captured = false;
    ++*count;
    return true;
invalid:
    xx_psw_warn_once(context, "A VS free-tree node/container address was invalid for the target ABI.");
    return false;
}

static bool xx_pswhvd_add(xx_ps_context *context, xx_io_device *device, const xx_psw_nt_layout *layout, const char *name, uint64_t address, unsigned width,
                          bool free_view)
{
    size_t first = context->snapshot->structure_count;
    bool result = xx_pswhsd_add(context, device, layout, name, address, width);
    for (size_t i = first; i < context->snapshot->structure_count; ++i) {
        xx_process_structure *item = context->snapshot->structures + i;
        for (size_t j = 0; j < item->field_count; ++j) {
            xx_process_struct_field *field = item->fields + j;
            const wchar_t *member = field->desc.name;
            if (!free_view || (member && (!wcsncmp(member, L"Header", 6) || !wcscmp(member, L"OverlapsHeader"))))
                (void)snprintf(field->comment, sizeof(field->comment),
                               "Stored encoded/overlaid chunk bits; allocation sizes/flags/page offsets are not decoded or inferred.");
        }
    }
    return result;
}

static void xx_pswhvc_children(xx_ps_context *context, xx_io_device *device, uint64_t heap_address, unsigned width, const xx_psw_nt_layout *selected)
{
    const xx_psw_nt_layout *header_layout, *free_layout;
    const xx_data_struct_field_desc *tree_field, *root_field, *encoded_field, *node_field, *header_field;
    unsigned char tree_before[16], tree_after[16], after[XX_PSWHVD_HEADER_MAX];
    xx_pswhvd_node *nodes = NULL;
    uint64_t tree_address, root, left, right, parent_value, container;
    uint64_t backend = 0, check;
    size_t count = 0, capacity = 0, node_offset, first;
    const char *free_name, *header_name;
    bool changed = false, keep = true, have_backend;
    if (!context || !context->snapshot || !device || !selected || context->status != XX_PROCESS_STRUCTS_OK || (width != 4 && width != 8)) return;
    header_layout = xx_pswhvc_native_header(selected, width);
    free_layout = xx_pswhvc_native_free(selected, width);
    if (!header_layout) {
        header_layout = xx_pswhvc_wow_header(selected, width);
        free_layout = xx_pswhvc_wow_free(selected, width);
    }
    if (!header_layout || !free_layout) return;
    node_field = xx_pswr_field(free_layout, L"Node");
    header_field = xx_pswr_field(free_layout, L"Header");
    if (!node_field || !header_field || node_field->rel_offset < 0 || node_field->size != (int64_t)(3u * width) || header_field->rel_offset != 0 ||
        header_field->size != (int64_t)header_layout->size || !header_layout->size || header_layout->size > free_layout->size || !free_layout->size ||
        free_layout->size > XX_PSWHVD_HEADER_MAX || (uint64_t)node_field->rel_offset > free_layout->size ||
        (uint64_t)node_field->size > free_layout->size - (size_t)node_field->rel_offset) {
        xx_psw_warn_once(context, "A selected VS free-chunk layout had an invalid fixed extent.");
        return;
    }
    node_offset = (size_t)node_field->rel_offset;
    tree_field = xx_pswr_field(selected, L"VsContext.FreeChunkTree");
    root_field = xx_pswr_field(selected, L"VsContext.FreeChunkTree.Root");
    encoded_field = xx_pswr_field(selected, L"VsContext.FreeChunkTree.Encoded");
    if (!tree_field || !root_field || !encoded_field || tree_field->rel_offset < 0 || tree_field->rel_offset > INT64_MAX - (int64_t)width ||
        tree_field->size != (int64_t)(2u * width) || root_field->rel_offset != tree_field->rel_offset || root_field->size != (int64_t)width ||
        encoded_field->rel_offset != tree_field->rel_offset + (int64_t)width || encoded_field->size <= 0 || encoded_field->size > (int64_t)width ||
        !xx_pswr_member_address(selected, L"VsContext.FreeChunkTree", heap_address, width, &tree_address) || !xx_ps_read(device, tree_address, tree_before, 2u * width)) {
        xx_psw_warn_once(context, "The verified VS free-tree root/encoding state was unavailable.");
        return;
    }
    if (tree_before[width] & 1u) {
        xx_psw_warn_once(context, "The VS free tree is encoded; chunk pointers were not decoded or followed.");
        return;
    }
    xx_psw_warn_once(context, "VS chunk views cover free-tree members only; first/busy chunk geometry and encoded allocation sizes are not inferred.");
    root = xx_psw_le(tree_before, width);
    if (!root) {
        if (xx_psw_le(tree_before + width, width)) xx_psw_warn_once(context, "The VS free tree had a null root with a non-null minimum.");
        return;
    }
    first = context->snapshot->structure_count;
    have_backend = xx_pswr_member_pointer(device, selected, L"VsContext.BackendCtx", heap_address, width, &backend);
    free_name = width == 8 ? "HEAP_VS_CHUNK_FREE_HEADER64" : "HEAP_VS_CHUNK_FREE_HEADER32";
    header_name = width == 8 ? "HEAP_VS_CHUNK_HEADER64" : "HEAP_VS_CHUNK_HEADER32";
    if (!xx_pswhvd_push(context, &nodes, &count, &capacity, root, 0, width, free_layout, node_offset)) goto post;
    for (size_t index = 0; index < count && context->status == XX_PROCESS_STRUCTS_OK; ++index) {
        size_t object_first;
        uint64_t node_address = nodes[index].address, expected_parent = nodes[index].parent;
        container = node_address - node_offset;
        if (!xx_pswhsd_capture(device, free_layout, container, width, nodes[index].bytes)) {
            keep = xx_pswhsd_unreadable(context, device, free_layout, free_name, container, width);
            if (keep) {
                keep = xx_pswhvd_add(context, device, header_layout, header_name, container, width, false);
                xx_pswhsd_discard_one(context, header_name, container,
                                      "The complete free header/parent backlink was unavailable; its overlapping stored header view was not trusted.");
            }
            break;
        }
        nodes[index].captured = true;
        if (!xx_pswhsd_value(free_layout, nodes[index].bytes, L"Node.Left", &left) || !xx_pswhsd_value(free_layout, nodes[index].bytes, L"Node.Right", &right) ||
            !xx_pswhsd_value(free_layout, nodes[index].bytes, L"Node.ParentValue", &parent_value) || (parent_value & ~UINT64_C(3)) != expected_parent) {
            xx_psw_warn_once(context, "A VS free-tree node had an inconsistent parent backlink.");
            break;
        }
        object_first = context->snapshot->structure_count;
        keep = xx_pswhvd_add(context, device, free_layout, free_name, container, width, true);
        if (keep) keep = xx_pswhvd_add(context, device, header_layout, header_name, container, width, false);
        /* A cap/OOM still leaves the captured object subject to its postcheck. */
        if (!xx_pswhsd_post(context, device, free_layout, free_name, container, width, nodes[index].bytes, object_first)) {
            changed = true;
            break;
        }
        if (!keep) break;
        /* Push may reallocate nodes; do not retain a pointer into that vector. */
        if (!xx_pswhvd_push(context, &nodes, &count, &capacity, left, node_address, width, free_layout, node_offset) ||
            !xx_pswhvd_push(context, &nodes, &count, &capacity, right, node_address, width, free_layout, node_offset))
            break;
    }
post:
    /* Recheck earlier nodes too: a rotation or unlink can retain the same
     * tree root while changing an already processed descendant relationship. */
    for (size_t i = 0; i < count; ++i)
        if (nodes[i].captured) {
            container = nodes[i].address - node_offset;
            if (!xx_pswhsd_capture(device, free_layout, container, width, after) || !xx_pswhsd_equal(nodes[i].bytes, after, free_layout->size)) {
                changed = true;
                break;
            }
        }
    if (!xx_ps_read(device, tree_address, tree_after, 2u * width) || !xx_pswhsd_equal(tree_before, tree_after, 2u * width)) changed = true;
    if (have_backend && (!xx_pswr_member_pointer(device, selected, L"VsContext.BackendCtx", heap_address, width, &check) || check != backend)) changed = true;
    if (changed) {
        xx_pswhsd_discard(context, first, "The VS free tree or a captured member changed during inspection; its new chunk views were discarded.");
        /* A previously collected physical view can have been deduplicated;
         * invalidate that same header too, without retaining object pointers. */
        for (size_t i = 0; i < count; ++i)
            if (nodes[i].captured) {
                container = nodes[i].address - node_offset;
                xx_pswhsd_discard_one(context, free_name, container, "The owning VS tree changed; this stored free-header snapshot was discarded.");
                xx_pswhsd_discard_one(context, header_name, container, "The owning VS tree changed; this overlapping stored chunk-header snapshot was discarded.");
            }
        xx_psw_warn_once(context, "The VS free tree changed during inspection; its chunk snapshots were discarded.");
    }
    free(nodes);
}

#undef XX_PSWHVD_HEADER_MAX
#endif /* XX_PROCESS_HEAP_VS_CHUNK_DISCOVERY_H */
