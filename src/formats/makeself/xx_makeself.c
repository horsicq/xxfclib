/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/megastep/makeself/blob/master/makeself-header.sh
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/makeself/xx_makeself.h"
#include "../makeself/xx_fourth_wrapper_table.h"

static bool wg_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    size_t len,n,i=0; char *text=wg_shell(f,&len); const char *sizes,*p; uint64_t lines=0; int64_t offset,limit=pm_available(f); unsigned count=0; bool ok=false;
    if(!text) return false;
    if(!xx_rt_strstr(text,"This script was generated using Makeself") || !wg_assignment(text,len,"filesizes",&sizes,&n) || !n) goto done;
    if(!wg_number(text,len,"skip",&lines)) { p=xx_rt_strstr(text,"head -n "); if(!p) goto done; p+=8; { size_t k=0; while(p[k]>='0' && p[k]<='9') ++k; if(!wg_decimal(p,k,&lines)) goto done; } }
    if(!wg_lines(f,lines,&offset,pd)) goto done;
    while(i<n) { uint64_t bytes; size_t begin; uint8_t h[6]; const char *ext="tar"; char label[48];
        while(i<n && sizes[i]==' ') ++i; begin=i; while(i<n && sizes[i]>='0' && sizes[i]<='9') ++i;
        if(begin==i || !wg_decimal(sizes+begin,i-begin,&bytes) || !bytes || bytes<6 || (i<n && sizes[i]!=' ') || ++count>64 || !wg_range(limit,offset,bytes) || !pm_read(f,offset,h,6) || wg_stop(pd)) goto done;
        if(h[0]==31 && h[1]==139 && h[2]==8 && !(h[3]&224)) ext="tar.gz";
        else if(!xx_rt_memcmp(h,"BZh",3) && h[3]>='1' && h[3]<='9') ext="tar.bz2";
        else if(!xx_rt_memcmp(h,"\xfd""7zXZ\0",6)) ext="tar.xz";
        else if(h[0]==31 && h[1]==157 && !(h[2]&96) && (h[2]&31)>=9 && (h[2]&31)<=16) ext="tar.Z";
        else if(!wg_tar(f,offset,offset+(int64_t)bytes,pd)) goto done;
        xx_rt_snprintf(label,sizeof(label),"payload-%u.%s",count-1,ext); if(!pm_add(f,s,label,offset,(int64_t)bytes)) goto done; offset+=(int64_t)bytes;
    }
    if(!count) goto done; s->size=offset; ok=true;
done: xx_mem_free(text); return ok;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return wg_parse(f,s,pd) && wg_members(s,pd); }
void xx_makeself_init(xx_makeself *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MAKESELF,"run"); } }
xx_makeself *xx_makeself_create(xx_io_device *d,int64_t b) { xx_makeself *r=(xx_makeself *)xx_mem_alloc(sizeof(*r)); if(r) xx_makeself_init(r,d,b); return r; }
void xx_makeself_destroy(xx_makeself *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_makeself_free(xx_makeself *r) { if(r) { xx_makeself_destroy(r); xx_mem_free(r); } }
bool xx_makeself_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_makeself_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
