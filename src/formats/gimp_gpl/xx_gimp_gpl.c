/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/GNOME/gimp/master/app/core/gimppalette-load.c
 * GIMP GPL UTF8 named RGB8 palettes: complete header, optional columns and exact bounded color rows. Original descriptor and color records exported; rendering unsupported.
 * Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/gimp_gpl/xx_gimp_gpl.h"
#include "../gimp_gpl/xx_twelfth_b.h"
static bool tb_quick(Abstractformat *f,uint64_t n) {uint8_t b[12];return n>=12&&pm_read(f,0,b,12)&&tb_tag(b,"GIMP Palette",12);}
static bool tb_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 tb_text q={b,0,n,0,0,0};bool name=false,columns=false,data=false;uint32_t colors=0;uint64_t section=0;char label[48];
 if(!tb_utf(b,n,false,pd)||!tb_line(&q)||!tb_word(&q,"GIMP")||!tb_word(&q,"Palette")||!tb_done(&q))return false;
 while(q.p<n){int32_t rgb[3];unsigned i;if(tb_stop(pd)||!tb_line(&q))return false;if(tb_done(&q))continue;
  if(!data&&tb_word(&q,"Name:")){tb_space(&q);if(name||q.t==q.stop||q.stop-q.t>4096)return false;name=true;continue;}
  if(!data&&tb_word(&q,"Columns:")){int32_t v;if(columns||!tb_i(&q,&v)||v<0||v>256||!tb_done(&q))return false;columns=true;continue;}
  if(!data){section=q.start;if(!tb_emit(f,s,"descriptor.gpl",0,section,n))return false;data=true;}
  for(i=0;i<3;++i)if(!tb_i(&q,&rgb[i])||rgb[i]<0||rgb[i]>255)return false;
  tb_space(&q);if(q.stop-q.t>4096||++colors>4094)return false;
  xx_rt_snprintf(label,sizeof(label),"color-%u.gpl",colors-1);
  if(!tb_emit(f,s,label,q.start,q.p-q.start,n))return false;section=q.p;
 }
 if(!data||!name||!colors)return false;
 /* Retain intervening comments as metadata as well as every color row. */
 {size_t i,count=s->count;uint64_t covered=0;for(i=0;i<count;++i){uint64_t p=(uint64_t)(s->items[i].offset-f->base_address),z=(uint64_t)s->items[i].size;if(p>covered&&!tb_emit(f,s,"comments.gpl",covered,p-covered,n))return false;covered=p+z;}if(covered<n&&!tb_emit(f,s,"comments.gpl",covered,n-covered,n))return false;}
 s->size=(int64_t)n;return true;
}

void xx_gimp_gpl_init(xx_gimp_gpl *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_GIMP_GPL,"gpl");}}
xx_gimp_gpl *xx_gimp_gpl_create(xx_io_device *d,int64_t at) {xx_gimp_gpl *r=(xx_gimp_gpl *)xx_mem_alloc(sizeof(*r));if(r)xx_gimp_gpl_init(r,d,at);return r;}
void xx_gimp_gpl_destroy(xx_gimp_gpl *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_gimp_gpl_free(xx_gimp_gpl *r) {if(r){xx_gimp_gpl_destroy(r);xx_mem_free(r);}}
bool xx_gimp_gpl_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_gimp_gpl_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
