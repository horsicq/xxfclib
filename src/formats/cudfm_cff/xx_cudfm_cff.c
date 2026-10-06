/* SPDX-License-Identifier: MIT. Original music framing derived independently from primary AdPlug loader. */
#include "xxfclib/formats/cudfm_cff/xx_cudfm_cff.h"
#include "../xx_sixteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 m16_blob b={0};uint64_t base=32,at,stride=1728;uint32_t np,i;bool end=false,ok=false;
 M16_NEED(m16_load(f,&b,pd)&&m16_tag(&b,0,"<CUD-FM-File>\032\336\340",16)&&m16_span(&b,0,32)&&b.p[16]==1&&!b.p[19]&&m16_zero(&b,20,12)&&pm_le16(b.p+17)==b.n-32&&m16_span(&b,base,0x669));np=b.p[base+0x5e0];
 M16_NEED(np&&np<=36&&b.n-base==0x669+(uint64_t)np*stride&&m16_tag(&b,base+0x5e1,"CUD-FM-File - SEND A POSTCARD -",31)&&m16_emit(f,s,&b,"descriptor.cff",0,32));
 for(i=0;i<47;++i) {M16_NEED(m16_emit(f,s,&b,"instrument.cff",base+i*32,32)); } M16_NEED(m16_emit(f,s,&b,"song-info.cff",base+0x5e0,72));
 for(i=0;i<64;++i){uint8_t c=b.p[base+0x628+i];if(c&128)end=true;else M16_NEED(!end&&c<np);}M16_NEED(end&&b.p[base+0x628]<np&&m16_emit(f,s,&b,"orders.cff",base+0x628,65));
 for(i=0;i<np;++i){uint64_t j;at=base+0x669+(uint64_t)i*stride;for(j=0;j<stride;j+=3){uint8_t effect=b.p[(size_t)(at+j)+1],arg=b.p[(size_t)(at+j)+2];M16_NEED(m16_work(&b,1));if(effect=='I')M16_NEED(arg<47);M16_NEED(!effect||effect=='I'||effect=='H'||effect=='A'||effect=='L'||effect=='K'||effect=='M'||effect=='C'||effect=='G'||effect=='B'||effect=='E'||effect=='F'||effect=='D'||effect=='J');}M16_NEED(m16_emit(f,s,&b,"pattern.cff",at,stride));}
 s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_cudfm_cff_init(xx_cudfm_cff *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_CUDFM_CFF,"cudfm_cff");}}
xx_cudfm_cff *xx_cudfm_cff_create(xx_io_device *d,int64_t b) {xx_cudfm_cff *r=(xx_cudfm_cff *)xx_mem_alloc(sizeof(*r));if(r)xx_cudfm_cff_init(r,d,b);return r;}
void xx_cudfm_cff_destroy(xx_cudfm_cff *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_cudfm_cff_free(xx_cudfm_cff *r) {if(r){xx_cudfm_cff_destroy(r);xx_mem_free(r);}}
bool xx_cudfm_cff_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_cudfm_cff_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
