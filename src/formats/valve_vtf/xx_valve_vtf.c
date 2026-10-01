/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/ValveSoftware/source-sdk-2013/master/src/public/vtf/vtf.h
 * Valve VTF 7.0–7.2 encoded thumbnails and mip/frame/face surfaces (RGBA/RGB/BGR/BGRA, DXT1/3/5). Resource-table 7.3+ and Xbox variants are rejected; no pixel decoding.
 */
#include "xxfclib/formats/valve_vtf/xx_valve_vtf.h"
#include "../xx_payload_members.h"

static uint16_t r16(const uint8_t *p,bool be) { return be ? pm_be16(p) : pm_le16(p); }
static uint32_t r32(const uint8_t *p,bool be) { return be ? pm_be32(p) : pm_le32(p); }
static uint64_t r64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[65]; uint32_t minor,header,w,height,depth=1,frames,levels,format,lowformat,i,j,faces=1; int64_t at; uint64_t n;
    if(!pm_read(f,0,h,63) || xx_rt_memcmp(h,"VTF\0",4) || pm_le32(h+4)!=7 || (minor=pm_le32(h+8))>2) return false;
    header=pm_le32(h+12); w=pm_le16(h+16); height=pm_le16(h+18); frames=pm_le16(h+24); levels=h[56]; format=pm_le32(h+52); lowformat=pm_le32(h+57);
    if(!w || !height || !frames || !levels || levels>16 || header<(minor==2 ? 65U : 63U) || header>4096 || header>pm_available(f)) return false;
    if(minor==2) { if(!pm_read(f,63,h+63,2) || !(depth=pm_le16(h+63))) return false; }
    if(pm_le32(h+20)&0x4000) { if(w!=height || depth!=1) return false; faces=pm_le16(h+26)==0xffff ? 6 : 7; }
    if((uint64_t)frames*faces*levels>65536 || format>15 || (format!=0 && format!=2 && format!=3 && format!=12 && format!=13 && format!=14 && format!=15)) return false;
    at=header;
    if(h[61] || h[62]) {
        if(!h[61] || !h[62] || lowformat!=13) return false;
        n=((h[61]+3ULL)/4)*((h[62]+3ULL)/4)*8;
        if(!pm_add(f,s,"thumbnail.dxt1",at,(int64_t)n)) return false; at+=(int64_t)n;
    } else if(lowformat!=0xffffffffU) return false;
    for(i=levels;i>0;--i) {
        uint32_t level=i-1,mw=w>>level,mh=height>>level,md=depth>>level;
        if(!mw) mw=1; if(!mh) mh=1; if(!md) md=1;
        n=format>=13 ? ((mw+3ULL)/4)*((mh+3ULL)/4)*md*(format==13 ? 8 : 16) : (uint64_t)mw*mh*md*(format==0 || format==12 ? 4 : 3);
        for(j=0;j<frames*faces;++j) {
            char label[48]; if(pd && xx_pd_is_stopped(pd)) return false;
            xx_rt_snprintf(label,sizeof(label),"mip-%u-image-%u.bin",(unsigned)level,(unsigned)j);
            if(n>INT64_MAX || !pm_add(f,s,label,at,(int64_t)n)) return false; at+=(int64_t)n;
        }
    }
    s->size=at; return true;
}

void xx_valve_vtf_init(xx_valve_vtf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_VALVE_VTF,"vtf"); } }
xx_valve_vtf *xx_valve_vtf_create(xx_io_device *d,int64_t b) { xx_valve_vtf *r=(xx_valve_vtf *)xx_mem_alloc(sizeof(*r)); if(r) xx_valve_vtf_init(r,d,b); return r; }
void xx_valve_vtf_destroy(xx_valve_vtf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_valve_vtf_free(xx_valve_vtf *r) { if(r) { xx_valve_vtf_destroy(r); xx_mem_free(r); } }
bool xx_valve_vtf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_valve_vtf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
