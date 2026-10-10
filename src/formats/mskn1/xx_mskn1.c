/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/mskn1/xx_mskn1.h"
#include "../xx_bounded_deflate_members.h"
#include "../xx_format_abstract_extractor_adapter.h"
#include "../xx_bounded_member_cursor.h"
static void mskn1_put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}

static bool mskn1_add_bitmap(Abstractformat *f,pm_stream *s,const char *name,const uint8_t *p,uint32_t w,uint32_t h) {
    uint64_t n=(uint64_t)w*h*4U;
    uint8_t *bmp; char label[96];
    if(!w || !h || w>INT32_MAX || h>INT32_MAX || n>BDM_MEMORY_LIMIT-54U) return false;
    bmp=(uint8_t *)xx_mem_alloc((size_t)n+54U);
    if(!bmp) return false;
    xx_mem_zero(bmp,54); bmp[0]='B'; bmp[1]='M'; mskn1_put32(bmp+2,(uint32_t)n+54U);
    mskn1_put32(bmp+10,54); mskn1_put32(bmp+14,40); mskn1_put32(bmp+18,w);
    mskn1_put32(bmp+22,0U-h); bmp[26]=1; bmp[28]=32; mskn1_put32(bmp+34,(uint32_t)n);
    xx_mem_copy(bmp+54,p,(size_t)n);
    (void)xx_rt_snprintf(label,sizeof(label),"%s.bmp",name);
    if(!bdm_add(f,s,label,0,0)) { xx_mem_free(bmp); return false; }
    s->items[s->count-1U].memory=bmp; s->items[s->count-1U].size=(int64_t)n+54;
    s->items[s->count-1U].compression_method=8;
    return true;
}

static bool mskn1_mskn1_entries_fit(bdm_cursor c,uint32_t count,bool bitmap) {
    uint32_t i;
    for(i=0;i<count;++i) {
        uint32_t n,w;
        if(!bdm_string(&c,NULL,0) || !bdm_word(&c,&n)) return false;
        if(bitmap) {
            uint64_t bytes;
            w=n;
            if(!w || !bdm_word(&c,&n) || !n) return false;
            bytes=(uint64_t)w*n*4U;
            if(bytes>BDM_MEMORY_LIMIT || !bdm_take(&c,(size_t)bytes,NULL)) return false;
        } else if(!bdm_take(&c,n,NULL)) return false;
    }
    return true;
}

/* MSKN v1 has its own complete header and decoded member grammar. */
static bool mskn1_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t header[4]; bdm_buffer b={0}; bdm_cursor c;
    uint32_t i,count=0; int64_t used=0,size=pm_available(f);
    bool result=false;
    uint32_t flags; bool raw_bitmaps;
    if(size<4+6 || !pm_read(f,0,header,sizeof(header)) ||
       xx_mem_compare(header,"KSLZ",sizeof(header)) ||
       !bdm_inflate(f,4,size-4,true,&b,&used,pd) || used!=size-4 ||
       !bdm_room(f,s,b.capacity)) goto done;
    c.data=b.data; c.size=b.size; c.at=0;
    for(i=0;i<5;++i) if(!bdm_string(&c,NULL,0)) goto done;
    if(!bdm_word(&c,&flags)) goto done;
    count=flags&65535U;
    if((flags>>16)!=0 && (flags>>16)!=15U) goto done;
    raw_bitmaps=(flags>>16)==15U;
    /* Early SkinEngine II 2.0 writes raw BGRA images without the later marker.
     * Accept that complete grammar only if the stored-member grammar fails. */
    if(!raw_bitmaps && !mskn1_mskn1_entries_fit(c,count,false)) {
        if(!mskn1_mskn1_entries_fit(c,count,true)) goto done;
        raw_bitmaps=true;
    }
    for(i=0;i<count;++i) {
        char name[80]; const uint8_t *p; uint32_t w,n;
        if((pd && xx_pd_is_stopped(pd)) ||
           !bdm_string(&c,name,sizeof(name))) goto done;
        if(raw_bitmaps) {
            uint64_t bytes;
            if(!bdm_word(&c,&w) || !bdm_word(&c,&n)) goto done;
            bytes=(uint64_t)w*n*4U;
            if(bytes>BDM_MEMORY_LIMIT || !bdm_room(f,s,(uint64_t)b.capacity+bytes+54U+sizeof(pm_member)*(s->capacity?s->capacity:8U)) ||
               !bdm_take(&c,(size_t)bytes,&p) || !mskn1_add_bitmap(f,s,name,p,w,n)) goto done;
        } else {
            if(!bdm_word(&c,&n) || !bdm_room(f,s,(uint64_t)b.capacity+n+sizeof(pm_member)*(s->capacity?s->capacity:8U)) ||
               !bdm_take(&c,n,&p) || !bdm_add_memory(f,s,name,p,n)) goto done;
            s->items[s->count-1U].compression_method=8;
        }
    }
    /* Preserve remaining decoded theme-object metadata alongside the images. */
    if(c.at<c.size && (!bdm_room(f,s,(uint64_t)b.capacity+c.size-c.at+sizeof(pm_member)*(s->capacity?s->capacity:8U)) ||
       !bdm_add_memory(f,s,"theme-objects.bin",c.data+c.at,c.size-c.at))) goto done;
    if(!s->count) goto done;
    s->size=size;
    for(i=0;i<s->count;++i) s->items[i].packed_size=0;
    s->items[0].packed_size=size-4;
    result=true;
done:
    xx_mem_free(b.data); return result;
}

static bool pm_parse(Abstractformat *format, pm_stream *members, xx_pd_struct *pd) {
    if ((pd && xx_pd_is_stopped(pd)) || format->file_type != XX_FILE_TYPE_MSKN1) return false;
    return mskn1_parse(format,members,pd);
}

Abstractformat *xx_mskn1_create(xx_io_device *device, int64_t base) {
    Abstractformat *format = (Abstractformat *)xx_mem_alloc(sizeof(*format));
    if (format) pm_init(format, device, base, XX_FILE_TYPE_MSKN1, "mskn");
    return format;
}
void xx_mskn1_free(Abstractformat *format) {
    if (format) { xx_format_destroy(format); xx_mem_free(format); }
}

/* Detection keeps the inexpensive signature rule; the reader validates the grammar. */
xx_file_type_t xx_mskn1_detect(xx_io_device *device, int64_t base) {
    uint8_t signature[4]; int64_t saved,total;
    xx_file_type_t result=XX_FILE_TYPE_UNKNOWN;
    if(!device || base<0 || (total=xx_io_size(device))<base || total-base<4) return result;
    saved=xx_io_tell(device);
    if(xx_io_read_at(device,base,signature,sizeof(signature)) &&
       !xx_mem_compare(signature,"KSLZ",sizeof(signature))) result=XX_FILE_TYPE_MSKN1;
    if(saved>=0 && xx_io_seek64(device,saved,SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return result;
}

static Abstractformat *mskn1_open(xx_io_device *device) {
    return xx_mskn1_create(device, 0);
}
static const xx_file_type_t mskn1_types[] = {XX_FILE_TYPE_MSKN1};
static const xx_format_search_desc mskn1_descriptor = {
    mskn1_types, 1, NULL, 0, mskn1_open, xx_mskn1_free, true
};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(mskn1, mskn1_descriptor)
