/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://www.microfocus.com/documentation/amc-archive/infoconnect-16-2/pdfdoc/infoconnect-help/infoconnect-help.pdf
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/hp3000_wrq/xx_hp3000_wrq.h"
#include "../sfx_arc/xx_fifth_wrapper_table.h"

static bool w5_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    static const char *keys[6]={"RECSIZE=","BLOCKFACTOR=","CODE=","EXTENTS=","FILESIZE=","FORMAT="}; uint8_t h[512]; size_t n,p=0,i; uint64_t v[5]; int64_t limit=pm_available(f); uint64_t bytes;
    if(limit<64 || wg_stop(pd)) return false; n=limit>512 ? 512U : (size_t)limit; if(!pm_read(f,0,h,n)) return false;
    for(i=0;i<6;++i) { size_t k=xx_rt_strlen(keys[i]),start; if(k>n-p || xx_rt_memcmp(h+p,keys[i],k)) return false; p+=k;
        if(i==5) { if(p>=n || h[p++]!='F') return false; break; } start=p; while(p<n && h[p]>='0' && h[p]<='9') ++p;
        if(!wg_decimal((const char *)h+start,p-start,&v[i]) || p>=n || h[p++]!=';') return false;
    }
    if(!v[0] || v[0]>1048576 || !v[1] || v[1]>65535 || v[2]>32767 || !v[3] || v[3]>65535 || !v[4] || v[4]>(uint64_t)INT64_MAX/v[0]) return false;
    bytes=v[0]*v[4]; if(bytes!=(uint64_t)(limit-(int64_t)p) || !pm_add(f,s,"host-file.bin",(int64_t)p,(int64_t)bytes)) return false; s->size=limit; return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w5_parse(f,s,pd) && wg_members(s,pd); }
void xx_hp3000_wrq_init(xx_hp3000_wrq *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_HP3000_WRQ,"wrq"); } }
xx_hp3000_wrq *xx_hp3000_wrq_create(xx_io_device *d,int64_t b) { xx_hp3000_wrq *r=(xx_hp3000_wrq *)xx_mem_alloc(sizeof(*r)); if(r) xx_hp3000_wrq_init(r,d,b); return r; }
void xx_hp3000_wrq_destroy(xx_hp3000_wrq *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_hp3000_wrq_free(xx_hp3000_wrq *r) { if(r) { xx_hp3000_wrq_destroy(r); xx_mem_free(r); } }
bool xx_hp3000_wrq_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_hp3000_wrq_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
