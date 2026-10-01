/* SPDX-License-Identifier: MIT
 * Independently implemented from https://tecplot.azureedge.net/products/360/2024r1m1/360-data-format.html */
#include "xxfclib/formats/tecplot_plt/xx_tecplot_plt.h"
#include "../xx_eighth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t p[48];uint64_t at=12,total=(uint64_t)pm_available(f),counts[32],points=0,header;uint32_t vars,zones=0,word,i,j;bool be;char label[64];
    if(total>67108864 || !pm_read(f,0,p,12) || xx_rt_memcmp(p,"#!TDV112",8)) return false;be=pm_be32(p+8)==1;
    if(fd_u32(p+8,be)!=1 || !eh_word(f,&at,total,be,&word,pd) || word || !eh_i32_string(f,&at,total,be,true,pd) || !eh_word(f,&at,total,be,&vars,pd) || !vars || vars>64) return false;
    for(i=0;i<vars;++i) if(!eh_i32_string(f,&at,total,be,false,pd)) return false;
    while(true) {uint64_t count=1;if(!eh_word(f,&at,total,be,&word,pd)) return false;if(word==0x43b28000U) break;
        if(word!=0x43958000U || zones>=32 || !eh_i32_string(f,&at,total,be,false,pd) || !eh_take(f,&at,total,p,48,pd) || fd_u32(p,be)!=UINT32_MAX || (int32_t)fd_u32(p+4,be)< -2 || !sv_finite64(sv_u64(p+8,be)) || fd_u32(p+16,be)!=UINT32_MAX || fd_u32(p+20,be) || fd_u32(p+24,be) || fd_u32(p+28,be) || fd_u32(p+32,be)) return false;
        for(i=36;i<48;i+=4) if(!fd_u32(p+i,be) || !fd_mul(count,fd_u32(p+i,be),&count) || count>1048576) return false;
        if(!eh_word(f,&at,total,be,&word,pd) || word || points>1048576-count) return false;points+=count;counts[zones++]=count;
    }header=at;if(!zones || !pm_add(f,s,"tecplot-header.bin",0,(int64_t)header)) return false;
    for(i=0;i<zones;++i) {uint32_t types[64];uint64_t start=at,data_header;
        if(!eh_word(f,&at,total,be,&word,pd) || word!=0x43958000U) return false;
        for(j=0;j<vars;++j) if(!eh_word(f,&at,total,be,types+j,pd) || !types[j] || types[j]>5) return false;
        if(!eh_word(f,&at,total,be,&word,pd) || word || !eh_word(f,&at,total,be,&word,pd) || word || !eh_word(f,&at,total,be,&word,pd) || word!=UINT32_MAX) return false;
        for(j=0;j<vars;++j) {uint64_t lo,hi;if(!eh_take(f,&at,total,p,16,pd) || !sv_finite64(lo=sv_u64(p,be)) || !sv_finite64(hi=sv_u64(p+8,be)) || sv_ordered64(lo)>sv_ordered64(hi)) return false;}
        data_header=at;xx_rt_snprintf(label,sizeof(label),"zone-%u-data-header.bin",i);if(!pm_add(f,s,label,(int64_t)start,(int64_t)(data_header-start))) return false;
        for(j=0;j<vars;++j) {uint64_t bytes;unsigned width=types[j]==2 ? 8:types[j]==4 ? 2:types[j]==5 ? 1:4;
            if(!fd_mul(counts[i],width,&bytes) || !eh_span(at,bytes,total) || (types[j]<=2 && !eh_float_array(f,at,bytes,width,be,pd))) return false;
            xx_rt_snprintf(label,sizeof(label),"zone-%u-variable-%u.bin",i,j);if(!pm_add(f,s,label,(int64_t)at,(int64_t)bytes)) return false;at+=bytes;
        }
    }if(at!=total) return false;s->size=(int64_t)at;return true;
}

void xx_tecplot_plt_init(xx_tecplot_plt *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TECPLOT_PLT,"tecplot_plt"); } }
xx_tecplot_plt *xx_tecplot_plt_create(xx_io_device *d,int64_t b) { xx_tecplot_plt *r=(xx_tecplot_plt *)xx_mem_alloc(sizeof(*r)); if(r) xx_tecplot_plt_init(r,d,b); return r; }
void xx_tecplot_plt_destroy(xx_tecplot_plt *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tecplot_plt_free(xx_tecplot_plt *r) { if(r) { xx_tecplot_plt_destroy(r); xx_mem_free(r); } }
bool xx_tecplot_plt_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tecplot_plt_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
