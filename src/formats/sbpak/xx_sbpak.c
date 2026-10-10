/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Sandlot SBPAK resource archive.
 */
#include "xxfclib/formats/sbpak/xx_sbpak.h"
#include "../xx_game_resource_helpers.h"
#include "../xx_format_abstract_extractor_adapter.h"

/* Sandlot SBPAK uses a recoverable byte mask for payloads and a separate
 * alternating mask for the reversed, length-salted name strings. */
static bool sbpak_sb_range(Abstractformat *f,pm_member *m,uint64_t at,void *destination,size_t size,xx_pd_struct *pd) {
    size_t i;uint8_t *out=(uint8_t *)destination,key=*(uint8_t *)m->context;
    if(xgr_stop(pd) || !xgr_range(at,size,(uint64_t)m->size) || !pm_read(f,m->offset-f->base_address+(int64_t)at,out,size)) return false;
    for(i=0;i<size;++i) out[i]^=key;
    return true;
}
static bool sbpak_sb_string(const uint8_t *names,size_t length,uint32_t at,char *out) {
    size_t i,n=0;
    if(at>=length) return false;
    while(at+n<length && names[at+n]) { if(++n>XGR_MAX_NAME) return false; }
    if(at+n==length || !n) return false;
    for(i=0;i<n;++i) out[i]=(char)names[at+n-1-i];out[n]=0;return true;
}
static bool sbpak_sb_key(const uint8_t *names,size_t length,size_t at,int *keys) {
    int key;if(at>=length) return false;
    key=names[at]^(uint8_t)(at-length);
    if(keys[at&1]>=0 && keys[at&1]!=key) return false;
    keys[at&1]=key;return true;
}
static bool sbpak_sbpak(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[52],r[16],*names=NULL,key;uint64_t n=(uint64_t)pm_available(f),names_at,data;uint32_t count,i;size_t length,k;int keys[2]={-1,-1};bool ok=false;
    if(n<52 || !pm_read(f,0,h,52) || memcmp(h,"SBPAK V 1.0\r\n",13)) return false;
    key=h[28];for(k=0;k<52;++k) h[k]^=key;
    count=xgr_le32(h+36);data=xgr_le32(h+44);names_at=52ULL+16ULL*count;
    if(xgr_le32(h+20)!=0x01020304U || xgr_le32(h+24)!=0xffffffffU || xgr_le32(h+28)!=0x00010000U || xgr_le32(h+48)!=n || !count || count>XGR_MAX_RECORDS || data<=names_at || data>n || data-names_at>32U*1024U*1024U) return false;
    length=(size_t)(data-names_at);names=(uint8_t *)xx_mem_alloc(length);
    if(!names || !pm_read(f,(int64_t)names_at,names,length)) goto done;
    for(k=0;k<length;++k) names[k]^=key;
    if(!sbpak_sb_key(names,length,length-1,keys)) goto done;
    for(i=0;i<count;++i) {
        uint32_t off;
        if(xgr_stop(pd) || !pm_read(f,52+(int64_t)i*16,r,16)) goto done;
        for(k=0;k<16;++k) r[k]^=key;off=xgr_le32(r+8)&0xffffffU;
        if(off>=length || (off && !sbpak_sb_key(names,length,off-1,keys))) goto done;
    }
    if(keys[0]<0) keys[0]=keys[1]==0xaa?0xa1:keys[1]==0xb1?0xb6:keys[1]==0xc4?0xc4:-1;
    if(keys[1]<0) keys[1]=keys[0]==0xa1?0xaa:keys[0]==0xb6?0xb1:keys[0]==0xc4?0xc4:-1;
    if(keys[0]<0 || keys[1]<0) goto done;
    for(k=0;k<length;++k) names[k]=(uint8_t)((names[k]^keys[k&1])+(length-k));
    for(i=0;i<count;++i) {
        char name[XGR_MAX_NAME+1],leaf[XGR_MAX_NAME+1];uint32_t size,at,no,dir;size_t a,b;
        if(xgr_stop(pd) || !pm_read(f,52+(int64_t)i*16,r,16)) goto done;
        for(k=0;k<16;++k) r[k]^=key;
        size=xgr_le32(r);at=xgr_le32(r+4);no=xgr_le32(r+8)&0xffffffU;dir=xgr_le32(r+12)&0xffffffU;
        if(no>UINT32_MAX-2 || !sbpak_sb_string(names,length,no+2,leaf)) goto done;
        name[0]=0;
        if(dir) { if(!sbpak_sb_string(names,length,dir,name)) goto done;a=xx_rt_strlen(name);while(a && (name[a-1]=='/' || name[a-1]=='\\')) name[--a]=0; }
        a=xx_rt_strlen(name);b=xx_rt_strlen(leaf);
        if(a+b+(a?1:0)>XGR_MAX_NAME) goto done;
        if(a) name[a++]='/';memcpy(name+a,leaf,b+1);
        if(!xgr_range(data+at,size,n) || !xgr_add(f,s,name,data+at,size)) goto done;
        {pm_member *m=&s->items[s->count-1];m->context=xx_mem_alloc(1);if(!m->context) goto done;*(uint8_t *)m->context=key;m->free_context=xgr_free;m->read_range=sbpak_sb_range;}
    }
    s->size=(int64_t)n;ok=true;
done:xx_mem_free(names);return ok;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    return pm_available(f)>=0 && !xgr_stop(pd) && sbpak_sbpak(f,s,pd);
}
Abstractformat *xx_sbpak_create(xx_io_device *d,int64_t base) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(!f) return NULL;
    xx_mem_zero(f,sizeof(*f));pm_init(f,d,base,XX_FILE_TYPE_SBPAK,"pak");return f;
}
void xx_sbpak_free(Abstractformat *f) {
    if(f) { xx_format_cleanup_extra_parameters(f);xx_mem_free(f); }
}
xx_file_type_t xx_sbpak_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[64];int64_t size,old=xx_io_tell(d);
    xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;size=pm_available(&f);
    if(size<8 || !pm_read(&f,0,h,(size_t)(size<64?size:64))) goto done;
    if(size>=52 && !memcmp(h,"SBPAK V 1.0\r\n",13)) type=XX_FILE_TYPE_SBPAK;
done:
    if(old>=0) xx_io_seek64(d,old,SEEK_SET);
    return type;
}
static Abstractformat *sbpak_open(xx_io_device *d) { return xx_sbpak_create(d,0); }
static const xx_file_type_t sbpak_types[]={XX_FILE_TYPE_SBPAK};
static const xx_format_search_desc sbpak_desc={
    sbpak_types,1,NULL,0,sbpak_open,xx_sbpak_free,true
};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(sbpak,sbpak_desc)
