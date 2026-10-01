/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://www.lua.org/source/5.1/lundump.c.html
 * Lua5.1 little-endian chunks with4-byte integers/instructions,4/8-byte size_t and IEEE64 numbers. Parses all nested prototype/code/constant/debug tables, opcode numbers, stack counts and debug ranges; recursion32,1024 prototypes/1million instructions. Exports header and encoded prototype tree. Other Lua versions/architectures, VM semantic verification and execution unsupported.
 */
#include "xxfclib/formats/lua_bytecode51/xx_lua_bytecode51.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"

static __inline bool span(uint64_t a,uint64_t n,uint64_t e) { return a<=e && n<=e-a; }
static __inline bool stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static __inline uint64_t u64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
static __inline uint32_t u32(const uint8_t *p,bool be) { return be ? pm_be32(p):pm_le32(p); }
static __inline uint16_t u16(const uint8_t *p,bool be) { return be ? pm_be16(p):pm_le16(p); }
static __inline uint32_t be24(const uint8_t *p) { return (uint32_t)p[0]<<16 | (uint32_t)p[1]<<8 | p[2]; }
static __inline bool zero(const uint8_t *b,uint64_t n) { uint64_t i; for(i=0;i<n;++i) if(b[i]) return false; return true; }
static __inline bool finite32(const uint8_t *p,bool be) { return (u32(p,be)&0x7f800000U)!=0x7f800000U; }
static __inline bool finite64(const uint8_t *p,bool be) { return (u64(p,be)&0x7ff0000000000000ULL)!=0x7ff0000000000000ULL; }
static __inline bool floats(const uint8_t *b,uint64_t at,uint64_t count,bool be,uint64_t n) { uint64_t i; if(!span(at,count*4,n)) return false; for(i=0;i<count;++i) if(!finite32(b+at+i*4,be)) return false; return true; }
static __inline bool emit(Abstractformat *f,pm_stream *s,const char *label,uint64_t a,uint64_t n,uint64_t e) { return span(a,n,e) && s->count<4096 && pm_add(f,s,label,(int64_t)a,(int64_t)n); }
static __inline bool cstr(const uint8_t *b,uint64_t *at,uint64_t end,uint64_t maximum,bool empty) { uint64_t start=*at; while(*at<end && *at-start<=maximum) { uint8_t c=b[(*at)++]; if(!c) return empty || *at>start+1; if(c<32 || c==127) return false; } return false; }
typedef struct range { uint64_t at,n; } range;
static __inline bool reserve(range *r,unsigned *nr,unsigned max,uint64_t at,uint64_t n,uint64_t lo,uint64_t end) { unsigned i; if(*nr>=max || at<lo || !span(at,n,end)) return false; for(i=0;i<*nr;++i) if(n && r[i].n && at<r[i].at+r[i].n && r[i].at<at+n) return false; r[*nr].at=at;r[*nr].n=n;++*nr;return true; }
static __inline uint32_t crc32_bytes(const uint8_t *b,uint64_t n) { return xx_crc32_calc(0U, b, (size_t)n); }

