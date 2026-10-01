/* SPDX-License-Identifier: MIT. Private authenticated carrier/component helpers. */
#ifndef XX_SIXTH_WRAPPER_TABLE_H
#define XX_SIXTH_WRAPPER_TABLE_H
#include "../sfx_arc/xx_fifth_wrapper_table.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
static uint64_t w6_u64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
static uint32_t w6_u32(const uint8_t *p,bool be) { return be ? pm_be32(p):pm_le32(p); }
static uint16_t w6_u16(const uint8_t *p,bool be) { return be ? pm_be16(p):pm_le16(p); }
static uint32_t w6_crc(const uint8_t *p,size_t n) { return xx_crc32_calc(0U,p,n); }
static bool w6_crc_checked(const uint8_t *p,size_t n,uint32_t expected,xx_pd_struct *pd) {
    uint32_t crc=0U;size_t at=0U;
    while(at<n) { size_t part=n-at>4096U ? 4096U:n-at;
        if(wg_stop(pd)) return false;
        crc=xx_crc32_calc(crc,p+at,part);at+=part;
    }
    return !wg_stop(pd) && crc==expected;
}
/* ELF tables are independently bounded before any payload scan. PT_LOAD file
 * ranges and sections may overlap, as the ABI explicitly permits. NOBITS
 * sections carry no physical data. Extended count encodings are refused. */
