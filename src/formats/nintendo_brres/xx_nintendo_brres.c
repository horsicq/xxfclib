/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/libertyernie/brawltools/master/BrawlLib/SSBB/Types/BRES.cs
 * BRRES v0 big endian resource dictionary entries. Exports encoded resource sections; embedded string pool references retain their original offsets.
 */
#include "xxfclib/formats/nintendo_brres/xx_nintendo_brres.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static XXFC_MAYBE_UNUSED uint16_t r16(const uint8_t *p,bool be) { return be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false); }
static XXFC_MAYBE_UNUSED uint32_t r32(const uint8_t *p,bool be) { return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false); }
static XXFC_MAYBE_UNUSED uint64_t r64(const uint8_t *p,bool be) { return be ? ((uint64_t)xx_data_get_u32(p, 4, 0, true)<<32)|xx_data_get_u32(p+4, 4, 0, true) : ((uint64_t)xx_data_get_u32(p+4, 4, 0, false)<<32)|xx_data_get_u32(p, 4, 0, false); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[16],b[16],g[8],e[16],c; uint32_t total,root,root_size,groups,i,sections; int64_t master;
    if(!pm_read(f,0,h,16) || xx_rt_memcmp(h,"bres",4) || xx_data_get_u16(h+4, 2, 0, true)!=0xfeff || xx_data_get_u16(h+6, 2, 0, true)) return false;
    total=xx_data_get_u32(h+8, 4, 0, true); root=xx_data_get_u16(h+12, 2, 0, true); sections=xx_data_get_u16(h+14, 2, 0, true);
    if(total<48 || total>pm_available(f) || root<16 || root>total-8 || !sections || !pm_read(f,root,b,8) || xx_rt_memcmp(b,"root",4)) return false;
    root_size=xx_data_get_u32(b+4, 4, 0, true); if(root_size<32 || root_size>total-root) return false;
    master=root+8; if(!pm_read(f,master,g,8)) return false;
    groups=xx_data_get_u32(g+4, 4, 0, true); if(groups>4096 || xx_data_get_u32(g, 4, 0, true)<24+(uint64_t)groups*16 || xx_data_get_u32(g, 4, 0, true)>root_size-8) return false;
    for(i=0;i<groups;++i) {
        int64_t sub; uint32_t count,j,subsize;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,master+24+(int64_t)i*16,e,16)) return false;
        sub=master+(int32_t)xx_data_get_u32(e+12, 4, 0, true);
        if(sub<master+24+(int64_t)groups*16 || sub>root+root_size-24 || !pm_read(f,sub,g,8)) return false;
        count=xx_data_get_u32(g+4, 4, 0, true); subsize=xx_data_get_u32(g, 4, 0, true);
        if(count>65536 || subsize<24+(uint64_t)count*16 || subsize>root+root_size-sub) return false;
        for(j=0;j<count;++j) {
            int64_t at,str; uint32_t length,k; char label[40]; bool ended=false;
            if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,sub+24+(int64_t)j*16,e,16)) return false;
            at=sub+(int32_t)xx_data_get_u32(e+12, 4, 0, true); str=sub+(int32_t)xx_data_get_u32(e+8, 4, 0, true);
            if(str<16 || str>=total || at<root+root_size || at>total-16 || !pm_read(f,at,b,16)) return false;
            for(k=0;k<4096 && str+k<total;++k) { if(!pm_read(f,str+k,&c,1)) return false; if(!c) { ended=true; break; } }
            if(!ended) return false;
            length=xx_data_get_u32(b+4, 4, 0, true);
            if(length<16 || length>total-at || (int32_t)xx_data_get_u32(b+12, 4, 0, true)!=-at) return false;
            for(k=0;k<4;++k) if(b[k]<0x20 || b[k]>0x7e) return false;
            xx_rt_snprintf(label,sizeof(label),"resource-%u.bin",(unsigned)s->count);
            if(!pm_add(f,s,label,at,length)) return false;
        }
    }
    if(s->count+1!=sections) return false;
    s->size=total; return true;
}

void xx_nintendo_brres_init(xx_nintendo_brres *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_BRRES,"brres"); } }
xx_nintendo_brres *xx_nintendo_brres_create(xx_io_device *d,int64_t b) { xx_nintendo_brres *r=(xx_nintendo_brres *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_brres_init(r,d,b); return r; }
void xx_nintendo_brres_destroy(xx_nintendo_brres *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_brres_free(xx_nintendo_brres *r) { if(r) { xx_nintendo_brres_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_brres_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_brres_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
