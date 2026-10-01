/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://redis.io/docs/latest/develop/reference/protocol-spec/ */
#include "xxfclib/formats/redis_resp/xx_redis_resp.h"
#include "../xx_twelfth_a.h"

static bool re_int(nh_blob *b,uint64_t *at,int64_t *value) {bool neg=false;uint64_t n=0,start=*at;if(!nh_span(b,*at,1)) return false;if(b->p[(size_t)*at]=='-') {neg=true;++*at;}uint64_t digits=*at;while(*at<b->n && b->p[(size_t)*at]>='0' && b->p[(size_t)*at]<='9') {uint8_t d=b->p[(size_t)(*at)++]-'0';if(n>(9223372036854775807ULL-d)/10) return false;n=n*10+d;}if(*at==digits || *at-start>20 || (*at-digits>1 && b->p[(size_t)digits]=='0') || (neg && !n) || !nh_span(b,*at,2) || b->p[(size_t)*at]!='\r' || b->p[(size_t)*at+1]!='\n') return false;*at+=2;*value=neg ? -(int64_t)n:(int64_t)n;return true;}
static bool re_command(nh_blob *b,uint64_t at,uint64_t n,int64_t args) {uint8_t p[8];if(!n || n>7 || !nh_span(b,at,n)) return false;for(uint64_t i=0;i<n;++i) {uint8_t c=b->p[(size_t)(at+i)];if(c>='a' && c<='z') c-=32;p[i]=c;}if(n==3 && !xx_rt_memcmp(p,"SET",3)) return args==3;if(n==3 && !xx_rt_memcmp(p,"GET",3)) return args==2;if(n==3 && !xx_rt_memcmp(p,"DEL",3)) return args>=2;if(n==6 && !xx_rt_memcmp(p,"EXISTS",6)) return args>=2;if(n==4 && !xx_rt_memcmp(p,"MSET",4)) return args>=3 && (args&1);if(n==4 && !xx_rt_memcmp(p,"HSET",4)) return args>=4 && !(args&1);if(n==4 && !xx_rt_memcmp(p,"PING",4)) return args==1 || args==2;if(n==4 && !xx_rt_memcmp(p,"ECHO",4)) return args==2;return false;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {nh_blob b;uint64_t at=0,start,arg;int64_t count,n;unsigned commands=0;bool ok=false;if(!nh_load(f,&b,pd)) return false;NH_NEED(b.n>=24);while(at<b.n) {start=at;NH_NEED(b.p[(size_t)at++]=='*' && re_int(&b,&at,&count) && count>=1 && count<=1024 && nh_add(f,s,&b,"command-array",start,at-start));for(int64_t i=0;i<count;++i) {NH_NEED(nh_span(&b,at,1) && b.p[(size_t)at++]=='$' && re_int(&b,&at,&n) && n>=0 && n<=16777216 && nh_span(&b,at,(uint64_t)n+2));arg=at;NH_NEED(b.p[(size_t)(at+(uint64_t)n)]=='\r' && b.p[(size_t)(at+(uint64_t)n+1)]=='\n' && (i || re_command(&b,at,(uint64_t)n,count)) && nh_add(f,s,&b,i ? "argument":"command",at,(uint64_t)n));at=arg+(uint64_t)n+2;}NH_NEED(++commands<=256);}NH_NEED(commands>=2);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_redis_resp_init(xx_redis_resp *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_REDIS_RESP,"resp"); } }
xx_redis_resp *xx_redis_resp_create(xx_io_device *d,int64_t b) { xx_redis_resp *r=(xx_redis_resp *)xx_mem_alloc(sizeof(*r)); if(r) xx_redis_resp_init(r,d,b); return r; }
void xx_redis_resp_destroy(xx_redis_resp *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_redis_resp_free(xx_redis_resp *r) { if(r) { xx_redis_resp_destroy(r); xx_mem_free(r); } }
bool xx_redis_resp_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_redis_resp_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
