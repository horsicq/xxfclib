/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/RoadrunnerWMC/ndspy/master/ndspy/narc.py
 * Nintendo DS NARC v1 stored files. Uses stable numeric names; original filename directories are not reconstructed.
 */
#include "xxfclib/formats/nintendo_narc/xx_nintendo_narc.h"
#include "../xx_payload_members.h"

static uint16_t r16(const uint8_t *p,bool be) { return be ? pm_be16(p) : pm_le16(p); }
static uint32_t r32(const uint8_t *p,bool be) { return be ? pm_be32(p) : pm_le32(p); }
static uint64_t r64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[16],b[12],e[8]; uint32_t total,fat,names,count,i,start,end; int64_t image;
    if(!pm_read(f,0,h,16) || xx_rt_memcmp(h,"NARC",4) ||
       !((pm_le16(h+4)==0xfeff && pm_le16(h+6)==1) || (pm_le16(h+4)==0xfffe && pm_le16(h+6)==0x100)) ||
       pm_le16(h+12)!=16 || pm_le16(h+14)!=3) return false;
    total=pm_le32(h+8); if(total<44 || total>pm_available(f) || !pm_read(f,16,b,12) || xx_rt_memcmp(b,"BTAF",4)) return false;
    fat=pm_le32(b+4); count=pm_le32(b+8);
    if(count>65536 || fat!=12+(uint64_t)count*8 || fat>total-16 ||
       !pm_read(f,16+(int64_t)fat,b,8) || xx_rt_memcmp(b,"BTNF",4)) return false;
    names=pm_le32(b+4); if(names<16 || names>total-16-fat) return false;
    image=16+(int64_t)fat+names;
    if(image>total-8 || !pm_read(f,image,b,8) || xx_rt_memcmp(b,"GMIF",4) || pm_le32(b+4)!=total-image) return false;
    for(i=0;i<count;++i) {
        char label[40]; if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,28+(int64_t)i*8,e,8)) return false;
        start=pm_le32(e); end=pm_le32(e+4); if(end<start || end>total-image-8) return false;
        xx_rt_snprintf(label,sizeof(label),"file-%u.bin",(unsigned)i);
        if(!pm_add(f,s,label,image+8+start,end-start)) return false;
    }
    s->size=total; return true;
}

void xx_nintendo_narc_init(xx_nintendo_narc *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_NARC,"narc"); } }
xx_nintendo_narc *xx_nintendo_narc_create(xx_io_device *d,int64_t b) { xx_nintendo_narc *r=(xx_nintendo_narc *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_narc_init(r,d,b); return r; }
void xx_nintendo_narc_destroy(xx_nintendo_narc *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_narc_free(xx_nintendo_narc *r) { if(r) { xx_nintendo_narc_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_narc_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_narc_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
