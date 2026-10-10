/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/mskn3/xx_mskn3.h"
#include "../xx_bounded_deflate_members.h"
#include "../xx_format_abstract_extractor_adapter.h"
#include "../xx_bounded_member_cursor.h"
static void mskn3_put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}

static bool mskn3_add_bitmap(Abstractformat *f,pm_stream *s,const char *name,const uint8_t *p,uint32_t w,uint32_t h) {
    uint64_t n=(uint64_t)w*h*4U;
    uint8_t *bmp; char label[96];
    if(!w || !h || w>INT32_MAX || h>INT32_MAX || n>BDM_MEMORY_LIMIT-54U) return false;
    bmp=(uint8_t *)xx_mem_alloc((size_t)n+54U);
    if(!bmp) return false;
    xx_mem_zero(bmp,54); bmp[0]='B'; bmp[1]='M'; mskn3_put32(bmp+2,(uint32_t)n+54U);
    mskn3_put32(bmp+10,54); mskn3_put32(bmp+14,40); mskn3_put32(bmp+18,w);
    mskn3_put32(bmp+22,0U-h); bmp[26]=1; bmp[28]=32; mskn3_put32(bmp+34,(uint32_t)n);
    xx_mem_copy(bmp+54,p,(size_t)n);
    (void)xx_rt_snprintf(label,sizeof(label),"%s.bmp",name);
    if(!bdm_add(f,s,label,0,0)) { xx_mem_free(bmp); return false; }
    s->items[s->count-1U].memory=bmp; s->items[s->count-1U].size=(int64_t)n+54;
    s->items[s->count-1U].compression_method=8;
    return true;
}

/* MSKN v3 has its own complete header and decoded member grammar. */
static bool mskn3_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t header[7]; bdm_buffer b={0}; bdm_cursor c;
    uint32_t i,count=0; int64_t used=0,size=pm_available(f);
    bool result=false;
    if(size<7+6 || !pm_read(f,0,header,sizeof(header)) ||
       xx_mem_compare(header,"KSMSSTL",sizeof(header)) ||
       !bdm_inflate(f,7,size-7,true,&b,&used,pd) || used!=size-7 ||
       !bdm_room(f,s,b.capacity)) goto done;
    c.data=b.data; c.size=b.size; c.at=0;
    {
        const uint8_t *ini; uint32_t n;
        if(!bdm_string(&c,NULL,0) || !bdm_word(&c,&n) || !bdm_take(&c,n,&ini) ||
           !bdm_room(f,s,(uint64_t)b.capacity+n+sizeof(pm_member)*8U) ||
           !bdm_add_memory(f,s,"theme.ini",ini,n) || !bdm_word(&c,&count) || count>65535U) goto done;
    }
    for(i=0;i<count;++i) {
        char name[80]; const uint8_t *p; uint32_t w,n;
        if((pd && xx_pd_is_stopped(pd)) ||
           !bdm_string(&c,name,sizeof(name))) goto done;
        {
            uint64_t bytes;
            if(!bdm_word(&c,&w) || !bdm_word(&c,&n)) goto done;
            bytes=(uint64_t)w*n*4U;
            if(bytes>BDM_MEMORY_LIMIT || !bdm_room(f,s,(uint64_t)b.capacity+bytes+54U+sizeof(pm_member)*(s->capacity?s->capacity:8U)) ||
               !bdm_take(&c,(size_t)bytes,&p) || !mskn3_add_bitmap(f,s,name,p,w,n)) goto done;
        }
        if(!bdm_string(&c,NULL,0) || !bdm_take(&c,2,NULL)) goto done;
    }
    /* Preserve remaining decoded theme-object metadata alongside the images. */
    if(c.at<c.size && (!bdm_room(f,s,(uint64_t)b.capacity+c.size-c.at+sizeof(pm_member)*(s->capacity?s->capacity:8U)) ||
       !bdm_add_memory(f,s,"theme-objects.bin",c.data+c.at,c.size-c.at))) goto done;
    if(!s->count) goto done;
    s->size=size;
    for(i=0;i<s->count;++i) s->items[i].packed_size=0;
    s->items[0].packed_size=size-7;
    result=true;
done:
    xx_mem_free(b.data); return result;
}

static bool pm_parse(Abstractformat *format, pm_stream *members, xx_pd_struct *pd) {
    if ((pd && xx_pd_is_stopped(pd)) || format->file_type != XX_FILE_TYPE_MSKN3) return false;
    return mskn3_parse(format,members,pd);
}

Abstractformat *xx_mskn3_create(xx_io_device *device, int64_t base) {
    Abstractformat *format = (Abstractformat *)xx_mem_alloc(sizeof(*format));
    if (format) pm_init(format, device, base, XX_FILE_TYPE_MSKN3, "mskn");
    return format;
}
void xx_mskn3_free(Abstractformat *format) {
    if (format) { xx_format_destroy(format); xx_mem_free(format); }
}

/* Detection keeps the inexpensive signature rule; the reader validates the grammar. */
xx_file_type_t xx_mskn3_detect(xx_io_device *device, int64_t base) {
    uint8_t signature[7]; int64_t saved,total;
    xx_file_type_t result=XX_FILE_TYPE_UNKNOWN;
    if(!device || base<0 || (total=xx_io_size(device))<base || total-base<7) return result;
    saved=xx_io_tell(device);
    if(xx_io_read_at(device,base,signature,sizeof(signature)) &&
       !xx_mem_compare(signature,"KSMSSTL",sizeof(signature))) result=XX_FILE_TYPE_MSKN3;
    if(saved>=0 && xx_io_seek64(device,saved,SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return result;
}

static Abstractformat *mskn3_open(xx_io_device *device) {
    return xx_mskn3_create(device, 0);
}
static const xx_file_type_t mskn3_types[] = {XX_FILE_TYPE_MSKN3};
static const xx_format_search_desc mskn3_descriptor = {
    mskn3_types, 1, NULL, 0, mskn3_open, xx_mskn3_free, true
};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(mskn3, mskn3_descriptor)
