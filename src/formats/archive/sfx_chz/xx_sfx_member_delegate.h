/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT.
 * Private SFX member-reader delegation. The wrapper authenticates carrier
 * framing; the existing archive reader owns names, codecs and checksums.
 * SFX_MEMBER_TYPE and member_child_{create,free} are supplied by the caller.
 */
static bool member_open(Abstractformat *f, xx_pd_struct *pd)
{
    SFX_MEMBER_TYPE *outer = (SFX_MEMBER_TYPE *)f;
    pm_stream *located;
    Abstractformat *child;
    int64_t saved, relative;
    bool valid;
    if (!f || !f->device || carrier_stop(pd)) return false;
    if (outer->inner) return true;
    saved = xx_io_tell(f->device);
    located = pm_open(f, pd);
    if (!located || located->count != 1) {
        pm_free_stream(located);
        return false;
    }
    relative = located->items[0].offset - f->base_address;
    child = member_child_create(f->device, located->items[0].offset);
    pm_free_stream(located);
    valid = child && xx_format_handle_base_info(child, pd) && child->number_of_archive_records &&
            child->format_size > 0 && carrier_range(pm_available(f), relative, child->format_size);
    if (saved >= 0 && xx_io_seek64(f->device, saved, SEEK_SET) != 0) valid = false;
    if (!valid) {
        if (child) member_child_free(child);
        return false;
    }
    outer->inner = child;
    outer->is_complete = member_child_complete(child);
    f->format_size = relative + child->format_size;
    f->number_of_archive_records = child->number_of_archive_records;
    f->overlay_offset = f->base_address + f->format_size;
    f->overlay_size = pm_available(f) - f->format_size;
    if (!f->overlay_size) f->overlay_offset = -1;
    f->is_valid = true;
    f->base_info_handled = true;
    return true;
}
static int64_t member_size(Abstractformat *f, xx_pd_struct *pd)
{
    return member_open(f, pd) ? f->format_size : -1;
}
static uint64_t member_count(Abstractformat *f, xx_pd_struct *pd)
{
    return member_open(f, pd) ? f->number_of_archive_records : 0;
}
static xx_archive_record_state *member_records(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    SFX_MEMBER_TYPE *outer = (SFX_MEMBER_TYPE *)f;
    size_t i;
    if (!member_open(f, pd)) return NULL;
    xx_format_cleanup_extra_parameters(outer->inner);
    for (i = 0; i < f->list_extra_parameters.count; ++i) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)&f->list_extra_parameters, i);
        if (meta && !xx_format_set_extra_parameter(outer->inner, meta->meta_id, &meta->var)) return NULL;
    }
    return xx_format_create_archive_records_reading(outer->inner, options, pd);
}
static const xx_archive_record *member_current(Abstractformat *f, xx_archive_record_state *state)
{
    SFX_MEMBER_TYPE *outer = (SFX_MEMBER_TYPE *)f;
    return outer && outer->inner ? xx_format_get_current_archive_record(outer->inner, state) : NULL;
}
static bool member_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    SFX_MEMBER_TYPE *outer = (SFX_MEMBER_TYPE *)f;
    return outer && outer->inner && xx_format_archive_record_move_to_next(outer->inner, state, pd);
}
static bool member_unpack(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    SFX_MEMBER_TYPE *outer = (SFX_MEMBER_TYPE *)f;
    return outer && outer->inner && xx_format_unpack_current_archive_record(outer->inner, state, pd);
}
static void member_free_records(Abstractformat *f, xx_archive_record_state *state)
{
    SFX_MEMBER_TYPE *outer = (SFX_MEMBER_TYPE *)f;
    if (outer && outer->inner) xx_format_free_archive_records_reading(outer->inner, state);
}
static void member_destroy(Abstractformat *f)
{
    SFX_MEMBER_TYPE *outer = (SFX_MEMBER_TYPE *)f;
    if (!outer) return;
    if (outer->inner) member_child_free(outer->inner);
    outer->inner = NULL;
    xx_format_cleanup_extra_parameters(f);
}
static void member_callbacks(Abstractformat *f)
{
    f->get_format_size = member_size;
    f->get_number_of_archive_records = member_count;
    f->create_archive_records_reading = member_records;
    f->get_current_archive_record = member_current;
    f->archive_record_move_to_next = member_next;
    f->unpack_current_archive_record = member_unpack;
    f->free_archive_records_reading = member_free_records;
    f->destroy = member_destroy;
}
