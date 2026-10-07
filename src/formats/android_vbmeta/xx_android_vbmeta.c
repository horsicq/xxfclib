/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://android.googlesource.com/platform/external/avb/+/refs/heads/main/libavb/avb_vbmeta_image.h
 * Exports bounded authentication/key/descriptor components. Does not verify signatures or establish trust.
 */
#include "xxfclib/formats/android_vbmeta/xx_android_vbmeta.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool part(Abstractformat *f,pm_stream *s,const uint8_t *h,unsigned at,uint64_t base,uint64_t limit,const char *name) {
    uint64_t off=xx_data_get_u64(h+at, 8, 0, true),size=xx_data_get_u64(h+at+8, 8, 0, true);
    return off<=limit && size<=limit-off && (!size || pm_add(f,s,name,(int64_t)(base+off),(int64_t)size));
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[256],d[16]; uint64_t auth,aux,start,left; unsigned i;
    if(!pm_read(f,0,h,256) || xx_rt_memcmp(h,"AVB0",4) || xx_data_get_u32(h+4, 4, 0, true)!=1 || xx_data_get_u32(h+8, 4, 0, true)>3 || xx_data_get_u32(h+28, 4, 0, true)>8) return false;
    auth=xx_data_get_u64(h+12, 8, 0, true); aux=xx_data_get_u64(h+20, 8, 0, true);
    if(auth%64 || aux%64 || auth>(uint64_t)pm_available(f)-256 || aux>(uint64_t)pm_available(f)-256-auth) return false;
    for(i=176;i<256;++i) if(h[i]) return false;
    for(i=128;i<176 && h[i];++i) {} if(i==176) return false;
    if(xx_data_get_u32(h+28, 4, 0, true)==0 && (auth || xx_data_get_u64(h+40, 8, 0, true) || xx_data_get_u64(h+56, 8, 0, true))) return false;
    if(!part(f,s,h,32,256,auth,"hash.bin") || !part(f,s,h,48,256,auth,"signature.bin") ||
       !part(f,s,h,64,256+auth,aux,"public-key.bin") || !part(f,s,h,80,256+auth,aux,"public-key-metadata.bin")) return false;
    start=xx_data_get_u64(h+96, 8, 0, true); left=xx_data_get_u64(h+104, 8, 0, true); if(start>aux || left>aux-start) return false;
    start+=256+auth;
    while(left) {
        uint64_t size,tag; char name[64];
        if((pd && xx_pd_is_stopped(pd)) || left<16 || !pm_read(f,(int64_t)start,d,16)) return false;
        tag=xx_data_get_u64(d, 8, 0, true); size=xx_data_get_u64(d+8, 8, 0, true);
        if(size%8 || size>left-16) return false;
        xx_rt_snprintf(name,sizeof(name),"descriptor-tag-%llu.bin",(unsigned long long)tag);
        if(!pm_add(f,s,name,(int64_t)start,(int64_t)(size+16))) return false;
        start+=size+16; left-=size+16;
    }
    s->size=(int64_t)(256+auth+aux); return s->count>0;
}

void xx_android_vbmeta_init(xx_android_vbmeta *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ANDROID_VBMETA,"android_vbmeta"); } }
xx_android_vbmeta *xx_android_vbmeta_create(xx_io_device *d,int64_t b) { xx_android_vbmeta *r=(xx_android_vbmeta *)xx_mem_alloc(sizeof(*r)); if(r) xx_android_vbmeta_init(r,d,b); return r; }
void xx_android_vbmeta_destroy(xx_android_vbmeta *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_android_vbmeta_free(xx_android_vbmeta *r) { if(r) { xx_android_vbmeta_destroy(r); xx_mem_free(r); } }
bool xx_android_vbmeta_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_android_vbmeta_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
