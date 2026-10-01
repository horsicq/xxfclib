/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/LibRaw/LibRaw/master/src/metadata/minolta.cpp */
#include "xxfclib/formats/minolta_mrw/xx_minolta_mrw.h"
#include "../xx_eighth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[8],p[24];uint64_t total=(uint64_t)pm_available(f),at=8,end,pixels=0,bytes=0;uint32_t tags[32],count=0,i;bool prd=false;char label[48];
    if(total>67108864 || !pm_read(f,0,h,8) || xx_rt_memcmp(h,"\0MRM",4) || !(end=8U+(uint64_t)pm_be32(h+4)) || end<=8 || end>=total || !pm_add(f,s,"mrw-header.bin",0,8)) return false;
    while(at<end) {uint32_t type,n;uint64_t start=at;
        if(count>=32 || !eh_take(f,&at,end,h,8,pd) || h[0] || !(n=pm_be32(h+4)) || !eh_span(at,n,end)) return false;type=pm_be32(h);for(i=0;i<count;++i) if(tags[i]==type) return false;tags[count++]=type;
        if(type==0x505244U) {uint32_t rows,columns,height,width;if(n<24 || !pm_read(f,(int64_t)at,p,24) || !(rows=pm_be16(p+8)) || !(columns=pm_be16(p+10)) || !(height=pm_be16(p+12)) || !(width=pm_be16(p+14)) || height>rows || width>columns || !p[17] || p[17]>p[16] || !fd_mul(rows,columns,&pixels) || pixels>16777216) return false;
            if(p[16]==12 && p[18]==0x59) {if(pixels&1 || !fd_mul(pixels,3,&bytes)) return false;bytes/=2;}
            else if(p[16]==16 && p[18]==0x52) {if(!fd_mul(pixels,2,&bytes)) return false;}else return false;prd=true;
        }else if(type==0x574247U) {if(n!=12) return false;}
        else if(type==0x524946U) {if(n<28) return false;}
        else if(type==0x545457U) {if(n<8 || !pm_read(f,(int64_t)at,p,8) || !((p[0]=='I' && p[1]=='I' && pm_le16(p+2)==42) || (p[0]=='M' && p[1]=='M' && pm_be16(p+2)==42))) return false;}
        else return false;
        xx_rt_snprintf(label,sizeof(label),"metadata-%u.bin",count-1);if(!pm_add(f,s,label,(int64_t)start,(int64_t)n+8)) return false;at+=n;
    }if(!prd || bytes!=total-end || !pm_add(f,s,"cfa.bin",(int64_t)end,(int64_t)bytes)) return false;s->size=(int64_t)total;return true;
}

void xx_minolta_mrw_init(xx_minolta_mrw *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MINOLTA_MRW,"minolta_mrw"); } }
xx_minolta_mrw *xx_minolta_mrw_create(xx_io_device *d,int64_t b) { xx_minolta_mrw *r=(xx_minolta_mrw *)xx_mem_alloc(sizeof(*r)); if(r) xx_minolta_mrw_init(r,d,b); return r; }
void xx_minolta_mrw_destroy(xx_minolta_mrw *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_minolta_mrw_free(xx_minolta_mrw *r) { if(r) { xx_minolta_mrw_destroy(r); xx_mem_free(r); } }
bool xx_minolta_mrw_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_minolta_mrw_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
