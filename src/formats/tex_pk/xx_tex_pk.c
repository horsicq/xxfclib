/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/TeX-Live/texlive-source/trunk/texk/web2c/pktype.web
 * Four-byte-aligned TeX PK89 complete preamble, bounded short/extended/long glyph packets, fully decoded packed-number row/repeat framing and specials/post padding. Encoded original glyph packets exported; font rendering, external paths and unknown opcodes are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#include "xxfclib/formats/tex_pk/xx_tex_pk.h"
#include "../astc_texture/xx_tenth_media.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b[3];return tg_probe(f,n,b,3)&&b[0]==247&&b[1]==89;}
typedef struct pk_nibbles {const uint8_t *b;uint64_t at,end;uint32_t dyn,repeat;} pk_nibbles;
static bool pk_nib(pk_nibbles *q,uint32_t *v) {if(q->at>=q->end)return false;*v=(q->b[q->at/2]>>(q->at&1?0:4))&15U;++q->at;return true;}
static bool pk_num(pk_nibbles *q,uint32_t *value,unsigned depth) {uint32_t i,j=0,k=0,n;if(depth>2||!pk_nib(q,&i))return false;if(i==0){do{if(++k>7||!pk_nib(q,&j))return false;}while(!j);while(k--){if(j>0x0fffffffU||!pk_nib(q,&n))return false;j=j*16+n;}if(j<15)return false;*value=j-15+(13-q->dyn)*16+q->dyn;}
 else if(i<=q->dyn)*value=i;else if(i<14){if(!pk_nib(q,&j))return false;*value=(i-q->dyn-1)*16+j+q->dyn+1;}
 else{if(q->repeat)return false;q->repeat=1;if(i==14){if(!pk_num(q,&j,depth+1))return false;q->repeat=j;}return pk_num(q,value,depth+1);}return *value>0&&*value<=16777216;
}
static bool pk_raster(const uint8_t *b,uint64_t at,uint64_t end,uint32_t width,uint32_t height,uint32_t dyn,bool black,xx_pd_struct *pd) {
 uint64_t pixels=(uint64_t)width*height;if(!pixels)return at==end;
 if(dyn==14){uint64_t bytes=(pixels+7)/8;if(black||end-at!=bytes)return false;return !(pixels&7)||!(b[end-1]&((1U<<(8-(unsigned)(pixels&7)))-1));}
 {pk_nibbles q={b,at*2,end*2,dyn,0};uint32_t rows=height,left=width,run;while(rows){uint32_t finished;if(tg_stop(pd)||!pk_num(&q,&run,0))return false;if(run>=left){if(q.repeat>=rows)return false;rows-=q.repeat+1;q.repeat=0;run-=left;left=width;finished=run/width;if(finished>rows)return false;rows-=finished;run%=width;}left-=run;if(!rows&&left!=width)return false;}if(q.repeat)return false;if(q.at!=q.end){uint32_t pad;if(q.end-q.at!=1||!pk_nib(&q,&pad)||pad)return false;}return true;}
}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint64_t p=3+(uint64_t)b[2]+16,start,end,header=0;uint32_t ids[4090],count=0,i;bool post=false;char label[64];
 if(n%4||p>n||pm_be32(b+p-16)<0x100000U||pm_be32(b+p-16)>0x7fffffffU||!pm_be32(b+p-8)||pm_be32(b+p-8)>0x7fffffffU||!pm_be32(b+p-4)||pm_be32(b+p-4)>0x7fffffffU||!tg_emit(f,s,"pk-preamble.bin",0,p,n))return false;
 while(p<n){uint32_t op=b[p++];start=p-1;if(tg_stop(pd))return false;if(post){if(op!=246)return false;continue;}
  if(op<240){uint32_t dyn=op>>4,form=op&7,packet,id,w,h;unsigned offsetBytes,metricBytes;if(dyn>14||count>=4090)return false;
   if(form==7){if(!tg_span(p,8,n))return false;packet=pm_be32(b+p);id=pm_be32(b+p+4);p+=8;metricBytes=4;offsetBytes=4;header=28;}
   else if(form>=4){if(!tg_span(p,3,n))return false;packet=(form-4)*65536+pm_be16(b+p);id=b[p+2];p+=3;metricBytes=3;offsetBytes=2;header=13;}
   else{if(!tg_span(p,2,n))return false;packet=form*256+b[p];id=b[p+1];p+=2;metricBytes=3;offsetBytes=1;header=8;}
   if(packet<header||!tg_span(p,packet,n)||id>0x7fffffffU) {return false; } end=p+packet;
   for(i=0;i<count;++i) {if(ids[i]==id)return false; } ids[count++]=id;
   p+=metricBytes;if(form==7){p+=8;w=pm_be32(b+p);h=pm_be32(b+p+4);p+=16;}else{p+=offsetBytes;w=tg_uint(b+p,offsetBytes);h=tg_uint(b+p+offsetBytes,offsetBytes);p+=4*offsetBytes;}
   if(w>4096||h>4096||!pk_raster(b,p,end,w,h,dyn,(op&8)!=0,pd)) {return false; } p=end;xx_rt_snprintf(label,sizeof(label),"glyph-%u.pk",id);
  }else if(op>=240&&op<=243){unsigned bytes=op-239;uint32_t size;if(!tg_span(p,bytes,n))return false;size=tg_uint(b+p,bytes);p+=bytes;if(!tg_span(p,size,n))return false;p+=size;xx_rt_snprintf(label,sizeof(label),"special-%u.pk",(unsigned)s->count);}
  else if(op==244){if(!tg_span(p,4,n))return false;p+=4;xx_rt_snprintf(label,sizeof(label),"numeric-special-%u.pk",(unsigned)s->count);}
  else if(op==245){post=true;header=start;continue;}
  else if(op==246){xx_rt_snprintf(label,sizeof(label),"nop-%u.pk",(unsigned)s->count);}else return false;
  if(!tg_emit(f,s,label,start,p-start,n))return false;
 }
 if(!post||!count||!tg_emit(f,s,"pk-post-padding.bin",header,n-header,n)) {return false; } s->size=(int64_t)n;return true;
}

void xx_tex_pk_init(xx_tex_pk *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_TEX_PK,"pk");}}
xx_tex_pk *xx_tex_pk_create(xx_io_device *d,int64_t at) {xx_tex_pk *r=(xx_tex_pk *)xx_mem_alloc(sizeof(*r));if(r)xx_tex_pk_init(r,d,at);return r;}
void xx_tex_pk_destroy(xx_tex_pk *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tex_pk_free(xx_tex_pk *r) {if(r){xx_tex_pk_destroy(r);xx_mem_free(r);}}
bool xx_tex_pk_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_tex_pk_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
