/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://fontforge.org/docs/techref/pcf-format.html
 * X11 PCF directory and complete typed properties, accelerators, metrics, bitmap, encoding, width and glyph-name tables, including the original bdftopcf final accelerator100/72-byte convention. Counts, names, bitmap sizes and character references are bounded. Original typed tables remain encoded; unknown table types/format flags and font rendering are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#include "xxfclib/formats/font_pcf/xx_font_pcf.h"
#include "../astc_texture/xx_tenth_media.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b[8];return tg_probe(f,n,b,8)&&pm_tag(b,"\1fcp",4)&&pm_le32(b+4)>=3&&pm_le32(b+4)<=9;}
typedef struct pc_table {uint32_t type,format,declared;uint64_t at,end;} pc_table;
static uint32_t pc_u32(const uint8_t *p,uint32_t fmt) {return fmt&4?pm_be32(p):pm_le32(p);}
static uint32_t pc_u16(const uint8_t *p,uint32_t fmt) {return fmt&4?pm_be16(p):pm_le16(p);}
static bool pc_metric(const uint8_t *b,uint64_t at,bool compressed,uint32_t fmt,int32_t *w,int32_t *h) {int32_t left,right,asc,desc;if(compressed){left=b[at]-128;right=b[at+1]-128;asc=b[at+3]-128;desc=b[at+4]-128;}else{left=(int16_t)pc_u16(b+at,fmt);right=(int16_t)pc_u16(b+at+2,fmt);asc=(int16_t)pc_u16(b+at+6,fmt);desc=(int16_t)pc_u16(b+at+8,fmt);}*w=right-left;*h=asc+desc;return *w>=-4096&&*w<=4096&&*h>=-4096&&*h<=4096;}
static bool pc_padding(const uint8_t *b,uint64_t p,uint64_t end) {return p<=end&&end-p<=3&&tg_zero(b+p,end-p);}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 pc_table t[9];uint32_t tc=pm_le32(b+4),seen=0,i,j,glyphs=0;int32_t *dims=NULL;uint64_t p=8+(uint64_t)tc*16;bool result=false;char label[64];
 if(p>n) {return false; } for(i=0;i<tc;++i){const uint8_t *d=b+8+i*16;uint32_t type=pm_le32(d),fmt=pm_le32(d+4),decl=pm_le32(d+8);uint64_t at=pm_le32(d+12);if(tg_stop(pd)||!type||type>256||(type&(type-1))||(seen&type)||(fmt&~319U)||at!=p||at&3||decl<4)return false;seen|=type;t[i].type=type;t[i].format=fmt;t[i].declared=decl;t[i].at=at;
  /* Original bdftopcf reserves100 bytes for accelerators but writes72 at EOF. */
  if(!tg_span(at,decl,n)){if(i+1!=tc||(type!=2&&type!=256)||(fmt&256)==0||decl!=100||!tg_span(at,72,n)||at+72!=n)return false;t[i].end=n;p=n;}else{t[i].end=at+decl;p=t[i].end;}
  if(pm_le32(b+at)!=fmt)return false;
 }
 if(p!=n||(seen&44)!=44||!tg_emit(f,s,"pcf-directory.bin",0,8+(uint64_t)tc*16,n))return false;
 for(i=0;i<tc;++i)if(t[i].type==4){bool compressed=(t[i].format&256)!=0;uint64_t q=t[i].at+4,bytes;if(!tg_span(q,compressed?2U:4U,t[i].end))return false;glyphs=compressed?pc_u16(b+q,t[i].format):pc_u32(b+q,t[i].format);q+=compressed?2U:4U;bytes=(uint64_t)glyphs*(compressed?5U:12U);if(!glyphs||glyphs>65535||!tg_span(q,bytes,t[i].end)||!pc_padding(b,q+bytes,t[i].end))return false;dims=(int32_t *)xx_mem_alloc((size_t)glyphs*2*sizeof(*dims));if(!dims)return false;for(j=0;j<glyphs;++j)if(tg_stop(pd)||!pc_metric(b,q+(uint64_t)j*(compressed?5U:12U),compressed,t[i].format,&dims[j*2],&dims[j*2+1])||dims[j*2]<0||dims[j*2+1]<0)goto done;}
 if(!dims)return false;
 for(i=0;i<tc;++i){pc_table *d=&t[i];uint32_t fmt=d->format,type=d->type,ct,high=fmt&~63U;uint64_t q=d->at+4,end=d->end,after;
  if(tg_stop(pd)) {goto done; } if(type!=4&&type!=16&&type!=2&&type!=256&&high)goto done;
  if(type==1){uint64_t props,strings,ss;uint32_t size;if(!tg_span(q,4,end))goto done;ct=pc_u32(b+q,fmt);q+=4;props=q;if(ct>4096||!tg_span(q,(uint64_t)ct*9,end))goto done;q+=(uint64_t)ct*9;{uint64_t pad=(4-(ct&3))&3;if(!tg_span(q,pad+4,end)||!tg_zero(b+q,pad))goto done;q+=pad;}size=pc_u32(b+q,fmt);q+=4;strings=q;ss=q+size;if(!tg_span(q,size,end)||!pc_padding(b,ss,end))goto done;for(j=0;j<ct;++j){uint64_t row=props+(uint64_t)j*9;uint32_t name=pc_u32(b+row,fmt),val=pc_u32(b+row+5,fmt);if(tg_stop(pd)||name>=size||b[row+4]>1||!tg_nul(b,strings+name,ss,&after)||(b[row+4]&&(val>=size||!tg_nul(b,strings+val,ss,&after))))goto done;}}
  else if(type==2||type==256){unsigned k;uint64_t bytes=high==256?72U:48U;if(high!=0&&high!=256)goto done;if(!tg_span(d->at,bytes,end))goto done;for(k=0;k<7;++k)if(b[q+k]>1)goto done;if(b[q+7])goto done;q+=8;for(k=0;k<2;++k){int32_t v=(int32_t)pc_u32(b+q+k*4,fmt);if(v<0||v>4096)goto done;}q+=12;for(k=0;k<(high?4U:2U);++k){int32_t w,h;if(!pc_metric(b,q,false,fmt,&w,&h))goto done;q+=12;}if(q>end||end-q>28||!tg_zero(b+q,end-q))goto done;}
  else if(type==4||type==16){bool c=high==256;if((high!=0&&high!=256)||!tg_span(q,c?2U:4U,end))goto done;ct=c?pc_u16(b+q,fmt):pc_u32(b+q,fmt);q+=c?2U:4U;if(ct!=glyphs||!tg_span(q,(uint64_t)ct*(c?5U:12U),end))goto done;for(j=0;j<ct;++j){int32_t w,h;if(!pc_metric(b,q+(uint64_t)j*(c?5U:12U),c,fmt,&w,&h))goto done;}q+=(uint64_t)ct*(c?5U:12U);if(!pc_padding(b,q,end))goto done;}
  else if(type==8){uint64_t offsets,sizes,data,total[4]={0,0,0,0};uint32_t size,pad=1U<<(fmt&3),last=0;if(!tg_span(q,4,end)||pc_u32(b+q,fmt)!=glyphs)goto done;q+=4;offsets=q;if(!tg_span(q,(uint64_t)glyphs*4+16,end))goto done;q+=(uint64_t)glyphs*4;sizes=q;size=pc_u32(b+q+(fmt&3)*4,fmt);q+=16;data=q;if(!tg_span(data,size,end)||!pc_padding(b,data+size,end))goto done;
   for(j=0;j<glyphs;++j){unsigned k;uint32_t width=(uint32_t)dims[j*2],height=(uint32_t)dims[j*2+1],off=pc_u32(b+offsets+(uint64_t)j*4,fmt),bytes=((width+7)/8+pad-1)/pad*pad*height;if(tg_stop(pd)||off!=last||!tg_span(off,bytes,size))goto done;last=off+bytes;for(k=0;k<4;++k){uint32_t align=1U<<k;total[k]+=((width+7)/8+align-1)/align*align*height;}}
   if(last!=size) {goto done; } for(j=0;j<4;++j)if(total[j]!=pc_u32(b+sizes+j*4,fmt))goto done;
  }
  else if(type==32){uint32_t low,hi,l1,h1,ct2;if(!tg_span(q,10,end))goto done;low=pc_u16(b+q,fmt);hi=pc_u16(b+q+2,fmt);l1=pc_u16(b+q+4,fmt);h1=pc_u16(b+q+6,fmt);if(hi<low||hi>255||h1<l1||h1>255)goto done;ct2=(hi-low+1)*(h1-l1+1);q+=10;if(!tg_span(q,(uint64_t)ct2*2,end))goto done;for(j=0;j<ct2;++j){uint32_t ix=pc_u16(b+q+(uint64_t)j*2,fmt);if(tg_stop(pd)||(ix!=65535&&ix>=glyphs))goto done;}q+=(uint64_t)ct2*2;if(!pc_padding(b,q,end))goto done;}
  else if(type==64||type==128){uint64_t array,strings;uint32_t size;if(!tg_span(q,4,end)||pc_u32(b+q,fmt)!=glyphs)goto done;q+=4;array=q;if(!tg_span(q,(uint64_t)glyphs*4,end))goto done;q+=(uint64_t)glyphs*4;if(type==128){if(!tg_span(q,4,end))goto done;size=pc_u32(b+q,fmt);q+=4;strings=q;if(!tg_span(q,size,end))goto done;for(j=0;j<glyphs;++j){uint32_t off=pc_u32(b+array+(uint64_t)j*4,fmt);if(tg_stop(pd)||off>=size||!tg_nul(b,strings+off,strings+size,&after))goto done;}q+=size;}if(!pc_padding(b,q,end))goto done;}
  else goto done;
  xx_rt_snprintf(label,sizeof(label),"table-%u.pcf",type);if(!tg_emit(f,s,label,d->at,d->end-d->at,n))goto done;
 }
 s->size=(int64_t)n;result=true;
done:xx_mem_free(dims);return result;
}

void xx_font_pcf_init(xx_font_pcf *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_FONT_PCF,"pcf");}}
xx_font_pcf *xx_font_pcf_create(xx_io_device *d,int64_t at) {xx_font_pcf *r=(xx_font_pcf *)xx_mem_alloc(sizeof(*r));if(r)xx_font_pcf_init(r,d,at);return r;}
void xx_font_pcf_destroy(xx_font_pcf *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_font_pcf_free(xx_font_pcf *r) {if(r){xx_font_pcf_destroy(r);xx_mem_free(r);}}
bool xx_font_pcf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_font_pcf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