static bool w6_elf(Abstractformat *f,int64_t *low,bool inside,xx_pd_struct *pd) {
    uint8_t h[64],row[64]; bool wide,be,load=false; uint16_t phn,shn,phs,shs,eh; uint64_t ph,sh,end,high; unsigned i; int64_t limit=pm_available(f);
    if(!pm_read(f,0,h,64) || xx_rt_memcmp(h,"\x7f" "ELF",4) || (h[4]!=1 && h[4]!=2) || (h[5]!=1 && h[5]!=2) || h[6]!=1) return false; wide=h[4]==2;be=h[5]==2;
    if((w6_u16(h+16,be)!=2 && w6_u16(h+16,be)!=3) || w6_u32(h+20,be)!=1) return false;
    eh=w6_u16(h+(wide ? 52:40),be);phs=w6_u16(h+(wide ? 54:42),be);phn=w6_u16(h+(wide ? 56:44),be);shs=w6_u16(h+(wide ? 58:46),be);shn=w6_u16(h+(wide ? 60:48),be);
    ph=wide ? w6_u64(h+32,be):w6_u32(h+28,be);sh=wide ? w6_u64(h+40,be):w6_u32(h+32,be);
    if(eh!=(wide ? 64:52) || !phn || phn>4096 || shn>4096 || phs!=(wide ? 56:32) || (shn && shs!=(wide ? 64:40)) || ph<eh || !wg_range(limit,ph,(uint64_t)phn*phs) || (shn && (sh<eh || !wg_range(limit,sh,(uint64_t)shn*shs)))) return false;
    high=ph+(uint64_t)phn*phs; if(shn && sh+(uint64_t)shn*shs>high) high=sh+(uint64_t)shn*shs;
    for(i=0;i<phn;++i) { uint64_t at,n,m; if(wg_stop(pd) || !pm_read(f,(int64_t)(ph+(uint64_t)i*phs),row,phs)) return false;
        at=wide ? w6_u64(row+8,be):w6_u32(row+4,be);n=wide ? w6_u64(row+32,be):w6_u32(row+16,be);m=wide ? w6_u64(row+40,be):w6_u32(row+20,be);
        if(!wg_range(limit,at,n) || (w6_u32(row,be)==1 && m<n)) return false; if(w6_u32(row,be)==1) load=true; end=at+n;if(end>high) high=end;
    }
    for(i=0;i<shn;++i) { uint64_t at,n; if(wg_stop(pd) || !pm_read(f,(int64_t)(sh+(uint64_t)i*shs),row,shs)) return false;
        if(!w6_u32(row+4,be) || w6_u32(row+4,be)==8) continue;at=wide ? w6_u64(row+24,be):w6_u32(row+16,be);n=wide ? w6_u64(row+32,be):w6_u32(row+20,be);
        if(!wg_range(limit,at,n)) return false;end=at+n;if(end>high) high=end;
    } if(!load) return false; *low=inside ? eh:(int64_t)high;return true;
}
static bool w6_carrier(Abstractformat *f,int64_t *low,bool inside,bool elf,xx_pd_struct *pd) {
    uint8_t h[64]; int64_t limit=pm_available(f); uint64_t image,headers; int64_t overlay,cab,cabend;
    if(!pm_read(f,0,h,28)) return false;
    if(elf && !xx_rt_memcmp(h,"\x7f" "ELF",4)) return w6_elf(f,low,inside,pd);
    if(h[0]==0x60 && h[1]==0x1a) return w5_carrier(f,false,low,pd);
    if((xx_rt_memcmp(h,"MZ",2) && xx_rt_memcmp(h,"ZM",2)) || !pm_read(f,0,h,64)) return false;
    headers=(uint64_t)pm_le16(h+8)*16;image=pm_le16(h+4);if(!image || pm_le16(h+2)>511 || headers<28) return false;
    image=(image-1)*512+(pm_le16(h+2) ? pm_le16(h+2):512);
    if(headers>image || image>(uint64_t)limit || (uint64_t)pm_le16(h+24)+4U*pm_le16(h+6)>headers) return false;
    *low=inside ? (int64_t)headers:(int64_t)image;
    if(h[0]=='M') { uint8_t magic[4];uint32_t pe=pm_le32(h+60);if(pe>=64 && pm_read(f,pe,magic,4) && !xx_rt_memcmp(magic,"PE\0\0",4)) { if(!wg_pe(f,&overlay,&cab,&cabend,pd)) return false;*low=inside ? 64:overlay; } }
    return *low<=limit;
}
typedef bool (*w6_at)(Abstractformat *,pm_stream *,int64_t,xx_pd_struct *);
static bool w6_scan_impl(Abstractformat *f,pm_stream *s,const uint8_t *sig,size_t siglen,int adjust,bool inside,bool elf,w6_at parse,bool first,xx_pd_struct *pd) {
    int64_t low,limit=pm_available(f);size_t n,i,done=0,capacity=xx_get_file_buffer_size();
    unsigned candidates=0;uint8_t *b;
    if(!siglen || !w6_carrier(f,&low,inside,elf,pd) || low>=limit) return false;
    n=limit-low>16777216 ? 16777216U:(size_t)(limit-low);if(capacity>n) capacity=n;
    b=(uint8_t *)xx_mem_alloc(capacity);if(!b) return false;
    /* The old locator validated this complete prefix before callbacks. */
    while(done<n) {
        size_t take=n-done>capacity ? capacity:n-done;
        if(wg_stop(pd) || !pm_read(f,low+(int64_t)done,b,take)) {xx_mem_free(b);return false;}
        done+=take;
    }
    xx_mem_free(b);
    for(i=0;i+siglen<=n;++i) {
        int64_t at,found=xx_io_find_bytes_buffer_optimize_ex(f->device,f->base_address+low+(int64_t)i,(int64_t)(n-i),sig,siglen,capacity,pd);
        if(found<0) break;i=(size_t)(found-f->base_address-low);at=low+(int64_t)i+adjust;
        if(++candidates>8) break;
        if(at>=low && parse(f,s,at,pd)) return true;if(s->count || first) break;
    }
    return false;
}

#define w6_scan(f,s,sig,n,a,i,e,parse,pd) w6_scan_impl(f,s,sig,n,a,i,e,parse,false,pd)
#define w6_first(f,s,sig,n,a,i,e,parse,pd) w6_scan_impl(f,s,sig,n,a,i,e,parse,true,pd)
static bool w6_component(Abstractformat *f,pm_stream *s,int64_t at,int64_t size,const char *label) { if(size<=0 || !pm_add(f,s,label,at,size)) return false;s->size=at+size;return true; }
#endif
