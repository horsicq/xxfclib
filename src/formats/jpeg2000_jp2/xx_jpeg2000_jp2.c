/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://raw.githubusercontent.com/uclouvain/openjpeg/master/src/lib/openjp2/jp2.c, https://raw.githubusercontent.com/uclouvain/openjpeg/master/src/lib/openjp2/j2k.c
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#include "xxfclib/formats/jpeg2000_jp2/xx_jpeg2000_jp2.h"
#include "../xx_payload_members.h"

static uint64_t jp_be64(const uint8_t *p) { return (uint64_t)pm_be32(p)<<32|pm_be32(p+4); }
static bool jp_stream(Abstractformat *f,int64_t at,int64_t end,uint32_t width,uint32_t height,uint16_t channels) {
    uint8_t h[40]; bool siz=false,tile=false; int64_t tile_end=0;
    if(end-at<4 || !pm_read(f,at,h,2) || h[0]!=255 || h[1]!=79) return false; at+=2;
    while(at<end) { uint16_t marker,n;
        if(end-at<2 || !pm_read(f,at,h,2) || h[0]!=255 || !h[1] || h[1]==255) return false; marker=pm_be16(h); at+=2;
        if(marker==0xFFD9) return siz && tile && at==end;
        if(marker==0xFF93) { if(!tile || tile_end<=at || tile_end>end-2) return false; at=tile_end; continue; }
        if(marker==0xFF4F || marker==0xFF92 || marker==0xFF91 || end-at<2 || !pm_read(f,at,h,2) || (n=pm_be16(h))<2 || n>(uint64_t)(end-at)) return false;
        if(!siz && marker!=0xFF51) return false;
        if(marker==0xFF51) { uint32_t xs,ys,xo,yo; if(siz || n<38 || !pm_read(f,at,h,38)) return false;
            xs=pm_be32(h+4); ys=pm_be32(h+8); xo=pm_be32(h+12); yo=pm_be32(h+16);
            if(xs<=xo || ys<=yo || xs-xo!=width || ys-yo!=height || pm_be16(h+36)!=channels || n!=38U+3U*channels || !pm_be32(h+20) || !pm_be32(h+24) || pm_be32(h+28)>xo || pm_be32(h+32)>yo) return false; siz=true;
        } else if(marker==0xFF90) { uint32_t length; if(n!=10 || !pm_read(f,at,h,10) || (length=pm_be32(h+4))<14 || length>(uint64_t)(end-(at-2))) return false; tile_end=at-2+length; tile=true; }
        at+=n;
    } return false;
}
static bool jp_boxes(Abstractformat *f,pm_stream *s,int64_t at,int64_t end,bool header,uint32_t *width,uint32_t *height,uint16_t *channels,unsigned *ihdr,unsigned *color,unsigned *streams,xx_pd_struct *pd) {
    unsigned boxes=0;
    while(at<end) { uint8_t h[32]; uint64_t size; int64_t head=8,body; char name[16]; unsigned i;
        if((pd && xx_pd_is_stopped(pd)) || end-at<8 || !pm_read(f,at,h,8) || ++boxes>65536) return false; size=pm_be32(h);
        if(size==1) { if(end-at<16 || !pm_read(f,at+8,h+8,8)) return false; size=jp_be64(h+8); head=16; } else if(!size) size=(uint64_t)(end-at);
        if(size<(uint64_t)head || size>(uint64_t)(end-at)) return false; body=at+head;
        if(!xx_rt_memcmp(h+4,"jp2h",4)) { if(header || !jp_boxes(f,s,body,at+(int64_t)size,true,width,height,channels,ihdr,color,streams,pd)) return false; }
        else {
            if(header && !*ihdr && xx_rt_memcmp(h+4,"ihdr",4)) return false;
            if(!xx_rt_memcmp(h+4,"ihdr",4)) { if(!header || (*ihdr)++ || size-head!=14 || !pm_read(f,body,h+8,14) || !(*height=pm_be32(h+8)) || !(*width=pm_be32(h+12)) || !(*channels=pm_be16(h+16)) || h[19]!=7 || h[20]>1 || h[21]>1) return false; }
            if(!xx_rt_memcmp(h+4,"colr",4)) { if(!header || size-head<3 || !pm_read(f,body,h+8,3) || (h[8]!=1 && h[8]!=2) || (h[8]==1 && size-head!=7) || (h[8]==2 && size-head<=3)) return false; ++*color; }
            if(!xx_rt_memcmp(h+4,"jp2c",4)) { if(header || !*ihdr || !*color || !jp_stream(f,body,at+(int64_t)size,*width,*height,*channels)) return false; ++*streams; }
            for(i=0;i<4;++i) name[i]=h[4+i]>=32 && h[4+i]<127 ? (char)h[4+i] : '_'; name[4]=0; xx_rt_memcpy(name+4,".bin",5);
            if(!pm_add(f,s,name,body,(int64_t)size-head)) return false;
        } at+=(int64_t)size;
    } return at==end;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[24]; uint32_t n,width=0,height=0; uint16_t channels=0; unsigned ihdr=0,color=0,streams=0; int64_t limit=pm_available(f);
    if(!pm_read(f,0,h,24) || xx_rt_memcmp(h,"\x00\x00\x00\x0CjP  \x0D\x0A\x87\x0A",12) || xx_rt_memcmp(h+16,"ftyp",4) || (n=pm_be32(h+12))<20 || n>(uint64_t)(limit-12) || ((n-16)&3) || xx_rt_memcmp(h+20,"jp2 ",4)) return false;
    if(!jp_boxes(f,s,12,limit,false,&width,&height,&channels,&ihdr,&color,&streams,pd) || ihdr!=1 || !color || !streams) return false; s->size=limit; return true;
}

void xx_jpeg2000_jp2_init(xx_jpeg2000_jp2 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_JPEG2000_JP2,"jpeg2000_jp2"); } }
xx_jpeg2000_jp2 *xx_jpeg2000_jp2_create(xx_io_device *d,int64_t b) { xx_jpeg2000_jp2 *r=(xx_jpeg2000_jp2 *)xx_mem_alloc(sizeof(*r)); if(r) xx_jpeg2000_jp2_init(r,d,b); return r; }
void xx_jpeg2000_jp2_destroy(xx_jpeg2000_jp2 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_jpeg2000_jp2_free(xx_jpeg2000_jp2 *r) { if(r) { xx_jpeg2000_jp2_destroy(r); xx_mem_free(r); } }
bool xx_jpeg2000_jp2_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_jpeg2000_jp2_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
