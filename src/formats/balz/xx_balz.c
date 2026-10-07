/* SPDX-License-Identifier: MIT. Original bounded BALZ 1.20 decoder from public-domain format facts. */
#include "xxfclib/formats/balz/xx_balz.h"
#include "../xx_legacy_archive.h"
typedef struct bz_counter {uint16_t a,b;} bz_counter;
typedef struct bz_range {ac_blob *blob;uint32_t at,low,high,code;bool failed;} bz_range;
static unsigned bz_bit(bz_range *r,bz_counter *c){uint32_t mid=r->low+(uint32_t)(((uint64_t)(r->high-r->low)*((uint32_t)(c->a+c->b)<<15))>>32);unsigned bit=r->code<=mid;
 if(bit){r->high=mid;c->a+=(uint16_t)((c->a^65535U)>>3);c->b+=(uint16_t)((c->b^65535U)>>6);}else{r->low=mid+1U;c->a-=(uint16_t)(c->a>>3);c->b-=(uint16_t)(c->b>>6);}
 while((r->low^r->high)<0x01000000U){if(r->at==r->blob->n){r->failed=true;return 0;}r->code=(r->code<<8)|r->blob->p[r->at++];r->low<<=8;r->high=(r->high<<8)|255U;}return bit;
}
static unsigned bz_symbol(bz_range *r,bz_counter *c,unsigned count){unsigned context=1;while(context<count){context=context*2U+bz_bit(r,c+context);if(r->failed)return 0;}return context-count;}
static void bz_filter(ac_blob *b,uint8_t *p,uint32_t n){uint32_t at=0,end;if(n<9)return;end=n-8;while(at<end&&xx_data_get_u32(p+at, 4, 0, false)!=0x4550U)++at;
 while(at<end){if((at&4095U)==0&&!ac_poll(b))return;if((p[at++]&254U)==0xe8U){int64_t value=(int32_t)xx_data_get_u32(p+at, 4, 0, false);if(value<0){if(value+at>=0)value+=n;}else if(value<n)value-=at;uint32_t v=(uint32_t)value;for(unsigned k=0;k<4;++k)p[at+k]=(uint8_t)(v>>(8*k));at+=4;}}
}
static bool bz_parse(Abstractformat *f,pm_stream *s,ac_blob *b){uint64_t length;uint8_t *out=NULL,*counts=NULL;uint32_t *table=NULL; bz_counter *prob=NULL;bz_range r;bool ok=false;uint32_t table_size=65536U*128U*4U,prob_size=256U*(512U+128U)*sizeof(bz_counter);
 if(b->n<13||b->p[0]!=0xbaU) {return false; } length=xx_data_get_u32(b->p+1, 4, 0, false)|((uint64_t)xx_data_get_u32(b->p+5, 4, 0, false)<<32);if(length>AC_MAX_BYTES)return false;
 out=ac_alloc(b,(uint32_t)length);table=(uint32_t *)ac_alloc(b,table_size);counts=ac_alloc(b,65536U);prob=(bz_counter *)ac_alloc(b,prob_size);if(!out||!table||!counts||!prob)goto done;
 xx_mem_zero(counts,65536U);for(uint32_t i=0;i<256U*(512U+128U);++i)prob[i].a=prob[i].b=32768U;
 xx_mem_zero(&r,sizeof(r));r.blob=b;r.at=13;r.high=UINT32_MAX;r.code=xx_data_get_u32(b->p+9, 4, 0, true);
 for(uint32_t base=0;base<(uint32_t)length;){uint32_t n=(uint32_t)length-base;if(n>33554432U)n=33554432U;uint8_t *data=out+base;uint32_t at=0;xx_mem_zero(table,table_size);
  while(at<2U&&at<n){unsigned t=bz_symbol(&r,prob,512U);if(r.failed||t>255U)goto done;data[at++]=(uint8_t)t;}
  while(at<n){uint32_t start=at,c2=(uint32_t)data[at-2]|(uint32_t)data[at-1]<<8;unsigned token;if(!ac_poll(b))goto done;token=bz_symbol(&r,prob+(uint32_t)data[at-1]*512U,512U);if(r.failed)goto done;
   if(token>=256U){unsigned len=token-253U,index=bz_symbol(&r,prob+256U*512U+(uint32_t)data[at-2]*128U,128U);uint32_t pos=table[c2*128U+((counts[c2]-index)&127U)];if(r.failed||len>n-at||!pos||pos>=at)goto done;while(len--)data[at++]=data[pos++];}
   else { data[at++]=(uint8_t)token; } table[c2*128U+(++counts[c2]&127U)]=start;
  }bz_filter(b,data,n);if(!ac_poll(b))goto done;base+=n;
 }
 if(r.failed||r.at!=b->n) {goto done; } ac_release(b,(uint8_t *)table,table_size);table=NULL;ac_release(b,counts,65536U);counts=NULL;ac_release(b,(uint8_t *)prob,prob_size);prob=NULL;
 ok=ac_memory(f,s,b,"decoded.bin",out,(uint32_t)length,b->n-9U,1);out=NULL;
done:ac_release(b,out,(uint32_t)length);ac_release(b,(uint8_t *)table,table_size);ac_release(b,counts,65536U);ac_release(b,(uint8_t *)prob,prob_size);return ok;
}
AC_PARSE(bz_parse)
AC_DEFINE(balz,XX_FILE_TYPE_BALZ,"balz")
