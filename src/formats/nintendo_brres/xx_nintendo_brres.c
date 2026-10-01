/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/libertyernie/brawltools/master/BrawlLib/SSBB/Types/BRES.cs
 * BRRES v0 big endian resource dictionary entries. Exports encoded resource sections; embedded string pool references retain their original offsets.
 */
#include "xxfclib/formats/nintendo_brres/xx_nintendo_brres.h"
#include "../xx_payload_members.h"

static uint16_t r16(const uint8_t *p,bool be) { return be ? pm_be16(p) : pm_le16(p); }
static uint32_t r32(const uint8_t *p,bool be) { return be ? pm_be32(p) : pm_le32(p); }
static uint64_t r64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[16],b[16],g[8],e[16],c; uint32_t total,root,root_size,groups,i,sections; int64_t master;
    if(!pm_read(f,0,h,16) || xx_rt_memcmp(h,"bres",4) || pm_be16(h+4)!=0xfeff || pm_be16(h+6)) return false;
    total=pm_be32(h+8); root=pm_be16(h+12); sections=pm_be16(h+14);
    if(total<48 || total>pm_available(f) || root<16 || root>total-8 || !sections || !pm_read(f,root,b,8) || xx_rt_memcmp(b,"root",4)) return false;
    root_size=pm_be32(b+4); if(root_size<32 || root_size>total-root) return false;
    master=root+8; if(!pm_read(f,master,g,8)) return false;
    groups=pm_be32(g+4); if(groups>4096 || pm_be32(g)<24+(uint64_t)groups*16 || pm_be32(g)>root_size-8) return false;
    for(i=0;i<groups;++i) {
        int64_t sub; uint32_t count,j,subsize;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,master+24+(int64_t)i*16,e,16)) return false;
        sub=master+(int32_t)pm_be32(e+12);
        if(sub<master+24+(int64_t)groups*16 || sub>root+root_size-24 || !pm_read(f,sub,g,8)) return false;
        count=pm_be32(g+4); subsize=pm_be32(g);
        if(count>65536 || subsize<24+(uint64_t)count*16 || subsize>root+root_size-sub) return false;
        for(j=0;j<count;++j) {
            int64_t at,str; uint32_t length,k; char label[40]; bool ended=false;
            if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,sub+24+(int64_t)j*16,e,16)) return false;
            at=sub+(int32_t)pm_be32(e+12); str=sub+(int32_t)pm_be32(e+8);
            if(str<16 || str>=total || at<root+root_size || at>total-16 || !pm_read(f,at,b,16)) return false;
            for(k=0;k<4096 && str+k<total;++k) { if(!pm_read(f,str+k,&c,1)) return false; if(!c) { ended=true; break; } }
            if(!ended) return false;
            length=pm_be32(b+4);
            if(length<16 || length>total-at || (int32_t)pm_be32(b+12)!=-at) return false;
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
