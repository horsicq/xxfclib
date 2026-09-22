/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/ain/xx_ain.h"
#include <string.h>

#define AIN_WINDOW 0x8000U
#define AIN_MAIN 0x110U
#define AIN_LENGTH 0xfeU
#define AIN_PRE 19U
#define AIN_TREE 1024U
#define AIN_MAXLEN 16U

typedef struct { const uint8_t *p; size_t n,pos; uint64_t bits; unsigned count; } ain_bits;
typedef struct { uint16_t table[256], tree[AIN_TREE]; } ain_tree;
typedef struct { int count[AIN_MAXLEN+1], head[AIN_MAXLEN+1], next[AIN_MAIN], order[AIN_MAIN], node; unsigned order_count,order_pos; bool bad; } ain_build;

static bool ain_fill(ain_bits *b,unsigned n){while(b->count<n){if(b->pos==b->n)return false;b->bits|=(uint64_t)b->p[b->pos++]<<b->count;b->count+=8;}return true;}
static int64_t ain_get(ain_bits *b,unsigned n){uint64_t v;if(!n)return 0;if(!ain_fill(b,n))return -1;v=b->bits&(((uint64_t)1<<n)-1U);b->bits>>=n;b->count-=n;return(int64_t)v;}
static void ain_walk(ain_build *s,ain_tree*t,unsigned d,unsigned node,uint32_t prefix){int child,value;if(s->bad||d>AIN_MAXLEN||node>=AIN_TREE){s->bad=true;return;}if(--s->count[d]<0){child=s->node;if(child+1>=AIN_TREE){s->bad=true;return;}s->node+=2;t->tree[node]=(uint16_t)child;if(d==8)t->table[prefix>>8]=(uint16_t)(child|0x8000U);ain_walk(s,t,d+1,(unsigned)child,(prefix>>1)&0xffffU);ain_walk(s,t,d+1,(unsigned)child+1,((prefix>>1)|0x8000U)&0xffffU);return;}if(s->order_pos>=s->order_count){s->bad=true;return;}value=s->order[s->order_pos++];t->tree[node]=(uint16_t)(-value);if(d<9){unsigned at=d?prefix>>(16-d):0,step=1U<<d;uint16_t entry=(uint16_t)(value|(d<<10));while(at<256){t->table[at]=entry;at+=step;}}}
static bool ain_build_tree(const int *len,unsigned n,ain_tree*t){ain_build s;uint32_t total=0;unsigned i,d;xx_rt_memset(&s,0,sizeof(s));xx_rt_memset(t,0,sizeof(*t));for(d=0;d<=AIN_MAXLEN;d++)s.head[d]=-1;for(i=0;i<n;i++){if(len[i]<0||len[i]>AIN_MAXLEN)return false;++s.count[len[i]];s.next[i]=s.head[len[i]];s.head[len[i]]=(int)i;}for(d=1;d<=AIN_MAXLEN;d++)total+=(uint32_t)s.count[d]<<(16-d);if(total!=0x10000U)return false;for(d=1;d<=AIN_MAXLEN;d++){int p=s.head[d];while(p>=0){s.order[s.order_count++]=p;p=s.next[p];}}s.count[0]=0;s.node=0;ain_walk(&s,t,0,0,0);return !s.bad;}
static int ain_symbol(ain_bits*b,const ain_tree*t){uint16_t e;unsigned node,guard;if(!ain_fill(b,8))return -1;e=t->table[b->bits&255U];if(e<0x8000U){unsigned n=e>>10;if(!n)return -1;b->bits>>=n;b->count-=n;return e&1023U;}node=e&1023U;b->bits>>=8;b->count-=8;if(!ain_fill(b,16))return -1;for(guard=0;guard<AIN_MAXLEN;guard++){int v=(int)(int16_t)t->tree[node+(unsigned)(b->bits&1U)];b->bits>>=1;--b->count;if(v<=0)return -v;node=(unsigned)v;if(!ain_fill(b,1))return -1;}return -1;}
static bool ain_table(ain_bits*b,unsigned symbols,ain_tree*t){int pre[AIN_PRE]={0},lens[AIN_MAIN]={0};ain_tree pt;int64_t v;unsigned i,count,at=0;v=ain_get(b,5);if(v<0)return false;count=AIN_PRE-(unsigned)v;for(i=0;i<count;i++){v=ain_get(b,3);if(v<0)return false;pre[i]=(int)v;if(pre[i]==7)for(;;){v=ain_get(b,1);if(v<0)return false;if(!v)break;if(++pre[i]>AIN_MAXLEN)return false;}}if(!ain_build_tree(pre,AIN_PRE,&pt))return false;v=ain_get(b,9);if(v<0)return false;count=symbols-(unsigned)v;while(at<count){int sym=ain_symbol(b,&pt);if(sym<0)return false;if(sym>=3)lens[at++]=sym-2;else{unsigned run=1;if(sym==1){v=ain_get(b,4);if(v<0)return false;run=(unsigned)v+3;}else if(sym==2){v=ain_get(b,9);if(v<0)return false;run=(unsigned)v+20;}if(run>count-at)return false;while(run--)lens[at++]=0;}}return ain_build_tree(lens,symbols,t);}

bool xx_ain_decode_memory(const uint8_t *input,size_t input_size,size_t skip_size,uint8_t *output,size_t output_size,size_t *written){ain_bits b;ain_tree main_tree,length_tree;uint8_t window[AIN_WINDOW];size_t produced=0,out=0;unsigned wp=0;if(written)*written=0;if(!input||!output||skip_size>SIZE_MAX-output_size)return false;xx_rt_memset(&b,0,sizeof(b));b.p=input;b.n=input_size;xx_rt_memset(window,0,sizeof(window));if(ain_get(&b,1)<0||!ain_table(&b,AIN_MAIN,&main_tree)||!ain_table(&b,AIN_LENGTH,&length_tree))return false;while(produced<skip_size+output_size){int sym=ain_symbol(&b,&main_tree);if(sym<0)return false;if(sym<256){window[wp]=(uint8_t)sym;wp=(wp+1)&(AIN_WINDOW-1U);if(produced>=skip_size)output[out++]=(uint8_t)sym;++produced;continue;}else{unsigned distance=(unsigned)(sym-256),rp,length;int ls;if(distance>1){int64_t extra=ain_get(&b,distance-1U);if(extra<0)return false;if(extra==0x3fff){int64_t more=ain_get(&b,1);if(more!=0)return false;if(!ain_table(&b,AIN_MAIN,&main_tree)||!ain_table(&b,AIN_LENGTH,&length_tree))return false;continue;}distance=(unsigned)extra|(1U<<(distance-1U));}rp=(wp-(distance+1U))&(AIN_WINDOW-1U);ls=ain_symbol(&b,&length_tree);if(ls<0)return false;length=(unsigned)ls+3U;while(length--&&produced<skip_size+output_size){uint8_t c=window[rp];window[wp]=c;wp=(wp+1)&(AIN_WINDOW-1U);rp=(rp+1)&(AIN_WINDOW-1U);if(produced>=skip_size)output[out++]=c;++produced;}}}if(written)*written=out;return out==output_size;}
