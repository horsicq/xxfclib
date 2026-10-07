/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/id-Software/DOOM-3/master/neo/renderer/Model_md5.cpp
 * MD5Version10 skeletal meshes, strict complete text grammar with quoted names and // comments. Validates parent hierarchy, finite transforms/UV/weights, quaternion XYZ norm, sequential indices, vertex/triangle/joint references and normalized influence sums. Up to256 joints/meshes,65536 vertices/triangles/weights per mesh;64MiB text. Exports original joints and mesh text components; escaped/non-ASCII names, MD5 animations and rendering unsupported.
 */
#include "xxfclib/formats/quake_md5mesh/xx_quake_md5mesh.h"
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

typedef struct lex {const uint8_t *b;uint64_t n,p,start,end;char token[256];bool quoted;xx_pd_struct *pd;} lex;
static bool tok(lex *q){unsigned i=0;uint8_t c;if(stop(q->pd))return false;for(;;){while(q->p<q->n&&(q->b[q->p]==' '||q->b[q->p]=='\t'||q->b[q->p]=='\r'||q->b[q->p]=='\n'))++q->p;if(q->p+1<q->n&&q->b[q->p]=='/'&&q->b[q->p+1]=='/'){while(q->p<q->n&&q->b[q->p]!='\n')++q->p;}else break;}if(q->p>=q->n)return false;q->start=q->p;c=q->b[q->p++];q->quoted=c=='"';if(q->quoted){while(q->p<q->n&&q->b[q->p]!='"'){c=q->b[q->p++];if(c<32||c>126||c=='\\'||i>=255)return false;q->token[i++]=(char)c;}if(q->p>=q->n)return false;++q->p;}else if(c=='{'||c=='}'||c=='('||c==')'){q->token[i++]=(char)c;}else{q->token[i++]=(char)c;while(q->p<q->n){c=q->b[q->p];if(c==' '||c=='\t'||c=='\r'||c=='\n'||c=='{'||c=='}'||c=='('||c==')')break;if(c<33||c>126||i>=255)return false;q->token[i++]=(char)c;++q->p;}}q->token[i]=0;q->end=q->p;return true;}
static bool word(lex *q,const char *s){return tok(q)&&!q->quoted&&!xx_rt_strcmp(q->token,s);}
static bool integer(lex *q,int32_t *v){uint64_t value=0;unsigned i=0;bool neg=false;if(!tok(q)||q->quoted)return false;if(q->token[0]=='-'){neg=true;++i;}if(!q->token[i])return false;for(;q->token[i];++i){if(q->token[i]<'0'||q->token[i]>'9')return false;value=value*10+(unsigned)(q->token[i]-'0');if(value>2147483647U)return false;}*v=neg?-(int32_t)value:(int32_t)value;return true;}
static bool number(lex *q,double *v){const char *end;uint64_t bits;if(!tok(q)||q->quoted)return false;*v=xx_rt_strtod(q->token,&end);xx_rt_memcpy(&bits,v,8);return end>q->token&&!*end&&(bits&0x7ff0000000000000ULL)!=0x7ff0000000000000ULL&&*v>=-1e20&&*v<=1e20;}
static bool vector3(lex *q,double *sq){double x,y,z;if(!word(q,"(")||!number(q,&x)||!number(q,&y)||!number(q,&z)||!word(q,")"))return false;if(sq)*sq=x*x+y*y+z*z;return true;}
static bool md5_mesh(lex *q,int32_t joints){int32_t nv=0,nt=0,nw=0,i,j,index,value,*first=NULL,*counts=NULL;double *bias=NULL,a,d;bool result=false;
 if(!word(q,"{")||!word(q,"shader")||!tok(q)||!q->quoted||!word(q,"numverts")||!integer(q,&nv)||nv<3||nv>65536)goto done;
 first=(int32_t *)xx_mem_alloc((size_t)nv*4);counts=(int32_t *)xx_mem_alloc((size_t)nv*4);if(!first||!counts)goto done;
 for(i=0;i<nv;++i){if(!word(q,"vert")||!integer(q,&index)||index!=i||!word(q,"(")||!number(q,&a)||!number(q,&d)||!word(q,")")||!integer(q,&first[i])||!integer(q,&counts[i])||first[i]<0||counts[i]<1||counts[i]>256)goto done;}
 if(!word(q,"numtris")||!integer(q,&nt)||nt<1||nt>65536) {goto done; } for(i=0;i<nt;++i){if(!word(q,"tri")||!integer(q,&index)||index!=i)goto done;for(j=0;j<3;++j)if(!integer(q,&value)||value<0||value>=nv)goto done;}
 if(!word(q,"numweights")||!integer(q,&nw)||nw<1||nw>65536) {goto done; } bias=(double *)xx_mem_alloc((size_t)nw*sizeof(double));if(!bias)goto done;
 for(i=0;i<nw;++i)if(!word(q,"weight")||!integer(q,&index)||index!=i||!integer(q,&value)||value<0||value>=joints||!number(q,&bias[i])||bias[i]<0||bias[i]>1||!vector3(q,NULL))goto done;
 for(i=0;i<nv;++i){double sum=0;if(first[i]>=nw||counts[i]>nw-first[i])goto done;for(j=0;j<counts[i];++j)sum+=bias[first[i]+j];if(sum<0.999||sum>1.001)goto done;}
 if(!word(q,"}")) {goto done; } result=true;
 done:if(first)xx_mem_free(first);if(counts)xx_mem_free(counts);if(bias)xx_mem_free(bias);return result;
}

