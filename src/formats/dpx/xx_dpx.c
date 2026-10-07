/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://pub.smpte.org/latest/st268-1/st0268-1-2014_stable2015.pdf, https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/dpx.c
 * Stored encoded component extraction; no image rendering or execution.
 */
#include "xxfclib/formats/dpx/xx_dpx.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static unsigned dx_u16(const uint8_t *p,bool little) { return little ? xx_data_get_u16(p, 2, 0, false) : xx_data_get_u16(p, 2, 0, true); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[1664]; bool little; unsigned count,width,height,i,j; uint32_t total,image,industry,user,starts[8]; uint64_t sizes[8],header_end; char label[48];
    if(!pm_read(f,0,h,sizeof(h)) || (xx_rt_memcmp(h,"SDPX",4) && xx_rt_memcmp(h,"XPDS",4))) return false;
    little=h[0]=='X'; total=xx_data_get_u32(h+16, 4, 0, !little); image=xx_data_get_u32(h+4, 4, 0, !little); industry=xx_data_get_u32(h+28, 4, 0, !little); user=xx_data_get_u32(h+32, 4, 0, !little);
    header_end=1664U+(uint64_t)industry+user;
    if((xx_rt_memcmp(h+8,"V1.0",4) && xx_rt_memcmp(h+8,"V2.0",4)) || xx_data_get_u32(h+24, 4, 0, !little)!=1664 || (industry!=0 && industry!=384) || user>1048576 || header_end>image || (image&3) || total<image || total>(uint64_t)pm_available(f) || xx_data_get_u32(h+660, 4, 0, !little)!=0xFFFFFFFFU) return false;
    count=dx_u16(h+770,little); width=xx_data_get_u32(h+772, 4, 0, !little); height=xx_data_get_u32(h+776, 4, 0, !little);
    if(dx_u16(h+768,little)>7 || !count || count>8 || !width || !height || width>32768 || height>32768 || (uint64_t)width*height>67108864) return false;
    if(!pm_add(f,s,"generic-header.bin",0,1664) || (industry && !pm_add(f,s,"industry-header.bin",1664,industry)) || (user && !pm_add(f,s,"user-data.bin",1664+industry,user))) return false;
    for(i=0;i<count;++i) {
        const uint8_t *e=h+780+72*i; unsigned descriptor=e[20],depth=e[23],packing=dx_u16(e+24,little),samples; uint32_t eol=xx_data_get_u32(e+32, 4, 0, !little),eoi=xx_data_get_u32(e+36, 4, 0, !little); uint64_t fields,row;
        if((pd && xx_pd_is_stopped(pd)) || xx_data_get_u32(e, 4, 0, !little) || dx_u16(e+26,little) || packing>2 || eol>1048576 || eoi>1048576) return false;
        if((descriptor>=1 && descriptor<=4) || descriptor==6 || descriptor==8) samples=1;
        else if(descriptor==50) samples=3;
        else if(descriptor==51 || descriptor==52) samples=4;
        else return false;
        if((depth!=8 && depth!=10 && depth!=12 && depth!=16) || ((depth==8 || depth==16) && packing)) return false;
        fields=(uint64_t)width*samples;
        if(depth==10 && packing) row=((fields+2U)/3U)*4U;
        else if(depth==12 && packing) row=fields*2U;
        else if(depth==16) row=fields*2U;
        else row=((fields*depth+31U)/32U)*4U;
        sizes[i]=(row+eol)*height+eoi; starts[i]=xx_data_get_u32(e+28, 4, 0, !little);
        if(starts[i]<image || (starts[i]&3) || starts[i]>total || sizes[i]>total-starts[i]) return false;
        for(j=0;j<i;++j) if((uint64_t)starts[i]<starts[j]+sizes[j] && (uint64_t)starts[j]<starts[i]+sizes[i]) return false;
        xx_rt_snprintf(label,sizeof(label),"element-%u-raster.dpx",i); if(!pm_add(f,s,label,starts[i],(int64_t)sizes[i])) return false;
    }
    s->size=total; return true;
}

void xx_dpx_init(xx_dpx *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_DPX,"dpx"); } }
xx_dpx *xx_dpx_create(xx_io_device *d,int64_t b) { xx_dpx *r=(xx_dpx *)xx_mem_alloc(sizeof(*r)); if(r) xx_dpx_init(r,d,b); return r; }
void xx_dpx_destroy(xx_dpx *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_dpx_free(xx_dpx *r) { if(r) { xx_dpx_destroy(r); xx_mem_free(r); } }
bool xx_dpx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_dpx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
