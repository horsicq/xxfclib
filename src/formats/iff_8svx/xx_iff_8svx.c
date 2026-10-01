/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/iff_8svx/xx_iff_8svx.h"
#include "../tracker_liquid/xx_eighth_components.h"
#ifndef IFF_8SVX
#define XX_FILE_TYPE_IFF_8SVX ((xx_file_type_t)819)
#endif
static bool e8_parse(e8_blob*c) {
 size_t p=12;unsigned seen=0;uint64_t expected=0,body=0;
 if(!e8_eq(c,0,"FORM",4) || !e8_eq(c,8,"8SVX",4) || pm_be32(c->b+4)!=c->n-8 || !e8_add(c,"header.bin",0,12))return false;
 while(p<c->n){uint32_t z;size_t at=p+8;char name[24];if(!e8_range(c,p,8) || (z=pm_be32(c->b+p+4))>c->n-at)return false;
  if(e8_eq(c,p,"VHDR",4)){if(seen&1 || z!=20 || !pm_be16(c->b+at+12) || c->b[at+14]!=1 || c->b[at+15] || pm_be32(c->b+at+16)>65536)return false;seen|=1;expected=(uint64_t)pm_be32(c->b+at)+pm_be32(c->b+at+4);if(!expected)return false;}
  else if(e8_eq(c,p,"BODY",4)){if(seen&2 || !z)return false;seen|=2;body=z;}else if(e8_eq(c,p,"CHAN",4)){if(seen&4 || z!=4 || pm_be32(c->b+at)!=2)return false;seen|=4;}else if(!e8_eq(c,p,"NAME",4) && !e8_eq(c,p,"AUTH",4) && !e8_eq(c,p,"ANNO",4) && !e8_eq(c,p,"(c) ",4))return false;
  xx_rt_snprintf(name,sizeof(name),"%.4s.bin",c->b+p);if(!e8_add(c,name,p,8U+z))return false;p=at+z;if(z&1){if(!e8_zero(c,p,1))return false;++p;}
 }return p==c->n && (seen&3)==3 && body==expected;
}
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {return e8_loaded(f,s,pd,e8_parse);}
void xx_iff_8svx_init(xx_iff_8svx*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_IFF_8SVX,"bin");}}
xx_iff_8svx*xx_iff_8svx_create(xx_io_device*d,int64_t b) {xx_iff_8svx*r=(xx_iff_8svx*)xx_mem_alloc(sizeof(*r));if(r)xx_iff_8svx_init(r,d,b);return r;}
void xx_iff_8svx_destroy(xx_iff_8svx*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_iff_8svx_free(xx_iff_8svx*r) {if(r){xx_iff_8svx_destroy(r);xx_mem_free(r);}}
bool xx_iff_8svx_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_iff_8svx_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
