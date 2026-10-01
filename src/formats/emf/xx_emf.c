/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-emf/de081cd7-351f-4cc2-830b-d03fb55e89ab
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/emf/xx_emf.h"
#include "../xx_fifth_data.h"

static bool sm_rect(const uint8_t *p) { return (int32_t)pm_le32(p)<=(int32_t)pm_le32(p+8) && (int32_t)pm_le32(p+4)<=(int32_t)pm_le32(p+12); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[88],b[32],objects[4096]; uint32_t hs,total,records,count=1,handles,desc,offset,points=0; uint64_t at; unsigned saved=0;
    if(!pm_read(f,0,h,88) || pm_le32(h)!=1 || !(hs=pm_le32(h+4)) || hs<88 || (hs&3) || !sm_rect(h+8) || !sm_rect(h+24) || xx_rt_memcmp(h+40," EMF",4) || pm_le32(h+44)!=0x10000 || (total=pm_le32(h+48))<(uint64_t)hs+20 || total>(uint64_t)pm_available(f) || (records=pm_le32(h+52))<3 || records>4096 || !(handles=pm_le16(h+56)) || handles>4096 || pm_le16(h+58) || pm_le32(h+68) || (int32_t)pm_le32(h+72)<=0 || (int32_t)pm_le32(h+76)<=0 || (int32_t)pm_le32(h+80)<=0 || (int32_t)pm_le32(h+84)<=0) return false;
    desc=pm_le32(h+60); offset=pm_le32(h+64);
    if(desc) { uint8_t text[2048]; uint32_t i; bool high=false;
        if(desc>1024 || desc<2 || offset!=88 || hs!=((88+desc*2+3)&~3U) || !pm_read(f,88,text,desc*2) || pm_le16(text+desc*2-2) || pm_le16(text+desc*2-4)) return false;
        for(i=0;i<desc;++i) { uint16_t v=pm_le16(text+i*2); if(fd_stop(pd)) return false;
            if(high) { if(v<0xdc00 || v>0xdfff) return false; high=false; }
            else if(v>=0xd800 && v<=0xdbff) high=true; else if(v>=0xdc00 && v<=0xdfff) return false;
        } if(high) return false;
    } else if(offset || hs!=88) return false;
    xx_mem_zero(objects,sizeof(objects)); at=hs; if(!pm_add(f,s,"emf-header.bin",0,hs)) return false;
    while(at<total) { uint32_t kind,size,wanted=0,v=0; char label[48];
        if(fd_stop(pd) || count>=records || !fd_range(at,8,total) || !pm_read(f,(int64_t)at,b,8)) return false; kind=pm_le32(b); size=pm_le32(b+4);
        if(size<8 || (size&3) || !fd_range(at,size,total) || !pm_read(f,(int64_t)at,b,size<32 ? size:32)) return false;
        if(kind==14) { if(size!=20 || pm_le32(b+8) || (pm_le32(b+12)!=0 && pm_le32(b+12)!=16) || pm_le32(b+16)!=20 || at+size!=total || count+1!=records) return false; }
        else if(kind>=2 && kind<=4) { uint32_t n;
            if(size<28 || !sm_rect(b+8) || !(n=pm_le32(b+24)) || n>65536-points || size!=28+(uint64_t)n*8 || (kind==2 && (n<4 || (n-1)%3)) || (kind==3 && n<3) || (kind==4 && n<2)) return false; points+=n;
        } else {
            if(kind>=9 && kind<=13) wanted=16;
            else if(kind==15) wanted=20;
            else if((kind>=16 && kind<=22) || kind==24 || kind==25 || kind==34 || kind==37 || kind==40) wanted=12;
            else if(kind==27 || kind==54) wanted=16;
            else if(kind==29 || kind==30 || kind==42 || kind==43) wanted=24;
            else if(kind==33 || kind==59 || kind==60 || kind==61) wanted=8;
            else if(kind==38) wanted=28; else if(kind==39) wanted=24; else return false;
            if(size!=wanted) return false;
            if(kind==29 || kind==30 || kind==42 || kind==43) { if(!sm_rect(b+8)) return false; }
            if(kind==33) { if(++saved>4096) return false; }
            if(kind==34) { int32_t level=(int32_t)pm_le32(b+8); if(level>=0 || level==INT32_MIN || (uint32_t)(-level)>saved) return false; saved-=(unsigned)(-level); }
            if(kind==37 || kind==38 || kind==39 || kind==40) {
                v=pm_le32(b+8);
                if(kind==37 && (v&0x80000000)) { if((v&0x7fffffff)>19) return false; }
                else { if(!v || v>=handles) return false;
                    if(kind==38 || kind==39) { if(objects[v]) return false; objects[v]=1; }
                    else if(!objects[v]) return false; else if(kind==40) objects[v]=0;
                }
            }
            if(kind==38 && (pm_le32(b+12)>8 || (int32_t)pm_le32(b+16)<0 || pm_le32(b+20))) return false;
            if(kind==39 && (pm_le32(b+12)>2 || (pm_le32(b+12)!=2 && pm_le32(b+20)))) return false;
        }
        xx_rt_snprintf(label,sizeof(label),"emf-record-%u-type-%u.bin",count,kind); if(!pm_add(f,s,label,(int64_t)at,size)) return false; ++count; at+=size;
        if(kind==14) { s->size=total; return true; }
    } return false;
}

void xx_emf_init(xx_emf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_EMF,"emf"); } }
xx_emf *xx_emf_create(xx_io_device *d,int64_t b) { xx_emf *r=(xx_emf *)xx_mem_alloc(sizeof(*r)); if(r) xx_emf_init(r,d,b); return r; }
void xx_emf_destroy(xx_emf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_emf_free(xx_emf *r) { if(r) { xx_emf_destroy(r); xx_mem_free(r); } }
bool xx_emf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_emf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
