/* SPDX-License-Identifier: MIT
 * Primary reference: https://gts.sourceforge.net/reference/gts-surfaces.html
 * GNU GTS ASCII base classes: complete declared vertex/edge/triangle tables, finite coordinates and valid distinct local edge/vertex references forming each triangle. Original typed tables exported; custom per-object attributes and progressive surfaces declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/gts_surface/xx_gts_surface.h"
#include "../fontforge_sfd/xx_fourteenth_games.h"
static bool fg_quick(Abstractformat *f,uint64_t n) {uint8_t c;return n>=12&&pm_read(f,0,&c,1)&&(c=='#'||c=='!'||(c>='0'&&c<='9'));}
static bool fg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 fg_lex q={b,0,n,pd,0,true,false,false};int32_t nv,ne,nf,a,c,d;uint32_t *edges=NULL;uint64_t p,end;int32_t i;double v;bool ok=false;
 if(!fg_utf(b,n,true,pd)||!fg_integer(&q,&nv)||nv<3||nv>1000000||!fg_integer(&q,&ne)||ne<3||ne>1000000||!fg_integer(&q,&nf)||nf<1||nf>1000000||!fg_skip(&q))return false;
 if(fg_kw(&q,"GtsSurface")){if(!fg_kw(&q,"GtsFace")||!fg_kw(&q,"GtsEdge")||!fg_kw(&q,"GtsVertex")||!fg_skip(&q))return false;}p=q.p;if(!fg_emit(f,s,"descriptor.gts",0,p,n))return false;
 for(i=0;i<nv;++i){if(!fg_number(&q,&v)||!fg_number(&q,&v)||!fg_number(&q,&v))goto done;}end=q.p;
 if(!fg_emit(f,s,"vertices.gts",p,end-p,n))goto done;p=end;
 edges=(uint32_t *)xx_mem_alloc((size_t)ne*8);if(!edges)goto done;
 for(i=0;i<ne;++i){if(!fg_integer(&q,&a)||!fg_integer(&q,&c)||a<1||a>nv||c<1||c>nv||a==c)goto done;edges[i*2]=(uint32_t)a;edges[i*2+1]=(uint32_t)c;}end=q.p;
 if(!fg_emit(f,s,"edges.gts",p,end-p,n))goto done;p=end;
 for(i=0;i<nf;++i){uint32_t e[6],j,k,unique=0;int32_t index[3];if(!fg_integer(&q,&a)||!fg_integer(&q,&c)||!fg_integer(&q,&d)||a<1||c<1||d<1||a>ne||c>ne||d>ne||a==c||a==d||c==d)goto done;index[0]=a;index[1]=c;index[2]=d;for(j=0;j<3;++j){e[j*2]=edges[(index[j]-1)*2];e[j*2+1]=edges[(index[j]-1)*2+1];}for(j=0;j<6;++j){unsigned count=0;bool first=true;for(k=0;k<6;++k)if(e[k]==e[j]){++count;if(k<j)first=false;}if(count!=2)goto done;if(first)++unique;}if(unique!=3)goto done;}
 if(!fg_emit(f,s,"faces.gts",p,q.p-p,n)||!fg_end(&q)||!fg_cover(f,s,"comments.gts",n))goto done;ok=true;
done:if(edges)xx_mem_free(edges);return ok;
}

void xx_gts_surface_init(xx_gts_surface *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_GTS_SURFACE,"gts");}}
xx_gts_surface *xx_gts_surface_create(xx_io_device *d,int64_t at) {xx_gts_surface *r=(xx_gts_surface *)xx_mem_alloc(sizeof(*r));if(r)xx_gts_surface_init(r,d,at);return r;}
void xx_gts_surface_destroy(xx_gts_surface *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_gts_surface_free(xx_gts_surface *r) {if(r){xx_gts_surface_destroy(r);xx_mem_free(r);}}
bool xx_gts_surface_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_gts_surface_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
