/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/KillzXGaming/Switch-Toolbox/blob/master/File_Format_Library/FileFormats/Archives/SARC.cs
 * Big/little endian SARC v1 stored files; numeric names avoid unsafe paths. Yaz0-wrapped SARC must be decoded separately.
 */
#include "xxfclib/formats/nintendo_sarc/xx_nintendo_sarc.h"
#include "../xx_payload_members.h"

static uint16_t r16(const uint8_t *p,bool be) { return be ? pm_be16(p) : pm_le16(p); }
static uint32_t r32(const uint8_t *p,bool be) { return be ? pm_be32(p) : pm_le32(p); }
static uint64_t r64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[20],b[12],e[16],c; bool be; uint32_t total,data,names,i,count;
    if(!pm_read(f,0,h,20) || xx_rt_memcmp(h,"SARC",4)) return false;
    be=h[6]==0xfe && h[7]==0xff; if(!be && !(h[6]==0xff && h[7]==0xfe)) return false;
    if(r16(h+4,be)!=20 || r16(h+16,be)!=0x100 || r16(h+18,be)) return false;
    total=r32(h+8,be); data=r32(h+12,be);
    if(total<40 || total>pm_available(f) || data>total || !pm_read(f,20,b,12) || xx_rt_memcmp(b,"SFAT",4) || r16(b+4,be)!=12) return false;
    count=r16(b+6,be); names=32+count*16;
    if(names>data || data-names<8 || !pm_read(f,names,b,8) || xx_rt_memcmp(b,"SFNT",4) || r16(b+4,be)!=8 || r16(b+6,be)) return false;
    names+=8;
    for(i=0;i<count;++i) {
        uint32_t attr,start,end; char label[40];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,32+(int64_t)i*16,e,16)) return false;
        attr=r32(e+4,be); start=r32(e+8,be); end=r32(e+12,be);
        if((attr>>24)>1 || end<start || end>total-data) return false;
        if(attr>>24) {
            uint64_t at=(uint64_t)names+(attr&0xffffffU)*4U; bool ended=false;
            if(at>=data) return false;
            for(;at<data;++at) { if(!pm_read(f,(int64_t)at,&c,1)) return false; if(!c) { ended=true; break; } }
            if(!ended) return false;
        }
        xx_rt_snprintf(label,sizeof(label),"file-%u.bin",(unsigned)i);
        if(!pm_add(f,s,label,(int64_t)data+start,end-start)) return false;
    }
    s->size=total; return true;
}

void xx_nintendo_sarc_init(xx_nintendo_sarc *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_SARC,"sarc"); } }
xx_nintendo_sarc *xx_nintendo_sarc_create(xx_io_device *d,int64_t b) { xx_nintendo_sarc *r=(xx_nintendo_sarc *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_sarc_init(r,d,b); return r; }
void xx_nintendo_sarc_destroy(xx_nintendo_sarc *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_sarc_free(xx_nintendo_sarc *r) { if(r) { xx_nintendo_sarc_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_sarc_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_sarc_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
