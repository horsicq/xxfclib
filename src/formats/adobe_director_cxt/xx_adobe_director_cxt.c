/* SPDX-License-Identifier: MIT.
 * Director RIFX/XFIR chunk framing: https://github.com/scummvm/scummvm/blob/master/engines/director/archive.cpp
 * Exposes stored cast and sound chunks without deprotecting them.
 */
#include "xxfclib/formats/adobe_director_cxt/xx_adobe_director_cxt.h"
#include "../xx_fifth_data.h"
#ifndef ADOBE_DIRECTOR_CXT
#define XX_FILE_TYPE_ADOBE_DIRECTOR_CXT ((xx_file_type_t)1510)
#endif
static bool is_form(const uint8_t*p,bool be) {
    static const char*const forms_be[]={"MV93","FGDM","MC95","FGDC"};
    static const char*const forms_le[]={"39VM","MDGF","59CM","CDGF"};
    unsigned i;const char*const*forms=be?forms_be:forms_le;
    for(i=0;i<4;++i)if(!xx_rt_memcmp(p,forms[i],4))return true;
    return false;
}
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {
    uint8_t h[12],c[8];uint64_t p,total;unsigned chunks=0,seen=0;bool be;int64_t n;
    if(fd_stop(pd)||(n=pm_available(f))<12 || !pm_read(f,0,h,12))return false;
    if(!xx_rt_memcmp(h,"RIFX",4))be=true;
    else if(!xx_rt_memcmp(h,"XFIR",4))be=false;
    else return false;
    total=8U+(uint64_t)xx_data_get_u32(h+4, 4, 0, be);
    if(total<12 || total>(uint64_t)n || !is_form(h+8,be) ||
       !pm_add(f,s,"rifx-header.bin",0,12))return false;
    p=12;
    while(p<total) {
        uint32_t size;uint64_t next;char label[48],tag[5];unsigned i;
        if(fd_stop(pd)||++chunks>4096 || !fd_range(p,8,total) ||
           !pm_read(f,(int64_t)p,c,8))return false;
        size=xx_data_get_u32(c+4, 4, 0, be);next=p+8U+(uint64_t)size+(size&1U);
        if(next>total)return false;
        if(!xx_rt_memcmp(c,"CAS",3))seen|=1U;
        if(!xx_rt_memcmp(c,"KEY*",4))seen|=2U;
        if(!xx_rt_memcmp(c,"snd",3))seen|=4U;
        for(i=0;i<4;++i)tag[i]=(c[i]>=32 && c[i]<=126 && c[i]!='*' &&
                                c[i]!='/' && c[i]!='\\')?(char)c[i]:'_';
        tag[4]=0;
        xx_rt_snprintf(label,sizeof(label),"%s.chunk",tag);
        if(!pm_add(f,s,label,(int64_t)p,8+(int64_t)size))return false;
        p=next;
    }
    if(p!=total || seen!=7U)return false;
    s->size=(int64_t)total;return true;
}
void xx_adobe_director_cxt_init(xx_adobe_director_cxt*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ADOBE_DIRECTOR_CXT,"cxt");}}
xx_adobe_director_cxt*xx_adobe_director_cxt_create(xx_io_device*d,int64_t b) {xx_adobe_director_cxt*r=(xx_adobe_director_cxt*)xx_mem_alloc(sizeof(*r));if(r)xx_adobe_director_cxt_init(r,d,b);return r;}
void xx_adobe_director_cxt_destroy(xx_adobe_director_cxt*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_adobe_director_cxt_free(xx_adobe_director_cxt*r) {if(r){xx_adobe_director_cxt_destroy(r);xx_mem_free(r);}}
bool xx_adobe_director_cxt_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_adobe_director_cxt_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
