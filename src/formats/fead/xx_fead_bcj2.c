/* Copyright (c) 2026 hors<horsicq@gmail.com>; SPDX-License-Identifier: MIT.
 * FEAD adapter of the library's xx_7zip_branch.c BCJ2 decoder: fixed zero IP,
 * cancellation/progress every64KiB; no shared codec source is modified. */
#include "fead_bcj2.h"
#include "xxfclib/memory/xx_memory.h"
#include <limits.h>
typedef struct fead_bcj2_decoder {
 const uint8_t *main_data, *call_data, *jump_data, *range_data;
 size_t main_size, call_size, jump_size, range_size;
 size_t main_pos, call_pos, jump_pos, range_pos;
 uint32_t range, code, ip;
 uint16_t probabilities[258];
 uint8_t previous;
} fead_bcj2_decoder;
static uint32_t b2_be32(const uint8_t *p)
{ return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void b2_le32(uint8_t *p, uint32_t value)
{ p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);p[2]=(uint8_t)(value>>16);p[3]=(uint8_t)(value>>24); }
static bool b2_range_byte(fead_bcj2_decoder *d, uint8_t *v)
{ if(d->range_pos>=d->range_size)return false;*v=d->range_data[d->range_pos++];return true; }
static bool b2_bit(fead_bcj2_decoder *d, uint16_t *probability, bool *bit)
{
 uint32_t bound;uint8_t next;
 if(d->range<(UINT32_C(1)<<24)) {
  if(!b2_range_byte(d,&next))return false;
  d->range<<=8;d->code=(d->code<<8)|next;
 }
 bound=(d->range>>11)* *probability;
 if(d->code<bound) {
  d->range=bound;*probability=(uint16_t)(*probability+((2048U-*probability)>>5));*bit=false;
 } else {
  d->range-=bound;d->code-=bound;*probability=(uint16_t)(*probability-(*probability>>5));*bit=true;
 }
 return true;
}
bool fead_bcj2_decode(const uint8_t *const inputs[4], const size_t sizes[4],
 uint8_t *output, size_t output_size, xx_pd_struct *pd)
{
 fead_bcj2_decoder d;
 size_t produced=0,checkpoint=0,i;
 uint8_t first;
 int level;
 bool ok=false;
 if(!inputs||!sizes||(!output&&output_size)||(!inputs[0]&&sizes[0])||
    (!inputs[1]&&sizes[1])||(!inputs[2]&&sizes[2])||(!inputs[3]&&sizes[3])||
    sizes[3]<5||(sizes[1]&3U)||(sizes[2]&3U)||xx_pd_is_stopped(pd))return false;
 xx_mem_zero(&d,sizeof(d));
 d.main_data=inputs[0];d.main_size=sizes[0];d.call_data=inputs[1];d.call_size=sizes[1];
 d.jump_data=inputs[2];d.jump_size=sizes[2];d.range_data=inputs[3];d.range_size=sizes[3];
 for(i=0;i<258;++i)d.probabilities[i]=1024;
 if(!b2_range_byte(&d,&first)||first!=0)return false;
 for(i=0;i<4;++i) { uint8_t next;if(!b2_range_byte(&d,&next))return false;d.code=(d.code<<8)|next; }
 if(d.code==UINT32_MAX) {return false; } d.range=UINT32_MAX;
 level=xx_pd_enter_level(pd,output_size,"FEAD BCJ2 restore");
 while(d.main_pos<d.main_size) {
  uint8_t opcode;bool branch;
  if(d.main_pos>=checkpoint) {
   if(xx_pd_is_stopped(pd))goto done;
   xx_pd_set_current(pd,level,produced);
   checkpoint=d.main_pos>SIZE_MAX-65536U?SIZE_MAX:d.main_pos+65536U;
  }
  opcode=d.main_data[d.main_pos++];
  branch=opcode==0xe8U||opcode==0xe9U||(d.previous==0x0fU&&(opcode&0xf0U)==0x80U);
  if(produced>=output_size) {goto done; } output[produced++]=opcode;++d.ip;
  if(branch) {
   uint16_t *probability=opcode==0xe8U?&d.probabilities[2U+d.previous]:&d.probabilities[opcode==0xe9U?1U:0U];
   bool converted;
   if(!b2_bit(&d,probability,&converted))goto done;
   if(converted) {
    const uint8_t *target;size_t *position,target_size;uint32_t value;
    if(opcode==0xe8U){target=d.call_data;position=&d.call_pos;target_size=d.call_size;}
    else{target=d.jump_data;position=&d.jump_pos;target_size=d.jump_size;}
    if(*position>target_size||target_size-*position<4||output_size-produced<4)goto done;
    value=b2_be32(target+*position);*position+=4;d.ip+=4;value-=d.ip;
    b2_le32(output+produced,value);produced+=4;d.previous=(uint8_t)(value>>24);continue;
   }
  }
  d.previous=opcode;
 }
 ok=produced==output_size&&d.call_pos==d.call_size&&d.jump_pos==d.jump_size&&d.code==0&&!xx_pd_is_stopped(pd);
 if(ok)xx_pd_set_current(pd,level,produced);
done:
 xx_pd_leave_level(pd,level);
 return ok&&!xx_pd_is_stopped(pd);
}