typedef struct lua_scan {const uint8_t *b;uint64_t n,p;unsigned size_t_width,protos;uint64_t instructions;xx_pd_struct *pd;} lua_scan;
static bool lua_string(lua_scan *q){uint64_t len;if(!span(q->p,q->size_t_width,q->n))return false;len=q->size_t_width==8?u64(q->b+q->p,false):pm_le32(q->b+q->p);q->p+=q->size_t_width;if(len>16777216||!span(q->p,len,q->n)||(len&&q->b[q->p+len-1]))return false;q->p+=len;return true;}
static bool lua_count(lua_scan *q,uint32_t *c,uint32_t max){if(!span(q->p,4,q->n))return false;*c=pm_le32(q->b+q->p);q->p+=4;return *c<=max;}
static bool lua_proto(lua_scan *q,unsigned depth){uint32_t first,last,code,constants,children,lines,locals,upvalues,i;uint8_t nups,params,stack;if(depth>32||++q->protos>1024||stop(q->pd)||!lua_string(q)||!span(q->p,12,q->n))return false;
 first=pm_le32(q->b+q->p);last=pm_le32(q->b+q->p+4);nups=q->b[q->p+8];params=q->b[q->p+9];stack=q->b[q->p+11];if(first>last||nups>60||params>stack||stack<2||stack>250||q->b[q->p+10]>7)return false;q->p+=12;
 if(!lua_count(q,&code,65536)||!code||q->instructions+code>1000000||!span(q->p,(uint64_t)code*4,q->n))return false;q->instructions+=code;for(i=0;i<code;++i)if((pm_le32(q->b+q->p+(uint64_t)i*4)&63)>37)return false;q->p+=(uint64_t)code*4;
 if(!lua_count(q,&constants,65536))return false;for(i=0;i<constants;++i){uint8_t t;if(q->p>=q->n)return false;t=q->b[q->p++];if(t==0)continue;if(t==1){if(q->p>=q->n||q->b[q->p++]>1)return false;}else if(t==3){if(!span(q->p,8,q->n)||!finite64(q->b+q->p,false))return false;q->p+=8;}else if(t==4){if(!lua_string(q))return false;}else return false;}
 if(!lua_count(q,&children,1024))return false;for(i=0;i<children;++i)if(!lua_proto(q,depth+1))return false;
 if(!lua_count(q,&lines,65536)||(lines&&lines!=code)||!span(q->p,(uint64_t)lines*4,q->n))return false;q->p+=(uint64_t)lines*4;if(!lua_count(q,&locals,65536))return false;for(i=0;i<locals;++i){uint32_t a,e;if(!lua_string(q)||!span(q->p,8,q->n))return false;a=pm_le32(q->b+q->p);e=pm_le32(q->b+q->p+4);q->p+=8;if(a>e||e>code)return false;}
 if(!lua_count(q,&upvalues,60)||(upvalues&&upvalues!=nups))return false;for(i=0;i<upvalues;++i)if(!lua_string(q))return false;return true;
}

static bool parse_data(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {

 lua_scan q;if(n<12||xx_rt_memcmp(b,"\x1bLua\x51",5)||b[5]||b[6]!=1||b[7]!=4||(b[8]!=4&&b[8]!=8)||b[9]!=4||b[10]!=8||b[11])return false;xx_mem_zero(&q,sizeof(q));q.b=b;q.n=n;q.p=12;q.size_t_width=b[8];q.pd=pd;if(!lua_proto(&q,0)||q.p!=n)return false;
 if(!emit(f,s,"header.bin",0,12,n)||!emit(f,s,"prototype-tree.bin",12,n-12,n))return false;s->size=(int64_t)n;return true;

}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 int64_t available=pm_available(f);uint8_t *b,probe[32];bool result;if(available<1||available>67108864||stop(pd))return false;if(available<5 || !pm_read(f,0,probe,5) || xx_rt_memcmp(probe,"\x1b\x4c\x75\x61\x51",5)) return false; b=(uint8_t *)xx_mem_alloc((size_t)available);if(!b)return false;
 result=pm_read(f,0,b,(size_t)available)&&parse_data(f,s,b,(uint64_t)available,pd);xx_mem_free(b);return result;
}

void xx_lua_bytecode51_init(xx_lua_bytecode51 *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_LUA_BYTECODE51,"luac");} }
xx_lua_bytecode51 *xx_lua_bytecode51_create(xx_io_device *d,int64_t b) {xx_lua_bytecode51 *r=(xx_lua_bytecode51 *)xx_mem_alloc(sizeof(*r));if(r)xx_lua_bytecode51_init(r,d,b);return r;}
void xx_lua_bytecode51_destroy(xx_lua_bytecode51 *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_lua_bytecode51_free(xx_lua_bytecode51 *r) {if(r){xx_lua_bytecode51_destroy(r);xx_mem_free(r);}}
bool xx_lua_bytecode51_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_lua_bytecode51_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
