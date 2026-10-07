/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/devkitPro/ndstool/master/source/header.h
 * Nintendo DS unitcode 0 images with header/logo CRC16 checks. Exports ARM9/ARM7, FAT files and optional banner; validates Nitro FNT/overlay references. No secure-area decryption, DSi/modcrypt, relocation or execution.
 */
#include "xxfclib/formats/nintendo_nds/xx_nintendo_nds.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static XXFC_MAYBE_UNUSED uint64_t g64(const uint8_t *p,bool be) { return be ? ((uint64_t)xx_data_get_u32(p, 4, 0, true)<<32)|xx_data_get_u32(p+4, 4, 0, true) : ((uint64_t)xx_data_get_u32(p+4, 4, 0, false)<<32)|xx_data_get_u32(p, 4, 0, false); }
static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i;
    if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) { uint64_t a=(uint64_t)(s->items[i].offset-f->base_address),b=(uint64_t)s->items[i].size;
        if(n && b && at<a+b && a<at+n) return false; }
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static XXFC_MAYBE_UNUSED bool zname(Abstractformat *f,uint64_t at,uint64_t end) {
    uint8_t c; uint64_t i; if(at>=end || end>(uint64_t)pm_available(f)) return false;
    for(i=0;i<4096 && at+i<end;++i) { if(!pm_read(f,(int64_t)(at+i),&c,1)) return false; if(!c) return i!=0; } return false;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[512],e[32],b[8],c,dir_seen[4096],file_seen[4096]; uint64_t total,fnt,fns,fat,fas,metadata_at[4],metadata_size[4]; uint32_t files,dirs,i,j; char label[40];
    if(!pm_read(f,0,h,512) || h[0x12] || h[0x13] || h[0x14]>15 || xx_crc16_modbus_calc(0xffffU,h+0xc0,156)!=xx_data_get_u16(h+0x15c, 2, 0, false) || xx_data_get_u16(h+0x15c, 2, 0, false)!=0xcf56 || xx_crc16_modbus_calc(0xffffU,h,0x15e)!=xx_data_get_u16(h+0x15e, 2, 0, false)) return false;
    total=xx_data_get_u32(h+0x80, 4, 0, false); if(total<512 || total>(uint64_t)pm_available(f) || xx_data_get_u32(h+0x84, 4, 0, false)<512 || xx_data_get_u32(h+0x84, 4, 0, false)>total) return false;
    for(i=0;i<2;++i) { uint64_t at=xx_data_get_u32(h+0x20+i*16, 4, 0, false),n=xx_data_get_u32(h+0x2c+i*16, 4, 0, false);
        if(!n || at<xx_data_get_u32(h+0x84, 4, 0, false) || !emit(f,s,i ? "arm7.bin" : "arm9.bin",at,n,total)) return false; }
    fnt=xx_data_get_u32(h+0x40, 4, 0, false); fns=xx_data_get_u32(h+0x44, 4, 0, false); fat=xx_data_get_u32(h+0x48, 4, 0, false); fas=xx_data_get_u32(h+0x4c, 4, 0, false);
    if((fas%8) || fas/8>4092 || (fns && fns<8) || !span(fnt,fns,total) || !span(fat,fas,total) || (!!fns!=!!fas)) return false;
    files=(uint32_t)(fas/8); xx_mem_zero(dir_seen,sizeof(dir_seen)); xx_mem_zero(file_seen,sizeof(file_seen));
    for(i=0;i<s->count;++i) { uint64_t a=(uint64_t)(s->items[i].offset-f->base_address),n=(uint64_t)s->items[i].size; if((fns && a<fnt+fns && fnt<a+n) || (fas && a<fat+fas && fat<a+n)) return false; }
    if(fns) { uint32_t named=0;
        if(fnt<xx_data_get_u32(h+0x84, 4, 0, false) || fat<xx_data_get_u32(h+0x84, 4, 0, false) || !pm_read(f,(int64_t)fnt,b,8)) return false;
        dirs=xx_data_get_u16(b+6, 2, 0, false); if(!dirs || dirs>4096 || (uint64_t)dirs*8>fns || (fnt<fat+fas && fat<fnt+fns)) return false;
        for(i=0;i<dirs;++i) { uint64_t pos; uint32_t id;
            if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)(fnt+i*8),b,8)) return false;
            pos=xx_data_get_u32(b, 4, 0, false); id=xx_data_get_u16(b+4, 2, 0, false); if(pos<(uint64_t)dirs*8 || pos>=fns || id>files || (i && (xx_data_get_u16(b+6, 2, 0, false)<0xf000 || xx_data_get_u16(b+6, 2, 0, false)>=0xf000+dirs))) return false;
            for(j=0;j<4096;++j) { unsigned len; if(pos>=fns || !pm_read(f,(int64_t)(fnt+pos++),&c,1)) return false; if(!c) break;
                len=c&127; if(!len || !span(pos,len+(c&128 ? 2 : 0),fns)) return false; pos+=len;
                if(c&128) { uint32_t child; if(!pm_read(f,(int64_t)(fnt+pos),b,2) || xx_data_get_u16(b, 2, 0, false)<=0xf000 || xx_data_get_u16(b, 2, 0, false)>=0xf000+dirs) return false; pos+=2;
                    child=xx_data_get_u16(b, 2, 0, false)-0xf000; if(child<=i || dir_seen[child]++ || !pm_read(f,(int64_t)(fnt+child*8+6),b,2) || xx_data_get_u16(b, 2, 0, false)!=0xf000+i) return false; }
                else { if(id>=files || file_seen[id]++) return false; ++id; ++named; } }
            if(j==4096) return false; }
        if(named>files) { return false; } for(i=1;i<dirs;++i) if(!dir_seen[i]) return false;
    }
    for(i=0;i<files;++i) { uint64_t start,end;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)(fat+i*8),b,8)) return false;
        start=xx_data_get_u32(b, 4, 0, false); end=xx_data_get_u32(b+4, 4, 0, false);
        if(start<xx_data_get_u32(h+0x84, 4, 0, false) || end<start || (start<fnt+fns && fnt<end) || (start<fat+fas && fat<end)) return false;
        xx_rt_snprintf(label,sizeof(label),"file-%u.bin",i); if(!emit(f,s,label,start,end-start,total)) return false; }
    for(i=0;i<2;++i) { uint64_t at=xx_data_get_u32(h+0x50+i*8, 4, 0, false),n=xx_data_get_u32(h+0x54+i*8, 4, 0, false);
        if(!span(at,n,total) || n%32 || n/32>files || (n && at<xx_data_get_u32(h+0x84, 4, 0, false))) return false;
        for(j=0;j<n/32;++j) if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)(at+j*32),e,32) || xx_data_get_u32(e+24, 4, 0, false)>=files) return false; }
    metadata_at[0]=fnt; metadata_size[0]=fns; metadata_at[1]=fat; metadata_size[1]=fas;
    for(i=0;i<2;++i) { metadata_at[2+i]=xx_data_get_u32(h+0x50+i*8, 4, 0, false); metadata_size[2+i]=xx_data_get_u32(h+0x54+i*8, 4, 0, false); }
    for(i=0;i<4;++i) { uint64_t at=metadata_at[i],n=metadata_size[i]; if(!n) { if(at) return false; continue; }
        for(j=0;j<i;++j) if(metadata_size[j] && at<metadata_at[j]+metadata_size[j] && metadata_at[j]<at+n) return false;
        for(j=0;j<s->count;++j) { uint64_t a=(uint64_t)(s->items[j].offset-f->base_address),sz=(uint64_t)s->items[j].size; if(sz && at<a+sz && a<at+n) return false; } }
    if(xx_data_get_u32(h+0x68, 4, 0, false)) { uint64_t at=xx_data_get_u32(h+0x68, 4, 0, false),n; if(at<xx_data_get_u32(h+0x84, 4, 0, false) || !pm_read(f,(int64_t)at,b,2)) return false;
        n=xx_data_get_u16(b, 2, 0, false)==1 ? 0x840 : xx_data_get_u16(b, 2, 0, false)==2 ? 0x940 : xx_data_get_u16(b, 2, 0, false)==3 ? 0x1240 : 0;
        for(i=0;i<4;++i) if(metadata_size[i] && at<metadata_at[i]+metadata_size[i] && metadata_at[i]<at+n) return false;
        if(!n || !emit(f,s,"banner.bin",at,n,total)) return false; }
    s->size=(int64_t)total; return true;

}

void xx_nintendo_nds_init(xx_nintendo_nds *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_NDS,"nds"); } }
xx_nintendo_nds *xx_nintendo_nds_create(xx_io_device *d,int64_t b) { xx_nintendo_nds *r=(xx_nintendo_nds *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_nds_init(r,d,b); return r; }
void xx_nintendo_nds_destroy(xx_nintendo_nds *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_nds_free(xx_nintendo_nds *r) { if(r) { xx_nintendo_nds_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_nds_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_nds_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
