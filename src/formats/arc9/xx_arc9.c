/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/arc9/xx_arc9.h"
#include "xx_arc9_streams.h"
#include "xx_arc9_compressed.h"
#include "xx_arc9_games.h"
#include "xx_arc9_misc.h"
#include "xx_arc9_legacy.h"
#include "xx_arc9_compat.h"
#include "../xx_format_abstract_extractor_adapter.h"

#define XX_ARC9_FORMAT(name,type,group,label) \
Abstractformat *xx_##name##_create(xx_io_device *d,int64_t base) {return xx_arc9_##group##_create(d,base,XX_FILE_TYPE_##type);} \
void xx_##name##_free(Abstractformat *f) {xx_arc9_##group##_free(f);} \
static Abstractformat *xx_arc9_##name##_open(xx_io_device *d) {return xx_##name##_create(d,0);} \
static const xx_file_type_t xx_arc9_##name##_types[]={XX_FILE_TYPE_##type}; \
static const xx_format_search_desc xx_arc9_##name##_desc={xx_arc9_##name##_types,1,NULL,0,xx_arc9_##name##_open,xx_##name##_free,true}; \
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(name,xx_arc9_##name##_desc)
#include "xxfclib/formats/arc9/xx_arc9_rows.inc"
#undef XX_ARC9_FORMAT

const char *xx_arc9_type_name(xx_file_type_t type) {
    switch(type) {
#define XX_ARC9_FORMAT(name,value,group,label) case XX_FILE_TYPE_##value:return label;
#include "xxfclib/formats/arc9/xx_arc9_rows.inc"
#undef XX_ARC9_FORMAT
    default:return NULL;
    }
}
xx_file_type_t xx_arc9_detect_device(xx_io_device *d,xx_pd_struct *pd) {
    xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;int64_t saved;unsigned i;
    xx_file_type_t (*detect[])(xx_io_device *,int64_t)={xx_arc9_streams_detect,xx_arc9_compressed_detect,xx_arc9_games_detect,xx_arc9_misc_detect,xx_arc9_legacy_detect,xx_arc9_compat_detect};
    if(!d || (pd && xx_pd_is_stopped(pd)))return type;saved=xx_io_tell(d);if(saved<0)return type;
    for(i=0;i<sizeof(detect)/sizeof(detect[0]);++i) {type=detect[i](d,0);if(type!=XX_FILE_TYPE_UNKNOWN)break;}
    if(xx_io_seek64(d,saved,SEEK_SET))return XX_FILE_TYPE_UNKNOWN;
    return type;
}
