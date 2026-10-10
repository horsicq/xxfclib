/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * General CFBF face of the shared Binder/compound-file parser. No additional
 * global autodetection priority is introduced by this selected reader. */
#include "xxfclib/formats/cfbf/xx_cfbf.h"
#include "xxfclib/memory/xx_memory.h"

void xx_cfbf_init(xx_cfbf *r, xx_io_device *device, int64_t base)
{
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    xx_format_init(&r->format, device, base);
    r->format.file_type = XX_FILE_TYPE_CFBF;
    r->format.endian = XX_ENDIAN_LITTLE;
    r->format.format_type = XX_TYPE_ARCHIVE;
    r->format.is_archive = true;
    xx_format_set_mime_type(&r->format, "application/x-ole-storage");
    xx_format_set_extension(&r->format, "ole");
    r->format.check_is_valid = xx_cfbf_check_is_valid;
    r->format.handle_base_info = xx_cfbf_handle_base_info;
    r->format.get_format_size = xx_cfbf_get_format_size;
    r->format.get_number_of_archive_records = xx_cfbf_get_number_of_archive_records;
    r->format.create_archive_records_reading = xx_cfbf_create_archive_records_reading;
    r->format.get_current_archive_record = xx_cfbf_get_current_archive_record;
    r->format.unpack_current_archive_record = xx_cfbf_unpack_current_archive_record;
    r->format.archive_record_move_to_next = xx_cfbf_archive_record_move_to_next;
    r->format.free_archive_records_reading = xx_cfbf_free_archive_records_reading;
    r->archive_end = -1;
}
xx_cfbf *xx_cfbf_create(xx_io_device *d, int64_t base)
{
    xx_cfbf *r = (xx_cfbf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_cfbf_init(r, d, base);
    return r;
}
void xx_cfbf_destroy(xx_cfbf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_cfbf_free(xx_cfbf *r)
{
    if (r) { xx_cfbf_destroy(r); xx_mem_free(r); }
}
bool xx_cfbf_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return xx_binder_check_is_valid(f, pd); }
bool xx_cfbf_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return xx_binder_handle_base_info(f, pd); }
int64_t xx_cfbf_get_format_size(Abstractformat *f, xx_pd_struct *pd) { return xx_binder_get_format_size(f, pd); }
uint64_t xx_cfbf_get_number_of_archive_records(Abstractformat *f, xx_pd_struct *pd) { return xx_binder_get_number_of_archive_records(f, pd); }
xx_archive_record_state *xx_cfbf_create_archive_records_reading(Abstractformat *f, const xx_list_s *o, xx_pd_struct *pd) { return xx_binder_create_archive_records_reading(f, o, pd); }
const xx_archive_record *xx_cfbf_get_current_archive_record(Abstractformat *f, xx_archive_record_state *s) { return xx_binder_get_current_archive_record(f, s); }
bool xx_cfbf_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd) { return xx_binder_unpack_current_archive_record(f, s, pd); }
bool xx_cfbf_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd) { return xx_binder_archive_record_move_to_next(f, s, pd); }
void xx_cfbf_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *s) { xx_binder_free_archive_records_reading(f, s); }
xx_file_type_t xx_cfbf_detect(xx_io_device *d, int64_t base)
{
    xx_cfbf r;
    bool valid;
    xx_cfbf_init(&r, d, base);
    valid = xx_cfbf_check_is_valid(&r.format, NULL);
    xx_cfbf_destroy(&r);
    return valid ? XX_FILE_TYPE_CFBF : XX_FILE_TYPE_UNKNOWN;
}
