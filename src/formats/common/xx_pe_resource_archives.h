/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XX_PE_RESOURCE_ARCHIVES_PRIVATE_H
#define XX_PE_RESOURCE_ARCHIVES_PRIVATE_H
#include "xx_pe_resource_locator.h"
typedef bool (*xx_sfx_resource_archive_parser)(Abstractformat *,pm_stream *,int64_t,uint32_t,xx_pd_struct *);
/* SPDX-License-Identifier: MIT. Private bounded PE resource enumeration. */
static bool xx_sfx_pe_resource_archives(Abstractformat *f, pm_stream *s, const uint8_t *signature, size_t signature_size, xx_sfx_resource_archive_parser parse, xx_pd_struct *pd)
{
    uint8_t dos[64], coff[24], opt[136], row[40], entry[8], leaf[16], magic[32];
    xx_sfx_resource_section sections[96];
    int64_t total=pm_available(f), root, image_end, cabinet, cabinet_end;
    uint32_t pe, rva, size; uint16_t count, opt_size, kind;
    unsigned n0, visits=0;
    if (!signature || !signature_size || signature_size>32 || !parse || carrier_stop(pd) || !carrier_pe(f,&image_end,&cabinet,&cabinet_end,pd) || !pm_read(f,0,dos,sizeof(dos))) return false;
    pe=xx_data_get_u32(dos+60,4,0,false);
    if (!pm_read(f,pe,coff,sizeof(coff))) return false;
    count=xx_data_get_u16(coff+6,2,0,false); opt_size=xx_data_get_u16(coff+20,2,0,false);
    if (!count || count>96 || opt_size<120 || !pm_read(f,(int64_t)pe+24,opt,120)) return false;
    kind=xx_data_get_u16(opt,2,0,false);
    if (kind==0x10b) { rva=xx_data_get_u32(opt+112,4,0,false); size=xx_data_get_u32(opt+116,4,0,false); }
    else if (kind==0x20b && opt_size>=136 && pm_read(f,(int64_t)pe+24,opt,136)) {
        rva=xx_data_get_u32(opt+128,4,0,false); size=xx_data_get_u32(opt+132,4,0,false);
    } else return false;
    if (!rva || size<16) return false;
    for(unsigned i=0;i<count;i++) {
        if (carrier_stop(pd) || !pm_read(f,(int64_t)pe+24+opt_size+(int64_t)i*40,row,40)) return false;
        sections[i].va=xx_data_get_u32(row+12,4,0,false); sections[i].raw=xx_data_get_u32(row+20,4,0,false);
        sections[i].raw_size=xx_data_get_u32(row+16,4,0,false);
    }
    if (!xx_sfx_resource_raw(sections,count,total,rva,size,&root) || !xx_sfx_resource_table(f,root,size,0,&n0)) return false;
    for(unsigned i=0;i<n0;i++) {
        unsigned n1; uint32_t a;
        if (carrier_stop(pd) || ++visits>16384 || !pm_read(f,root+16+(int64_t)i*8,entry,8)) return false;
        a=xx_data_get_u32(entry+4,4,0,false);
        if (!(a&0x80000000U) || !xx_sfx_resource_table(f,root,size,a&0x7fffffffU,&n1)) continue;
        for(unsigned j=0;j<n1;j++) {
            unsigned n2; uint32_t b;
            if (carrier_stop(pd) || ++visits>16384 || !pm_read(f,root+(a&0x7fffffffU)+16+(int64_t)j*8,entry,8)) return false;
            b=xx_data_get_u32(entry+4,4,0,false);
            if (!(b&0x80000000U) || !xx_sfx_resource_table(f,root,size,b&0x7fffffffU,&n2)) continue;
            for(unsigned k=0;k<n2;k++) {
                uint32_t c, bytes, data_rva; int64_t at; bool duplicate=false;
                if (carrier_stop(pd) || ++visits>16384 || !pm_read(f,root+(b&0x7fffffffU)+16+(int64_t)k*8,entry,8)) return false;
                c=xx_data_get_u32(entry+4,4,0,false);
                if ((c&0x80000000U) || c>size || size-c<16 || !pm_read(f,root+c,leaf,16)) continue;
                data_rva=xx_data_get_u32(leaf,4,0,false); bytes=xx_data_get_u32(leaf+4,4,0,false);
                if (bytes<signature_size || !xx_sfx_resource_raw(sections,count,total,data_rva,bytes,&at) || !pm_read(f,at,magic,signature_size) ||
                    xx_rt_memcmp(magic,signature,signature_size)) continue;
                for(size_t q=0;q<s->count;q++) if(s->items[q].offset==f->base_address+at) duplicate=true;
                if (duplicate) continue;
                if (s->count>=128 || !parse(f,s,at,bytes,pd)) return false;
            }
        }
    }
    if(s->count && s->size<image_end) s->size=image_end;
    return s->count>0 && !carrier_stop(pd);
}

#endif
