/* SPDX-License-Identifier: MIT. Appended readers use structural validation,
 * bounded signatures, and preserve the device cursor. */
#include "xxfclib/formats/xx_format_gap_headers.inc"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include <limits.h>
#include "xx_archive_additions_detect.inc"
#include "xx_disk_additions_detect.inc"
#include "xx_apple_additions_detect.inc"
#include "xx_volume_additions_detect.inc"
typedef struct gap_reader { xx_file_type_t type; Abstractformat *(*create)(xx_io_device *,int64_t); void (*destroy)(void *); unsigned group; } gap_reader;
#define GAP_READER(stem,type,group) static Abstractformat *gap_mk_##stem(xx_io_device *d,int64_t b) { return (Abstractformat *)xx_##stem##_create(d,b); } static void gap_rm_##stem(void *p) { xx_##stem##_free((xx_##stem *)p); }
#include "xx_format_gap_detector_rows.inc"
#undef GAP_READER
enum { gap_archive,gap_apple,gap_disk,gap_volume };
#define GAP_READER(stem,type,group) {type,gap_mk_##stem,gap_rm_##stem,gap_##group},
static const gap_reader gap_readers[]={
#include "xx_format_gap_detector_rows.inc"
};
#undef GAP_READER
xx_file_type_t xx_format_gap_detect(xx_io_device *device) {
    uint8_t first[512]; size_t size,i; int64_t total,saved; xx_file_type_t result=XX_FILE_TYPE_UNKNOWN;
    if(!device || (total=xx_io_size(device))<4 || (saved=xx_io_tell(device))<0) return result;
    size=total<(int64_t)sizeof(first)?(size_t)total:sizeof(first);
    if(xx_io_seek64(device,0,SEEK_SET) || xx_io_read(device,first,size)!=(ssize_t)size) goto done;
    for(i=0;i<sizeof(gap_readers)/sizeof(gap_readers[0]);++i) {
        const gap_reader *row=&gap_readers[i]; bool candidate=false,valid; Abstractformat *reader;
        switch(row->group) {
        case gap_archive:candidate=xx_archive_addition_candidate(row->type,device,first,size,total);break;
        case gap_disk:candidate=xx_disk_addition_candidate(row->type,first,size,(uint64_t)total);break;
        case gap_apple:candidate=xx_apple_addition_candidate(row->type,device,first,size,total);break;
        case gap_volume:candidate=xx_volume_addition_candidate(row->type,device,first,size,total);break;
        }
        if(!candidate) continue; reader=row->create(device,0); if(!reader) continue;
        valid=xx_format_is_valid(reader,NULL); row->destroy(reader);
        if(valid) { result=row->type; break; }
    }
done:(void)xx_io_seek64(device,saved,SEEK_SET); return result;
}
