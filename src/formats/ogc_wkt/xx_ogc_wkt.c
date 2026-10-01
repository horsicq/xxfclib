/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.ogc.org/standards/sfa/
 * OGC2D WKT POINT/LINESTRING/POLYGON/MULTIPOINT/MULTILINESTRING/MULTIPOLYGON and bounded nested GEOMETRYCOLLECTION: complete finite coordinate grammar, minimum vertex counts, closed nondegenerate rings. Nonempty ASCII geometries only. Original typed geometry components exported; dimensional/SRID extensions and topology evaluation declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/ogc_wkt/xx_ogc_wkt.h"
#include "../adobe_acb/xx_thirteenth_games.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b;return n>=12&&pm_read(f,0,&b,1)&&b>='A'&&b<='Z';}
static bool wkt_pair(tg_lex *q,double *x,double *y) {return tg_number(q,x)&&tg_number(q,y);}
static bool wkt_line(tg_lex *q,bool ring,unsigned minimum) {
 unsigned count=0;double firstx=0,firsty=0,lastx=0,lasty=0,x,y,area=0;bool distinct=false;
 if(!tg_char(q,'('))return false;
 do {if(++count>100000||!wkt_pair(q,&x,&y))return false;if(count==1){firstx=x;firsty=y;}else {area+=lastx*y-x*lasty;if(x!=firstx||y!=firsty)distinct=true;}lastx=x;lasty=y;}while(tg_char(q,','));
 return count>=minimum&&tg_char(q,')')&&(!ring||(lastx==firstx&&lasty==firsty&&distinct&&area!=0));
}
static bool wkt_polygon(tg_lex *q) {unsigned rings=0;if(!tg_char(q,'('))return false;do {if(++rings>1024||!wkt_line(q,true,4))return false;}while(tg_char(q,','));return tg_char(q,')');}
static bool wkt_geometry(Abstractformat *f,pm_stream *s,tg_lex *q,unsigned depth) {
 uint64_t start;unsigned kind,count=0;double x,y;bool wrapped;if(depth>16||!tg_skip(q))return false;start=q->p;
 if(tg_kw(q,"POINT"))kind=0;else if(tg_kw(q,"LINESTRING"))kind=1;else if(tg_kw(q,"POLYGON"))kind=2;else if(tg_kw(q,"MULTIPOINT"))kind=3;else if(tg_kw(q,"MULTILINESTRING"))kind=4;else if(tg_kw(q,"MULTIPOLYGON"))kind=5;else if(tg_kw(q,"GEOMETRYCOLLECTION"))kind=6;else return false;
 if(kind==0){if(!tg_char(q,'(')||!wkt_pair(q,&x,&y)||!tg_char(q,')'))return false;}
 else if(kind==1){if(!wkt_line(q,false,2))return false;}
 else if(kind==2){if(!wkt_polygon(q))return false;}
 else {if(!tg_char(q,'('))return false;wrapped=kind==3&&tg_char(q,'(');
  do {if(++count>1024||tg_stop(q->pd))return false;
   if(kind==3){if(count>1&&wrapped&&!tg_char(q,'('))return false;if(!wkt_pair(q,&x,&y)||(wrapped&&!tg_char(q,')')))return false;}
   else if(kind==4){if(!wkt_line(q,false,2))return false;}
   else if(kind==5){if(!wkt_polygon(q))return false;}
   else if(!wkt_geometry(f,s,q,depth+1))return false;
  }while(tg_char(q,','));if(!tg_char(q,')'))return false;
 }
 return kind==6||tg_emit(f,s,"geometry.wkt",start,q->p-start,q->n);
}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 tg_lex q={b,0,n,pd,0,false,false,false};if(!tg_utf(b,n,true,pd)||!wkt_geometry(f,s,&q,0)||!tg_end(&q)||!s->count||!tg_cover(f,s,"descriptor.wkt",n))return false;s->size=(int64_t)n;return true;
}

void xx_ogc_wkt_init(xx_ogc_wkt *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_OGC_WKT,"wkt");}}
xx_ogc_wkt *xx_ogc_wkt_create(xx_io_device *d,int64_t at) {xx_ogc_wkt *r=(xx_ogc_wkt *)xx_mem_alloc(sizeof(*r));if(r)xx_ogc_wkt_init(r,d,at);return r;}
void xx_ogc_wkt_destroy(xx_ogc_wkt *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_ogc_wkt_free(xx_ogc_wkt *r) {if(r){xx_ogc_wkt_destroy(r);xx_mem_free(r);}}
bool xx_ogc_wkt_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_ogc_wkt_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
