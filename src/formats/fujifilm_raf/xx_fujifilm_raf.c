/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/Exiv2/exiv2/main/src/rafimage.cpp */
#include "xxfclib/formats/fujifilm_raf/xx_fujifilm_raf.h"
#include "../xx_eighth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[148],p[4];uint64_t at,end,total=(uint64_t)pm_available(f),offsets[3],sizes[3],first;uint16_t tags[1024];uint32_t count,i,j;
    if(total>67108864 || total<108 || !pm_read(f,0,h,108) || xx_rt_memcmp(h,"FUJIFILMCCD-RAW ",16)) return false;
    for(i=16;i<20;++i) if(h[i]<'0' || h[i]>'9') return false;
    first=total;for(i=0;i<3;++i) {offsets[i]=xx_data_get_u32(h+84+i*8, 4, 0, true);sizes[i]=xx_data_get_u32(h+88+i*8, 4, 0, true);if(offsets[i]<108 || !sizes[i] || !eh_span(offsets[i],sizes[i],total)) return false;if(offsets[i]<first) first=offsets[i];for(j=0;j<i;++j) if(!eh_no_overlap(offsets[i],sizes[i],offsets[j],sizes[j])) return false;}
    if(first>65536 || (first>=148 && (!pm_read(f,0,h,148) || xx_data_get_u32(h+120, 4, 0, true) || xx_data_get_u32(h+124, 4, 0, true) || xx_data_get_u32(h+128, 4, 0, true) || xx_data_get_u32(h+132, 4, 0, true) || xx_data_get_u32(h+136, 4, 0, true) || xx_data_get_u32(h+140, 4, 0, true) || xx_data_get_u32(h+144, 4, 0, true))) || !eh_jpeg(f,offsets[0],offsets[0]+sizes[0],pd)) return false;
    at=offsets[1];end=at+sizes[1];if(!eh_take(f,&at,end,p,4,pd) || !(count=xx_data_get_u32(p, 4, 0, true)) || count>1024) return false;
    for(i=0;i<count;++i) {uint32_t n;if(!eh_take(f,&at,end,p,4,pd) || !(n=xx_data_get_u16(p+2, 2, 0, true)) || !eh_span(at,n,end)) return false;tags[i]=xx_data_get_u16(p, 2, 0, true);for(j=0;j<i;++j) if(tags[j]==tags[i]) return false;
        if(tags[i]==0x100) {if(n!=4 || !pm_read(f,(int64_t)at,p,4) || !xx_data_get_u16(p, 2, 0, true) || !xx_data_get_u16(p+2, 2, 0, true)) return false;}at+=n;
    }if(at!=end || !pm_add(f,s,"raf-header.bin",0,(int64_t)first) || !pm_add(f,s,"preview.jpg",(int64_t)offsets[0],(int64_t)sizes[0]) || !pm_add(f,s,"raf-metadata.bin",(int64_t)offsets[1],(int64_t)sizes[1]) || !pm_add(f,s,"cfa.bin",(int64_t)offsets[2],(int64_t)sizes[2])) return false;
    end=0;for(i=0;i<3;++i) if(offsets[i]+sizes[i]>end) end=offsets[i]+sizes[i];if(end!=total) return false;s->size=(int64_t)end;return true;
}

void xx_fujifilm_raf_init(xx_fujifilm_raf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_FUJIFILM_RAF,"fujifilm_raf"); } }
xx_fujifilm_raf *xx_fujifilm_raf_create(xx_io_device *d,int64_t b) { xx_fujifilm_raf *r=(xx_fujifilm_raf *)xx_mem_alloc(sizeof(*r)); if(r) xx_fujifilm_raf_init(r,d,b); return r; }
void xx_fujifilm_raf_destroy(xx_fujifilm_raf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_fujifilm_raf_free(xx_fujifilm_raf *r) { if(r) { xx_fujifilm_raf_destroy(r); xx_mem_free(r); } }
bool xx_fujifilm_raf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_fujifilm_raf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
