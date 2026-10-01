/* SPDX-License-Identifier: MIT. Original validated components; no playback/emulation. */
#include "xxfclib/formats/tracker_archimedes/xx_tracker_archimedes.h"
#include "../asylum_amf/xx_thirteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

 tm_blob b={0};uint64_t at=8;uint32_t ch=0,np=0,no=0,pat=0,ins=0;uint64_t rows_at=0,orders_at=0;unsigned seen=0;bool ok=false;
 TM_NEED(tm_load(f,&b,pd)&&tm_tag(&b,0,"MUSX",4)&&tm_span(&b,0,8)&&pm_le32(b.p+4)==b.n-8&&tm_emit(f,s,&b,"descriptor.musx",0,8));
 while(at<b.n){uint64_t q=at+8;uint32_t n;unsigned bit=0;TM_NEED(tm_work(&b,1)&&tm_span(&b,at,8));n=pm_le32(b.p+(size_t)at+4);TM_NEED(tm_span(&b,q,n));
 if(tm_tag(&b,at,"MVOX",4)){bit=1;TM_NEED(n==4);ch=pm_le32(b.p+(size_t)q);TM_NEED(ch&&ch<=8);}
 else if(tm_tag(&b,at,"MLEN",4)){bit=2;TM_NEED(n==4);no=pm_le32(b.p+(size_t)q);TM_NEED(no&&no<=128);}
 else if(tm_tag(&b,at,"PNUM",4)){bit=4;TM_NEED(n==4);np=pm_le32(b.p+(size_t)q);TM_NEED(np&&np<=64);}
 else if(tm_tag(&b,at,"PLEN",4)){bit=8;TM_NEED(n==64);rows_at=q;}
 else if(tm_tag(&b,at,"SEQU",4)){bit=16;TM_NEED(n==128);orders_at=q;}
 else if(tm_tag(&b,at,"MNAM",4)){bit=32;TM_NEED(n==32);}
 else if(tm_tag(&b,at,"ANAM",4)){bit=64;TM_NEED(n==32);}
 else if(tm_tag(&b,at,"TINF",4)){bit=128;TM_NEED(n==4);}
 else if(tm_tag(&b,at,"STER",4)){unsigned j;bit=256;TM_NEED(n==8);for(j=0;j<8;++j)TM_NEED(b.p[(size_t)q+j]<=7);}
 else if(tm_tag(&b,at,"PATT",4)){uint32_t j,rows;TM_NEED((seen&31)==31&&pat<np);rows=b.p[(size_t)rows_at+pat];TM_NEED(rows&&n==rows*ch*4);for(j=0;j<rows*ch;++j){const uint8_t *v=b.p+(size_t)q+j*4;TM_NEED(tm_work(&b,1)&&v[2]<=36&&v[3]<=72);}++pat;}
 else if(tm_tag(&b,at,"SAMP",4)){uint64_t x=q,finish=q+n,data_at;uint32_t namelen,len,loop,z;TM_NEED(pat==np&&ins<36&&tm_span(&b,x,8)&&tm_tag(&b,x,"SNAM",4));namelen=pm_le32(b.p+(size_t)x+4);TM_NEED(namelen<=32&&8+namelen<=finish-x);x+=8+namelen;
 TM_NEED(x<=finish&&56<=finish-x&&tm_tag(&b,x,"SVOL",4)&&pm_le32(b.p+(size_t)x+4)==4&&pm_le32(b.p+(size_t)x+8)<=255);x+=12;
 TM_NEED(tm_tag(&b,x,"SLEN",4)&&pm_le32(b.p+(size_t)x+4)==4);len=pm_le32(b.p+(size_t)x+8);x+=12;
 TM_NEED(tm_tag(&b,x,"ROFS",4)&&pm_le32(b.p+(size_t)x+4)==4);loop=pm_le32(b.p+(size_t)x+8);x+=12;
 TM_NEED(tm_tag(&b,x,"RLEN",4)&&pm_le32(b.p+(size_t)x+4)==4);z=pm_le32(b.p+(size_t)x+8);x+=12;
 TM_NEED(tm_tag(&b,x,"SDAT",4)&&pm_le32(b.p+(size_t)x+4)==len);data_at=x+8;TM_NEED(len==finish-data_at);if(z>2)TM_NEED(loop<=len&&z<=len-loop);else if(z==2&&loop)TM_NEED(loop<len);
 TM_NEED(tm_emit(f,s,&b,"sample-descriptor.musx",at,data_at-at));if(len)TM_NEED(tm_emit(f,s,&b,"sample.vidc",data_at,len));++ins;at=finish;continue;}
 else TM_NEED(false);
 if(bit){TM_NEED(!(seen&bit)&&!pat&&!ins);seen|=bit;}TM_NEED(tm_emit(f,s,&b,"chunk.musx",at,8+n));at=q+n;
 }
 TM_NEED((seen&31)==31&&pat==np&&ins==36);{uint32_t j;for(j=0;j<no;++j)TM_NEED(b.p[(size_t)orders_at+j]<np);}s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_tracker_archimedes_init(xx_tracker_archimedes *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_ARCHIMEDES,"tracker_archimedes");}}
xx_tracker_archimedes *xx_tracker_archimedes_create(xx_io_device *d,int64_t b) {xx_tracker_archimedes *r=(xx_tracker_archimedes *)xx_mem_alloc(sizeof(*r));if(r)xx_tracker_archimedes_init(r,d,b);return r;}
void xx_tracker_archimedes_destroy(xx_tracker_archimedes *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tracker_archimedes_free(xx_tracker_archimedes *r) {if(r){xx_tracker_archimedes_destroy(r);xx_mem_free(r);}}
bool xx_tracker_archimedes_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_tracker_archimedes_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
