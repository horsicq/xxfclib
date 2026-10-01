/*
BSD 2-Clause License

Copyright (c) 2017-2026, Teemu Suutari
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "xxfclib/formats/xx_format.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#define FR_SYMBOLS 511U
#define FR_ROOT 1020U
#define FR_LIMIT 268435456U
/* Port of Ancient's FreezeDecoder and DynamicHuffmanDecoder. Freeze 1 and 2
 * share the adaptive tree; their match lengths and distance tables differ. */
typedef struct fr_node { unsigned freq,index,parent,left,right; } fr_node;
typedef struct fr_context {
 fr_node nodes[1021]; unsigned map[1021],count;
 const uint8_t *input; size_t size,pos; unsigned bit;
 uint8_t *output; size_t used,capacity;
} fr_context;
static bool fr_bit(fr_context *c,unsigned *v) {
 if(c->pos>=c->size)return false;
 *v=(c->input[c->pos]>>(7-c->bit))&1;
 if(++c->bit==8){c->bit=0;++c->pos;}return true;
}
static bool fr_bits(fr_context *c,unsigned count,unsigned *v) {
 unsigned b;*v=0;while(count--){if(!fr_bit(c,&b))return false;*v=(*v<<1)|b;}return true;
}
static void fr_init(fr_context *c,unsigned count) {
 unsigned i,j;c->count=count;
 for(i=0;i<count;++i){fr_node *n=c->nodes+i;n->freq=1;n->index=i+(FR_SYMBOLS-count)*2;n->parent=FR_SYMBOLS*2-count+(i>>1);c->map[n->index]=i;}
 for(i=FR_SYMBOLS*2-count,j=0;i<=FR_ROOT;++i,j+=2){
  unsigned l=j>=count?j+(FR_SYMBOLS-count)*2:j;
  unsigned r=j+1>=count?j+1+(FR_SYMBOLS-count)*2:j+1;
  fr_node *n=c->nodes+i;n->freq=c->nodes[l].freq+c->nodes[r].freq;n->index=i;n->parent=FR_SYMBOLS+(i>>1);n->left=l;n->right=r;c->map[i]=i;
 }
}
static unsigned *fr_parent_leaf(fr_context *c,unsigned code) {
 fr_node *p=c->nodes+c->nodes[code].parent;return p->left==code?&p->left:&p->right;
}
static void fr_update(fr_context *c,unsigned code) {
 while(code!=FR_ROOT){
  unsigned index=c->nodes[code].index,dest=index,frequency=++c->nodes[code].freq;
  while(dest!=FR_ROOT && frequency>c->nodes[c->map[dest+1]].freq)++dest;
  if(index!=dest){
   unsigned other=c->map[dest],temp,*a,*b;
   temp=c->nodes[code].index;c->nodes[code].index=c->nodes[other].index;c->nodes[other].index=temp;
   temp=c->map[index];c->map[index]=c->map[dest];c->map[dest]=temp;
   a=fr_parent_leaf(c,code);b=fr_parent_leaf(c,other);temp=*a;*a=*b;*b=temp;
   temp=c->nodes[code].parent;c->nodes[code].parent=c->nodes[other].parent;c->nodes[other].parent=temp;
  }
  code=c->nodes[code].parent;
 }
 ++c->nodes[FR_ROOT].freq;
}
static void fr_halve(fr_context *c) {
 unsigned start=(FR_SYMBOLS-c->count)*2,i,j;
 for(i=start,j=start;i<=FR_ROOT && j<FR_SYMBOLS*2-c->count;++i)
  if(c->map[i]<FR_SYMBOLS)c->nodes[c->map[i]].index=j++;
 for(i=0;i<c->count;++i){fr_node *n=c->nodes+i;n->freq=(n->freq+1)>>1;n->parent=FR_SYMBOLS+(n->index>>1);c->map[n->index]=i;}
 for(i=FR_SYMBOLS*2-c->count,j=start;i<=FR_ROOT;++i,j+=2){
  unsigned l=c->map[j],r=c->map[j+1],freq=c->nodes[l].freq+c->nodes[r].freq,k;
  fr_node *n=c->nodes+i;n->freq=freq;n->index=i;n->parent=FR_SYMBOLS+(i>>1);n->left=l;n->right=r;c->map[i]=i;
  for(k=i;k>start && freq<c->nodes[c->map[k-1]].freq;--k){
   unsigned a=c->map[k],b=c->map[k-1],temp;
   temp=c->nodes[a].index;c->nodes[a].index=c->nodes[b].index;c->nodes[b].index=temp;
   temp=c->nodes[a].parent;c->nodes[a].parent=c->nodes[b].parent;c->nodes[b].parent=temp;
   c->map[k]=b;c->map[k-1]=a;
  }
 }
}
static bool fr_byte(fr_context *c,uint8_t value) {
 if(c->used>=FR_LIMIT)return false;
 if(c->used==c->capacity){size_t cap=c->capacity?c->capacity*2:65536;void *p=xx_mem_realloc(c->output,cap);if(!p)return false;c->output=(uint8_t *)p;c->capacity=cap;}
 c->output[c->used++]=value;return true;
}
bool xx_freeze_decode(const uint8_t *input,size_t size,uint8_t **output,size_t *used,xx_pd_struct *pd) {
 fr_context *c=NULL;unsigned table[8]={0,0,1,3,8,12,24,16},counts=0,weight=0,i,old;bool ok=false;
 if(!output || !used)return false;*output=NULL;*used=0;
 if(!input || size<2 || input[0]!=0x1f || (input[1]!=0x9e && input[1]!=0x9f))return false;
 old=input[1]==0x9e;
 if(!old){
  unsigned v,a=0,w=0;
  if(size<5 || (input[3]&0x80) || (input[4]&0xc0))return false;
  v=input[2]|(unsigned)input[3]<<8;
  table[0]=v&1;table[1]=(v>>1)&3;table[2]=(v>>3)&7;table[3]=(v>>6)&15;table[4]=v>>10;table[5]=input[4];
  for(i=0;i<6;++i){a+=table[i];w+=table[i]<<(7-i);}
  if(a>62 || w>256 || 256-w<62-a || 256-w>(62-a)*2)return false;
  table[6]=256-w-(62-a);table[7]=(62-a)*2-(256-w);
 }
 for(i=0;i<8;++i){counts+=table[i];weight+=table[i]<<(7-i);}
 if(counts>64 || weight!=256)return false;
 c=(fr_context *)xx_mem_alloc(sizeof(*c));if(!c)return false;xx_mem_zero(c,sizeof(*c));
 c->input=input;c->size=size;c->pos=old?2:5;fr_init(c,old?315:511);
 if(c->pos==size){ok=true;goto done;}
 for(;;){
  unsigned symbol=FR_ROOT,b,steps=0;
  if(pd && xx_pd_is_stopped(pd))goto done;
  while(symbol>=FR_SYMBOLS){if(++steps>1021 || !fr_bit(c,&b))goto done;symbol=b?c->nodes[symbol].right:c->nodes[symbol].left;}
  if(symbol>=c->count)goto done;
  if(c->nodes[FR_ROOT].freq==0x8000)fr_halve(c);
  fr_update(c,symbol);
  if(symbol==256)break;
  if(symbol<256){if(!fr_byte(c,(uint8_t)symbol))goto done;}
  else{
   unsigned length=symbol-254,code=0,first=0,offset=0,depth,high=0,low,distance;bool found=false;
   for(depth=0;depth<8;++depth){
    if(!fr_bit(c,&b))goto done;code=(code<<1)|b;
    if(code>=first && code-first<table[depth]){high=offset+code-first;found=true;break;}
    offset+=table[depth];first=(first+table[depth])<<1;
   }
   if(!found || !fr_bits(c,old?6:7,&low))goto done;
   distance=(high<<(old?6:7))+low+1;
   while(length--){uint8_t value=distance>c->used?0x20:c->output[c->used-distance];if(!fr_byte(c,value))goto done;}
  }
 }
 /* The physical last byte may contain padding bits, but extra bytes are not
  * a Freeze stream. This also rejects accidental marker collisions. */
 if(c->pos+(c->bit?1:0)!=size)goto done;
 ok=true;
 done:if(ok && !c->output){c->output=(uint8_t *)xx_mem_alloc(1);if(!c->output)ok=false;}
if(ok){*output=c->output;*used=c->used;c->output=NULL;}xx_mem_free(c->output);xx_mem_free(c);return ok;
}
