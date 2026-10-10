/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/iff_8svx/xx_iff_8svx.h"
#include "../common/xx_audio_components.h"
#ifndef IFF_8SVX
#define XX_FILE_TYPE_IFF_8SVX ((xx_file_type_t)819)
#endif
static bool audio_component_parse(audio_component_blob*c) {
 size_t p=12;unsigned seen=0;uint64_t expected=0,body=0;
 if(!audio_component_eq(c,0,"FORM",4) || !audio_component_eq(c,8,"8SVX",4) || xx_data_get_u32(c->b+4, 4, 0, true)!=c->n-8 || !audio_component_add(c,"header.bin",0,12))return false;
 while(p<c->n){uint32_t z;size_t at=p+8;char name[24];if(!audio_component_range(c,p,8) || (z=xx_data_get_u32(c->b+p+4, 4, 0, true))>c->n-at)return false;
  if(audio_component_eq(c,p,"VHDR",4)){if(seen&1 || z!=20 || !xx_data_get_u16(c->b+at+12, 2, 0, true) || c->b[at+14]!=1 || c->b[at+15] || xx_data_get_u32(c->b+at+16, 4, 0, true)>65536)return false;seen|=1;expected=(uint64_t)xx_data_get_u32(c->b+at, 4, 0, true)+xx_data_get_u32(c->b+at+4, 4, 0, true);if(!expected)return false;}
  else if(audio_component_eq(c,p,"BODY",4)){if(seen&2 || !z)return false;seen|=2;body=z;}else if(audio_component_eq(c,p,"CHAN",4)){if(seen&4 || z!=4 || xx_data_get_u32(c->b+at, 4, 0, true)!=2)return false;seen|=4;}else if(!audio_component_eq(c,p,"NAME",4) && !audio_component_eq(c,p,"AUTH",4) && !audio_component_eq(c,p,"ANNO",4) && !audio_component_eq(c,p,"(c) ",4))return false;
  xx_rt_snprintf(name,sizeof(name),"%.4s.bin",c->b+p);if(!audio_component_add(c,name,p,8U+z))return false;p=at+z;if(z&1){if(!audio_component_zero(c,p,1))return false;++p;}
 }return p==c->n && (seen&3)==3 && body==expected;
}
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {return audio_component_loaded(f,s,pd,audio_component_parse);}
void xx_iff_8svx_init(xx_iff_8svx*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_IFF_8SVX,"bin");}}
xx_iff_8svx*xx_iff_8svx_create(xx_io_device*d,int64_t b) {xx_iff_8svx*r=(xx_iff_8svx*)xx_mem_alloc(sizeof(*r));if(r)xx_iff_8svx_init(r,d,b);return r;}
void xx_iff_8svx_destroy(xx_iff_8svx*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_iff_8svx_free(xx_iff_8svx*r) {if(r){xx_iff_8svx_destroy(r);xx_mem_free(r);}}
bool xx_iff_8svx_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_iff_8svx_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
