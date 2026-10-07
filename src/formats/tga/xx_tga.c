/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.ludorg.net/amnesia/TGA_File_Format_Spec.html
 * Stored encoded component extraction; no media decoding claims.
 */
#include "xxfclib/formats/tga/xx_tga.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool tg_index(Abstractformat *f,int64_t at,uint64_t pixels,unsigned step,uint32_t first,uint32_t count,xx_pd_struct *pd) {
    size_t capacity=xx_get_file_buffer_size(),i; uint64_t done=0,bytes;
    uint8_t *b,word[2]; unsigned used=0; bool result=true;
    if((step!=1 && step!=2) || pixels>UINT64_MAX/step) return false;
    bytes=pixels*step; if(!bytes) return true; if(capacity>bytes) capacity=(size_t)bytes;
    b=(uint8_t *)xx_mem_alloc(capacity);if(!b) return false;
    while(done<bytes) {
        size_t n=bytes-done>capacity ? capacity:(size_t)(bytes-done);
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at+(int64_t)done,b,n)) {result=false;break;}
        for(i=0;i<n;++i) {
            word[used++]=b[i]; if(used==step) {
                uint32_t v=step==1 ? word[0]:xx_data_get_u16(word, 2, 0, false); used=0;
                if(v<first || v-first>=count) {result=false;break;}
            }
        }
        if(!result) { break; } done+=n;
    }
    xx_mem_free(b);return result;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[18],footer[26]; unsigned kind,mode,bits,step,attributes; uint32_t w,height,first,count; uint64_t pixels,palette; int64_t at,data,foot=pm_available(f)-26; bool indexed,rle;
    if(foot<18 || !pm_read(f,0,h,18) || !pm_read(f,foot,footer,26) || xx_data_get_u32(footer, 4, 0, false) || xx_data_get_u32(footer+4, 4, 0, false) || xx_rt_memcmp(footer+8,"TRUEVISION-XFILE.\0",18)) return false;
    kind=h[2]; indexed=kind==1 || kind==9; rle=kind>=9; mode=rle ? kind-8 : kind; bits=h[16]; attributes=h[17]&15;
    w=xx_data_get_u16(h+12, 2, 0, false); height=xx_data_get_u16(h+14, 2, 0, false); first=xx_data_get_u16(h+3, 2, 0, false); count=xx_data_get_u16(h+5, 2, 0, false);
    if(!w || !height || mode<1 || mode>3 || (h[17]&0xC0) || h[1]>1) return false;
    if(indexed && (!h[1] || (bits!=8 && bits!=16) || attributes)) return false;
    if(mode==2 && ((bits!=15 && bits!=16 && bits!=24 && bits!=32) || (bits==24 && attributes) || (bits==15 && attributes) || (bits==16 && attributes>1) || (bits==32 && attributes!=0 && attributes!=8))) return false;
    if(mode==3 && ((bits!=8 && bits!=16) || (bits==8 && attributes) || (bits==16 && attributes!=8))) return false;
    if(h[1]) { if(!count || first+count>65536 || (h[7]!=15 && h[7]!=16 && h[7]!=24 && h[7]!=32)) return false; palette=(uint64_t)count*((h[7]+7U)/8U); }
    else { if(first || count || h[7]) return false; palette=0; }
    step=(bits+7U)/8U; pixels=(uint64_t)w*height; at=18+h[0]; if(at>foot || palette>(uint64_t)(foot-at)) return false;
    if(!pm_add(f,s,"descriptor.bin",0,18) || (h[0] && !pm_add(f,s,"image-id.bin",18,h[0])) || (palette && !pm_add(f,s,"palette.bin",at,(int64_t)palette))) { return false; } at+=(int64_t)palette; data=at;
    if(!rle) { uint64_t bytes=pixels*step; if(bytes>(uint64_t)(foot-at) || (indexed && !tg_index(f,at,pixels,step,first,count,pd))) return false; at+=(int64_t)bytes; }
    else { uint64_t done=0; while(done<pixels) { uint8_t packet; uint64_t run,stored;
            if((pd && xx_pd_is_stopped(pd)) || at>=foot || !pm_read(f,at++,&packet,1)) { return false; } run=(packet&127)+1U; stored=(packet&128) ? 1U : run;
            if(run>pixels-done || stored*step>(uint64_t)(foot-at) || (indexed && !tg_index(f,at,stored,step,first,count,pd))) { return false; } at+=(int64_t)(stored*step); done+=run;
        } }
    if(at!=foot || !pm_add(f,s,rle ? "pixels-rle.bin" : "pixels-raw.bin",data,at-data)) { return false; } s->size=foot+26; return true;
}

void xx_tga_init(xx_tga *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TGA,"tga"); } }
xx_tga *xx_tga_create(xx_io_device *d,int64_t b) { xx_tga *r=(xx_tga *)xx_mem_alloc(sizeof(*r)); if(r) xx_tga_init(r,d,b); return r; }
void xx_tga_destroy(xx_tga *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tga_free(xx_tga *r) { if(r) { xx_tga_destroy(r); xx_mem_free(r); } }
bool xx_tga_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tga_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
