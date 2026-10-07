/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/algo/brotli/xx_brotli.h"
#include "xxfclib/data/xx_data.h"

static bool font_flavor(uint32_t n) { return n==0x10000 || n==0x4f54544f || n==0x74727565 || n==0x74797031; }
static bool font_tag(const uint8_t *p) { unsigned i; for(i=0;i<4;++i) if(p[i]<32 || p[i]>126) return false; return true; }
static uint32_t font_checksum(const uint8_t *p,size_t n,bool head) {
    uint32_t sum=0; size_t i,j; for(i=0;i<n;i+=4) { uint32_t word=0; for(j=0;j<4;++j) word=(word<<8)|((i+j<n && !(head && i+j>=8 && i+j<12)) ? p[i+j] : 0); sum+=word; } return sum;
}
static bool font_range(Abstractformat *f,pm_stream *s,int64_t at,uint32_t packed,uint32_t original,const char *name,int compression,uint32_t checksum,bool check,xx_pd_struct *pd) {
    uint8_t *in=NULL,*out=NULL; bool ok=false; size_t consumed=0,written=0; pm_member *m;
    uint64_t retained=0,ceiling=64U*1024U*1024U; size_t i;
    const xx_var *opt=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    for(i=0;i<s->count;++i) if(s->items[i].memory) retained+=(uint64_t)s->items[i].size;
    if(opt && xx_var_get_u64(opt)<ceiling) ceiling=xx_var_get_u64(opt);
    if(retained+(compression ? (uint64_t)packed+original : original)>ceiling) return false;
    if(packed>64U*1024U*1024U || original>64U*1024U*1024U || (opt && (uint64_t)packed+original>xx_var_get_u64(opt)) || (pd && xx_pd_is_stopped(pd))) return false;
    in=(uint8_t *)xx_mem_alloc(packed ? packed : 1); if(!in || !pm_read(f,at,in,packed)) goto done;
    if(compression) {
        out=(uint8_t *)xx_mem_alloc(original ? original : 1); if(!out) goto done;
        if(compression==1) {
            xx_io_device *dest;
            if(packed<6 || (in[0]&15)!=8 || in[0]>>4>7 || ((uint32_t)in[0]*256+in[1])%31 || (in[1]&32)) goto done;
            dest=xx_io_mem_open(out,original); if(!dest) goto done;
            ok=xx_deflate_unpack_memory_to_device_ex(in+2,packed-6,dest,&consumed,false,pd); written=(size_t)xx_io_tell(dest); xx_io_close(dest);
            ok=ok && consumed==packed-6 && written==original && xx_adler32_update(1,out,original)==xx_data_get_u32(in+packed-4, 4, 0, true);
        } else ok=xx_brotli_decompress_memory(in,packed,out,original,&written) && written==original;
        if(!ok) goto done;
    } else { if(packed!=original) goto done; out=in; in=NULL; }
    ok=false;
    if(check && font_checksum(out,original,!xx_rt_strcmp(name,"table-68656164.bin"))!=checksum) goto done;
    if(!pm_add(f,s,name,at,0)) goto done;
    m=&s->items[s->count-1]; m->memory=out; m->size=original; m->packed_size=packed; out=NULL; ok=true;
done: if(in) xx_mem_free(in); if(out) xx_mem_free(out); return ok && (!pd || !xx_pd_is_stopped(pd));
}
