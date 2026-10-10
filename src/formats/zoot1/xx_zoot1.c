/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/zoot1/xx_zoot1.h"
#include "../xx_bounded_deflate_members.h"
#include "../xx_format_abstract_extractor_adapter.h"
static bool zoot1_zoot_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[12],e[235]; uint32_t length,dirs,dir_size,stride,count,i;
    uint16_t version;
    int64_t size=pm_available(f),base;
    if(size<12 || !pm_read(f,0,h,12) || xx_mem_compare(h,"ZOT3",4)) return false;
    length=bdm_u32(h+4); version=bdm_u16(h+8); dirs=bdm_u16(h+10);
    if(!dirs || (version!=0x272e && version!=0x2738 && version!=0x2739)) return false;
    dir_size=version==0x272e?116U:76U; stride=version==0x2739?235U:137U;
    if(length<dirs*dir_size || (length-dirs*dir_size)%stride || (uint64_t)length+12U>(uint64_t)size) return false;
    count=(length-dirs*dir_size)/stride; base=12+(int64_t)dirs*dir_size;
    if(count>1000000U) return false;
    for(i=0;i<count;++i) {
        uint32_t offset,n; char name[65]; size_t j,end=60;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,base+(int64_t)i*stride,e,stride)) return false;
        if(bdm_u16(e+103)==0xfffeU || (version!=0x272e && bdm_u16(e+105)>=20U)) continue;
        offset=bdm_u32(e+93); n=bdm_u32(e+97);
        while(end && (e[end-1U]==' ' || e[end-1U]==0)) --end;
        for(j=0;j<end;++j) name[j]=(e[j]>=32 && e[j]<127)?(char)e[j]:'_';
        name[end]=0;
        if(!bdm_add(f,s,name,(int64_t)length+offset+11,n)) return false;
    }
    s->size=size; return true;
}

static bool pm_parse(Abstractformat *format, pm_stream *members, xx_pd_struct *pd) {
    if ((pd && xx_pd_is_stopped(pd)) || format->file_type != XX_FILE_TYPE_ZOOT1) return false;
    return zoot1_zoot_parse(format,members,pd);
}

Abstractformat *xx_zoot1_create(xx_io_device *device, int64_t base) {
    Abstractformat *format = (Abstractformat *)xx_mem_alloc(sizeof(*format));
    if (format) pm_init(format, device, base, XX_FILE_TYPE_ZOOT1, "zot");
    return format;
}
void xx_zoot1_free(Abstractformat *format) {
    if (format) { xx_format_destroy(format); xx_mem_free(format); }
}

/* Detection keeps the inexpensive signature rule; the reader validates the grammar. */
xx_file_type_t xx_zoot1_detect(xx_io_device *device, int64_t base) {
    uint8_t signature[4]; int64_t saved,total;
    xx_file_type_t result=XX_FILE_TYPE_UNKNOWN;
    if(!device || base<0 || (total=xx_io_size(device))<base || total-base<4) return result;
    saved=xx_io_tell(device);
    if(xx_io_read_at(device,base,signature,sizeof(signature)) &&
       !xx_mem_compare(signature,"ZOT3",sizeof(signature))) result=XX_FILE_TYPE_ZOOT1;
    if(saved>=0 && xx_io_seek64(device,saved,SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return result;
}

static Abstractformat *zoot1_open(xx_io_device *device) {
    return xx_zoot1_create(device, 0);
}
static const xx_file_type_t zoot1_types[] = {XX_FILE_TYPE_ZOOT1};
static const xx_format_search_desc zoot1_descriptor = {
    zoot1_types, 1, NULL, 0, zoot1_open, xx_zoot1_free, true
};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(zoot1, zoot1_descriptor)
