/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://raw.githubusercontent.com/TeX-Live/texlive-source/trunk/texk/web2c/gftype.web
 * TeX GF131 complete paint/skip/new-row glyph programs, glyph and postamble bounds, character backpointers, specials, postamble/locators and aligned postpost padding. Original encoded font glyphs/metadata exported; no specials execution or raster rendering.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#include "xxfclib/formats/tex_gf/xx_tex_gf.h"
#include "../wavefront_obj/xx_eleventh_media.h"
#include "xxfclib/data/xx_data.h"
static bool eg_quick(Abstractformat *f,uint64_t n) {uint8_t b[3];return n>=55&&pm_read(f,0,b,3)&&b[0]==247&&b[1]==131;}
static bool gf_special(const uint8_t *b,uint64_t *p,uint64_t n,uint8_t op) {
 uint32_t z;if(op>=239&&op<=242){unsigned bytes=op-238;if(!eg_span(*p,bytes,n))return false;z=eg_uint(b+*p,bytes);*p+=bytes;if(z>1048576||!eg_span(*p,z,n))return false;*p+=z;return true;}
 if(op==243){if(!eg_span(*p,4,n))return false;*p+=4;return true;}return op==244;
}
static bool eg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 int32_t boc[256],prefix[256],gm0=1048577,gm1=-1048577,gn0=1048577,gn1=-1048577;bool loc[256],painted=false;uint64_t p,section,after,post=0,budget=0;uint32_t i,glyphs=0,ops=0,locs=0;char label[64];
 if(b[0]!=247||b[1]!=131||!eg_span(3,b[2],n)) {return false; } p=3+b[2];section=after=p;
 for(i=0;i<256;++i){boc[i]=prefix[i]=-1;loc[i]=false;}if(!eg_emit(f,s,"preamble.gf",0,p,n))return false;
 while(p<n){uint64_t start=p;uint8_t op=b[p++];int32_t code,minm,maxm,minn,maxn;int64_t m,row;bool black=false;
  if(eg_stop(pd)||++ops>2000000)return false;
  if(op==248){post=start;if(!eg_span(p,36,n)||xx_data_get_u32(b+p, 4, 0, true)!=(uint32_t)after||!xx_data_get_u32(b+p+4, 4, 0, true)||(xx_data_get_u32(b+p+4, 4, 0, true)&0x80000000U)||!xx_data_get_u32(b+p+12, 4, 0, true)||(xx_data_get_u32(b+p+12, 4, 0, true)&0x80000000U)||!xx_data_get_u32(b+p+16, 4, 0, true)||(xx_data_get_u32(b+p+16, 4, 0, true)&0x80000000U))return false;
   if(section<start&&!eg_emit(f,s,"font-specials.gf",section,start-section,n))return false;
    {int32_t m0=(int32_t)xx_data_get_u32(b+p+20, 4, 0, true),m1=(int32_t)xx_data_get_u32(b+p+24, 4, 0, true),n0=(int32_t)xx_data_get_u32(b+p+28, 4, 0, true),n1=(int32_t)xx_data_get_u32(b+p+32, 4, 0, true);if(m0>m1||n0>n1||m0< -1048576||m1>1048576||n0< -1048576||n1>1048576||(painted&&(m0>gm0||m1<gm1||n0>gn0||n1<gn1)))return false;}
   p+=36;if(!eg_emit(f,s,"postamble.gf",start,p-start,n))return false;break;}
  if(gf_special(b,&p,n,op))continue;
  if(op==67){int32_t prev;if(!eg_span(p,24,n))return false;code=(int32_t)xx_data_get_u32(b+p, 4, 0, true);prev=(int32_t)xx_data_get_u32(b+p+4, 4, 0, true);minm=(int32_t)xx_data_get_u32(b+p+8, 4, 0, true);maxm=(int32_t)xx_data_get_u32(b+p+12, 4, 0, true);minn=(int32_t)xx_data_get_u32(b+p+16, 4, 0, true);maxn=(int32_t)xx_data_get_u32(b+p+20, 4, 0, true);p+=24;
   if(prev!=boc[(uint32_t)code&255]&&prev!=prefix[(uint32_t)code&255])return false;}
  else if(op==68){if(!eg_span(p,5,n))return false;code=b[p];maxm=b[p+2];minm=maxm-b[p+1];maxn=b[p+4];minn=maxn-b[p+3];p+=5;
   if(boc[(uint32_t)code&255]!=-1)return false;}
  else return false;
  if(++glyphs>1024||minm>maxm||minn>maxn||minm< -1048576||maxm>1048576||minn< -1048576||maxn>1048576||(uint64_t)((int64_t)maxm-minm+1)*((int64_t)maxn-minn+1)>16777216)return false;
  budget+=(uint64_t)((int64_t)maxm-minm+1)*((int64_t)maxn-minn+1);if(budget>67108864)return false;boc[(uint32_t)code&255]=(int32_t)start;prefix[(uint32_t)code&255]=(int32_t)section;m=minm;row=maxn;
  for(;;){uint32_t count=0;if(eg_stop(pd)||++ops>2000000||p==n)return false;op=b[p++];if(op==69)break;
   if(op<=66){if(op<=63)count=op;else{unsigned z=op-63;if(!eg_span(p,z,n))return false;count=eg_uint(b+p,z);p+=z;}if((int64_t)count>(int64_t)maxm+1-m)return false;if(black&&count){int32_t a=(int32_t)m,z=(int32_t)(m+count-1),r=(int32_t)row;painted=true;if(a<gm0)gm0=a;if(z>gm1)gm1=z;if(r<gn0)gn0=r;if(r>gn1)gn1=r;}m+=count;black=!black;}
   else if(op>=70&&op<=73){if(op>70){unsigned z=op-70;if(!eg_span(p,z,n))return false;count=eg_uint(b+p,z);p+=z;}row-=(int64_t)count+1;m=minm;black=false;if(row<minn)return false;}
   else if(op>=74&&op<=238){--row;m=(int64_t)minm+op-74;black=true;if(row<minn||m>(int64_t)maxm+1)return false;}
   else if(!gf_special(b,&p,n,op))return false;
  }
  xx_rt_snprintf(label,sizeof(label),"glyph-%u-code-%u.gf",glyphs-1,(uint32_t)code);if(!eg_emit(f,s,label,section,p-section,n))return false;after=section=p;
 }
 if(!post||!glyphs)return false;
 while(p<n){uint64_t start=p;uint8_t op=b[p++];uint32_t code;int32_t ref;
  if(eg_stop(pd))return false;
  if(op==249){if(!eg_span(p,5,n)||xx_data_get_u32(b+p, 4, 0, true)!=(uint32_t)post||b[p+4]!=131)return false;p+=5;
   if(n-p<4||(n&3)) {return false; } for(i=0;i<n-p;++i)if(b[p+i]!=223)return false;
   for(i=0;i<256;++i) {if(boc[i]>=0&&!loc[i])return false; } if(!locs||!eg_emit(f,s,"postpost.gf",start,n-start,n))return false;s->size=(int64_t)n;return true;}
  if(op==244){if(!eg_emit(f,s,"postamble-nop.gf",start,1,n))return false;continue;}
  if(op!=245&&op!=246) {return false; } if(!eg_span(p,op==245?17:10,n))return false;code=b[p];ref=(int32_t)xx_data_get_u32(b+p+(op==245?13:6), 4, 0, true);p+=op==245?17:10;
  if(loc[code]||(ref!=boc[code]&&ref!=prefix[code])||(ref==-1&&boc[code]!=-1)) {return false; } loc[code]=true;++locs;
  if(!eg_emit(f,s,"character-locator.gf",start,p-start,n))return false;
 }
 return false;
}

void xx_tex_gf_init(xx_tex_gf *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_TEX_GF,"gf");}}
xx_tex_gf *xx_tex_gf_create(xx_io_device *d,int64_t at) {xx_tex_gf *r=(xx_tex_gf *)xx_mem_alloc(sizeof(*r));if(r)xx_tex_gf_init(r,d,at);return r;}
void xx_tex_gf_destroy(xx_tex_gf *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tex_gf_free(xx_tex_gf *r) {if(r){xx_tex_gf_destroy(r);xx_mem_free(r);}}
bool xx_tex_gf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_tex_gf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
