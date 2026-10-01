/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/aseprite/aseprite/main/docs/ase-file-specs.md
 * Stored encoded component extraction; no image rendering or execution.
 */
#include "xxfclib/formats/aseprite/xx_aseprite.h"
#include "../xx_payload_members.h"

static bool as_string(Abstractformat *f,int64_t *pos,int64_t end) {
    uint8_t h[2]; unsigned len;
    if(end-*pos<2 || !pm_read(f,*pos,h,2)) return false; len=pm_le16(h); *pos+=2;
    if(len>(uint64_t)(end-*pos)) return false; *pos+=len; return true;
}
static bool as_palette(Abstractformat *f,int64_t pos,int64_t end,xx_pd_struct *pd) {
    uint8_t h[20]; uint32_t total,first,last,i;
    if(end-pos<20 || !pm_read(f,pos,h,20)) return false; total=pm_le32(h); first=pm_le32(h+4); last=pm_le32(h+8); pos+=20;
    if(!total || total>65536 || first>last || last>=total) return false;
    for(i=first;i<=last;++i) { uint8_t e[6]; unsigned flags;
        if((pd && xx_pd_is_stopped(pd)) || end-pos<6 || !pm_read(f,pos,e,6)) return false; flags=pm_le16(e); pos+=6;
        if(flags>1 || (flags && !as_string(f,&pos,end))) return false;
    }
    return pos==end;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[128],frame[16],chunk[24]; unsigned frames,depth,layers=0,frame_i,chunks_total=0; uint32_t total; int64_t pos=128; bool cel=false; char label[64];
    if(!pm_read(f,0,h,128) || pm_le16(h+4)!=0xA5E0 || pm_le32(h+20) || pm_le32(h+24) || (pm_le32(h+14)&~7U)) return false;
    total=pm_le32(h); frames=pm_le16(h+6); depth=pm_le16(h+12);
    if(total<128 || total>(uint64_t)pm_available(f) || !frames || frames>4096 || !pm_le16(h+8) || !pm_le16(h+10) || (uint64_t)pm_le16(h+8)*pm_le16(h+10)>67108864 || (depth!=8 && depth!=16 && depth!=32) || !pm_add(f,s,"descriptor.bin",0,128)) return false;
    for(frame_i=0;frame_i<frames;++frame_i) { uint32_t frame_size,count,i; int64_t end;
        if((pd && xx_pd_is_stopped(pd)) || total-pos<16 || !pm_read(f,pos,frame,16)) return false;
        frame_size=pm_le32(frame); count=pm_le32(frame+12); if(!count) count=pm_le16(frame+6);
        if(frame_size<16 || frame_size>(uint64_t)(total-pos) || pm_le16(frame+4)!=0xF1FA || frame[10] || frame[11] || count>4096-chunks_total) return false;
        chunks_total+=count; end=pos+frame_size; pos+=16;
        for(i=0;i<count;++i) { uint32_t size; unsigned type; int64_t payload,chunk_end;
            if((pd && xx_pd_is_stopped(pd)) || end-pos<6 || !pm_read(f,pos,chunk,6)) return false;
            size=pm_le32(chunk); type=pm_le16(chunk+4);
            if(size<6 || size>(uint64_t)(end-pos)) return false; payload=pos+6; chunk_end=pos+size;
            if(type==0x2004) { int64_t q; unsigned n;
                if(frame_i || layers==4096 || size<24 || !pm_read(f,payload,chunk,18) || pm_le16(chunk+2) || pm_le16(chunk+4) || pm_le16(chunk+10)>18 || chunk[13] || chunk[14] || chunk[15]) return false;
                n=pm_le16(chunk+16); q=payload+18+n;
                if(!n || q>chunk_end || q+((pm_le32(h+14)&4) ? 16 : 0)!=chunk_end) return false; ++layers;
            } else if(type==0x2005) { unsigned kind,w,height; uint64_t bytes;
                if(size<22 || !pm_read(f,payload,chunk,16) || pm_le16(chunk)>=layers) return false;
                kind=pm_le16(chunk+7);
                if(kind==0 || kind==2) {
                    if(size<26 || !pm_read(f,payload+16,chunk+16,4)) return false;
                    w=pm_le16(chunk+16); height=pm_le16(chunk+18); bytes=(uint64_t)w*height*(depth/8U);
                    if(!w || !height || bytes>268435456U) return false;
                    if(kind==0) { if(bytes!=size-26U) return false; }
                    else { uint8_t z[2]; if(size<32 || !pm_read(f,payload+20,z,2) || (z[0]&15)!=8 || (z[0]>>4)>7 || (z[1]&32) || (((unsigned)z[0]*256U+z[1])%31U)) return false; }
                } else if(kind==1) { if(size!=24 || !pm_read(f,payload+16,chunk+16,2) || pm_le16(chunk+16)>=frame_i) return false; }
                else return false; cel=true;
            } else if(type==0x2019) { if(!as_palette(f,payload,chunk_end,pd)) return false; }
            else if(type==0x2020) { uint32_t flags; int64_t q=payload+4;
                if(size<10 || !pm_read(f,payload,chunk,4)) return false; flags=pm_le32(chunk);
                if(flags>3 || ((flags&1) && !as_string(f,&q,chunk_end))) return false;
                if(flags&2) q+=4; if(q!=chunk_end) return false;
            }
            xx_rt_snprintf(label,sizeof(label),"frame-%u-chunk-%04X.bin",frame_i,type); if(!pm_add(f,s,label,payload,size-6)) return false; pos=chunk_end;
        }
        if(pos!=end) return false;
    }
    if(pos!=total || !layers || !cel) return false; s->size=total; return true;
}

void xx_aseprite_init(xx_aseprite *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ASEPRITE,"aseprite"); } }
xx_aseprite *xx_aseprite_create(xx_io_device *d,int64_t b) { xx_aseprite *r=(xx_aseprite *)xx_mem_alloc(sizeof(*r)); if(r) xx_aseprite_init(r,d,b); return r; }
void xx_aseprite_destroy(xx_aseprite *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_aseprite_free(xx_aseprite *r) { if(r) { xx_aseprite_destroy(r); xx_mem_free(r); } }
bool xx_aseprite_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_aseprite_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
