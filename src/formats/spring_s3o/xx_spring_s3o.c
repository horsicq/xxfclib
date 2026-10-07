/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/spring/spring/develop/rts/Rendering/Models/s3o.h
 * Spring S3O version0 triangle/quad pieces with up to256 pieces, depth32,65536 vertices and262144 indices per piece. Validates disjoint headers/strings/tables, finite geometry, child cycles and vertex references. Exports piece headers, vertex and index buffers; triangle strips, collision records, texture loading and rendering unsupported.
 */
#include "xxfclib/formats/spring_s3o/xx_spring_s3o.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static __inline bool span(uint64_t a,uint64_t n,uint64_t e) { return a<=e && n<=e-a; }
static __inline bool stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static __inline bool zero(const uint8_t *b,uint64_t n) { uint64_t i; for(i=0;i<n;++i) if(b[i]) return false; return true; }
static __inline bool finite32(const uint8_t *p,bool be) { return (xx_data_get_u32(p, 4, 0, be)&0x7f800000U)!=0x7f800000U; }
static __inline bool finite64(const uint8_t *p,bool be) { return (xx_data_get_u64(p, 8, 0, be)&0x7ff0000000000000ULL)!=0x7ff0000000000000ULL; }
static __inline bool floats(const uint8_t *b,uint64_t at,uint64_t count,bool be,uint64_t n) { uint64_t i; if(!span(at,count*4,n)) return false; for(i=0;i<count;++i) if(!finite32(b+at+i*4,be)) return false; return true; }
static __inline bool emit(Abstractformat *f,pm_stream *s,const char *label,uint64_t a,uint64_t n,uint64_t e) { return span(a,n,e) && s->count<4096 && pm_add(f,s,label,(int64_t)a,(int64_t)n); }
static __inline bool cstr(const uint8_t *b,uint64_t *at,uint64_t end,uint64_t maximum,bool empty) { uint64_t start=*at; while(*at<end && *at-start<=maximum) { uint8_t c=b[(*at)++]; if(!c) return empty || *at>start+1; if(c<32 || c==127) return false; } return false; }
typedef struct range { uint64_t at,n; } range;
static __inline bool reserve(range *r,unsigned *nr,unsigned max,uint64_t at,uint64_t n,uint64_t lo,uint64_t end) { unsigned i; if(*nr>=max || at<lo || !span(at,n,end)) return false; for(i=0;i<*nr;++i) if(n && r[i].n && at<r[i].at+r[i].n && r[i].at<at+n) return false; r[*nr].at=at;r[*nr].n=n;++*nr;return true; }

static bool s3o_string(const uint8_t *b,uint64_t n,uint32_t p,range *r,unsigned *nr) {uint64_t e=p;if(!p)return true;return cstr(b,&e,n,1024,false)&&reserve(r,nr,2048,p,e-p,52,n);}
static bool s3o_piece(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,uint32_t at,range *r,unsigned *nr,unsigned *pieces,unsigned depth,xx_pd_struct *pd) {
 uint32_t name,nc,children,nv,vertices,kind,ni,indices,i;char label[48];if(depth>32||++*pieces>256||stop(pd)||!reserve(r,nr,2048,at,52,52,n))return false;
 name=xx_data_get_u32(b+at, 4, 0, false);nc=xx_data_get_u32(b+at+4, 4, 0, false);children=xx_data_get_u32(b+at+8, 4, 0, false);nv=xx_data_get_u32(b+at+12, 4, 0, false);vertices=xx_data_get_u32(b+at+16, 4, 0, false);kind=xx_data_get_u32(b+at+24, 4, 0, false);ni=xx_data_get_u32(b+at+28, 4, 0, false);indices=xx_data_get_u32(b+at+32, 4, 0, false);
 if(nc>256||nv>65536||ni>262144||xx_data_get_u32(b+at+20, 4, 0, false)||kind>2||kind==1||xx_data_get_u32(b+at+36, 4, 0, false)||!floats(b,at+40,3,false,n)||!s3o_string(b,n,name,r,nr)||!name||(nv==0)!=(ni==0)||(ni&&(ni%(kind==2?4:3)))||(nc&&!reserve(r,nr,2048,children,(uint64_t)nc*4,52,n))||(nv&&!reserve(r,nr,2048,vertices,(uint64_t)nv*32,52,n))||(ni&&!reserve(r,nr,2048,indices,(uint64_t)ni*4,52,n))||!floats(b,vertices,(uint64_t)nv*8,false,n))return false;
 for(i=0;i<ni;++i)if(xx_data_get_u32(b+indices+(uint64_t)i*4, 4, 0, false)>=nv)return false;
 xx_rt_snprintf(label,sizeof(label),"piece-%u-header.bin",*pieces-1);if(!emit(f,s,label,at,52,n))return false;if(nv){xx_rt_snprintf(label,sizeof(label),"piece-%u-vertices.bin",*pieces-1);if(!emit(f,s,label,vertices,(uint64_t)nv*32,n))return false;xx_rt_snprintf(label,sizeof(label),"piece-%u-indices.bin",*pieces-1);if(!emit(f,s,label,indices,(uint64_t)ni*4,n))return false;}
 for(i=0;i<nc;++i) {if(!s3o_piece(f,s,b,n,xx_data_get_u32(b+children+(uint64_t)i*4, 4, 0, false),r,nr,pieces,depth+1,pd))return false; } return true;
}

static bool parse_data(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {

 range ranges[2048];unsigned nr=0,pieces=0;uint64_t end=52;unsigned i;if(n<52||xx_rt_memcmp(b,"Spring unit\0",12)||xx_data_get_u32(b+12, 4, 0, false)||xx_data_get_u32(b+40, 4, 0, false)||!floats(b,16,5,false,n)||(xx_data_get_u32(b+16, 4, 0, false)&0x80000000U)||(xx_data_get_u32(b+20, 4, 0, false)&0x80000000U)||!s3o_string(b,n,xx_data_get_u32(b+44, 4, 0, false),ranges,&nr)||!s3o_string(b,n,xx_data_get_u32(b+48, 4, 0, false),ranges,&nr)||!s3o_piece(f,s,b,n,xx_data_get_u32(b+36, 4, 0, false),ranges,&nr,&pieces,0,pd))return false;
 for(i=0;i<nr;++i) {if(ranges[i].at+ranges[i].n>end)end=ranges[i].at+ranges[i].n; } s->size=(int64_t)end;return s->count>1;

}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 int64_t available=pm_available(f);uint8_t *b,probe[32];bool result;if(available<1||available>67108864||stop(pd))return false;if(available<12 || !pm_read(f,0,probe,12) || xx_rt_memcmp(probe,"\x53\x70\x72\x69\x6e\x67\x20\x75\x6e\x69\x74\x00",12)) return false; b=(uint8_t *)xx_mem_alloc((size_t)available);if(!b)return false;
 result=pm_read(f,0,b,(size_t)available)&&parse_data(f,s,b,(uint64_t)available,pd);xx_mem_free(b);return result;
}

void xx_spring_s3o_init(xx_spring_s3o *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_SPRING_S3O,"s3o");} }
xx_spring_s3o *xx_spring_s3o_create(xx_io_device *d,int64_t b) {xx_spring_s3o *r=(xx_spring_s3o *)xx_mem_alloc(sizeof(*r));if(r)xx_spring_s3o_init(r,d,b);return r;}
void xx_spring_s3o_destroy(xx_spring_s3o *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_spring_s3o_free(xx_spring_s3o *r) {if(r){xx_spring_s3o_destroy(r);xx_mem_free(r);}}
bool xx_spring_s3o_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_spring_s3o_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
