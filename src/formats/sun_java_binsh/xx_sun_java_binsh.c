/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/installers/xbinshsfx.cpp
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/sun_java_binsh/xx_sun_java_binsh.h"
#include "../makeself/xx_fourth_wrapper_table.h"

static bool wg_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    size_t len; char *text=wg_shell(f,&len),*p; uint64_t line; int64_t offset,limit=pm_available(f); bool ok=false; uint8_t h[3];
    if(!text) return false;
    if(!xx_rt_strstr(text,"Java") || (!xx_rt_strstr(text,"SUN MICROSYSTEMS") && !xx_rt_strstr(text,"Sun Microsystems")) || xx_rt_strstr(text,"InstallAnywhere") || xx_rt_strstr(text,"Makeself")) goto done;
    p=xx_rt_strstr(text,"tail "); if(!p) goto done; p+=5; while(*p==' ' || *p=='\t') ++p;
    if(p[0]=='-' && p[1]=='n') { p+=2; while(*p==' ' || *p=='\t') ++p; }
    if(*p++!='+') { goto done; } { size_t n=0; while(p[n]>='0' && p[n]<='9') ++n; if(!wg_decimal(p,n,&line) || line<2) goto done; p+=n; }
    if(*p!=' ' && *p!='\t') { goto done; } while(*p==' ' || *p=='\t') ++p;
    if(xx_rt_strncmp(p,"$0",2) && xx_rt_strncmp(p,"\"$0\"",4) && xx_rt_strncmp(p,"${0}",4)) goto done;
    if(!wg_lines(f,line-1,&offset,pd) || offset<=(int64_t)(p-text) || limit-offset<512 || !pm_read(f,offset,h,3)) goto done;
    if(h[0]==31 && h[1]==157 && !(h[2]&96) && (h[2]&31)>=9 && (h[2]&31)<=16) ok=pm_add(f,s,"archive.tar.Z",offset,limit-offset);
    else if(wg_tar(f,offset,limit,pd)) ok=pm_add(f,s,"archive.tar",offset,limit-offset);
    if(ok) s->size=limit;
done: xx_mem_free(text); return ok;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return wg_parse(f,s,pd) && wg_members(s,pd); }
void xx_sun_java_binsh_init(xx_sun_java_binsh *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SUN_JAVA_BINSH,"sh"); } }
xx_sun_java_binsh *xx_sun_java_binsh_create(xx_io_device *d,int64_t b) { xx_sun_java_binsh *r=(xx_sun_java_binsh *)xx_mem_alloc(sizeof(*r)); if(r) xx_sun_java_binsh_init(r,d,b); return r; }
void xx_sun_java_binsh_destroy(xx_sun_java_binsh *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sun_java_binsh_free(xx_sun_java_binsh *r) { if(r) { xx_sun_java_binsh_destroy(r); xx_mem_free(r); } }
bool xx_sun_java_binsh_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sun_java_binsh_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