static bool parse_data(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {

 lex q;int32_t value,joints,meshes,i;uint64_t at,end;char label[40];xx_mem_zero(&q,sizeof(q));q.b=b;q.n=n;q.pd=pd;
 if(!word(&q,"MD5Version")||!integer(&q,&value)||value!=10||!word(&q,"commandline")||!tok(&q)||!q.quoted||!word(&q,"numJoints")||!integer(&q,&joints)||joints<1||joints>256||!word(&q,"numMeshes")||!integer(&q,&meshes)||meshes<1||meshes>256||!word(&q,"joints"))return false;
 at=q.start;if(!word(&q,"{"))return false;for(i=0;i<joints;++i){double sq;if(!tok(&q)||!q.quoted||!q.token[0]||!integer(&q,&value)||value< -1||value>=i||!vector3(&q,NULL)||!vector3(&q,&sq)||sq>1.000001)return false;}if(!word(&q,"}")||!emit(f,s,"joints.md5text",at,q.end-at,n))return false;
 for(i=0;i<meshes;++i){if(!word(&q,"mesh"))return false;at=q.start;if(!md5_mesh(&q,joints))return false;xx_rt_snprintf(label,sizeof(label),"mesh-%u.md5text",(unsigned)i);if(!emit(f,s,label,at,q.end-at,n))return false;}
 end=q.end;while(q.p<n){uint8_t c=b[q.p++];if(c!=' '&&c!='\t'&&c!='\r'&&c!='\n')return false;}s->size=(int64_t)end;return true;

}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 int64_t available=pm_available(f);uint8_t *b,probe[32];bool result;if(available<1||available>67108864||stop(pd))return false;if(available<10 || !pm_read(f,0,probe,10) || xx_rt_memcmp(probe,"\x4d\x44\x35\x56\x65\x72\x73\x69\x6f\x6e",10)) return false; b=(uint8_t *)xx_mem_alloc((size_t)available);if(!b)return false;
 result=pm_read(f,0,b,(size_t)available)&&parse_data(f,s,b,(uint64_t)available,pd);xx_mem_free(b);return result;
}

void xx_quake_md5mesh_init(xx_quake_md5mesh *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_QUAKE_MD5MESH,"md5mesh");} }
xx_quake_md5mesh *xx_quake_md5mesh_create(xx_io_device *d,int64_t b) {xx_quake_md5mesh *r=(xx_quake_md5mesh *)xx_mem_alloc(sizeof(*r));if(r)xx_quake_md5mesh_init(r,d,b);return r;}
void xx_quake_md5mesh_destroy(xx_quake_md5mesh *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_quake_md5mesh_free(xx_quake_md5mesh *r) {if(r){xx_quake_md5mesh_destroy(r);xx_mem_free(r);}}
bool xx_quake_md5mesh_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_quake_md5mesh_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
