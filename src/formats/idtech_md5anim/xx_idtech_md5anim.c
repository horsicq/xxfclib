/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/id-Software/DOOM-3/master/neo/game/anim/Anim.cpp
 * MD5Version10 animation text with strict complete hierarchy/bounds/baseframe/frame grammar. Validates one-root hierarchy, disjoint complete animated-component ranges, ordered frame IDs, finite values and reconstructed quaternion XYZ norms. Up to1024 frames/256 joints; original text components exported. Escaped/nonASCII names, comments after the final frame and playback unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#include "xxfclib/formats/idtech_md5anim/xx_idtech_md5anim.h"
#include "../audio_dolby_ac3/xx_ninth_media.h"
typedef struct lex {const uint8_t *b;uint64_t n,p,start,end;char token[256];bool quoted;xx_pd_struct *pd;} lex;
static bool tok(lex *q){unsigned i=0;uint8_t c;if(ng_stop(q->pd))return false;for(;;){while(q->p<q->n&&(q->b[q->p]==' '||q->b[q->p]=='\t'||q->b[q->p]=='\r'||q->b[q->p]=='\n'))++q->p;if(q->p+1<q->n&&q->b[q->p]=='/'&&q->b[q->p+1]=='/'){while(q->p<q->n&&q->b[q->p]!='\n')++q->p;}else break;}if(q->p>=q->n)return false;q->start=q->p;c=q->b[q->p++];q->quoted=c=='"';if(q->quoted){while(q->p<q->n&&q->b[q->p]!='"'){c=q->b[q->p++];if(c<32||c>126||c=='\\'||i>=255)return false;q->token[i++]=(char)c;}if(q->p>=q->n)return false;++q->p;}else if(c=='{'||c=='}'||c=='('||c==')'){q->token[i++]=(char)c;}else{q->token[i++]=(char)c;while(q->p<q->n){c=q->b[q->p];if(c==' '||c=='\t'||c=='\r'||c=='\n'||c=='{'||c=='}'||c=='('||c==')')break;if(c<33||c>126||i>=255)return false;q->token[i++]=(char)c;++q->p;}}q->token[i]=0;q->end=q->p;return true;}
static bool word(lex *q,const char *s){return tok(q)&&!q->quoted&&!xx_rt_strcmp(q->token,s);}
static bool integer(lex *q,int32_t *v){uint64_t value=0;unsigned i=0;bool neg=false;if(!tok(q)||q->quoted)return false;if(q->token[0]=='-'){neg=true;++i;}if(!q->token[i])return false;for(;q->token[i];++i){if(q->token[i]<'0'||q->token[i]>'9')return false;value=value*10+(unsigned)(q->token[i]-'0');if(value>2147483647U)return false;}*v=neg?-(int32_t)value:(int32_t)value;return true;}
static bool number(lex *q,double *v){const char *end;uint64_t bits;if(!tok(q)||q->quoted)return false;*v=xx_rt_strtod(q->token,&end);xx_rt_memcpy(&bits,v,8);return end>q->token&&!*end&&(bits&0x7ff0000000000000ULL)!=0x7ff0000000000000ULL&&*v>=-1e20&&*v<=1e20;}
static bool vector3(lex *q,double *sq){double x,y,z;if(!word(q,"(")||!number(q,&x)||!number(q,&y)||!number(q,&z)||!word(q,")"))return false;if(sq)*sq=x*x+y*y+z*z;return true;}
static bool ng_quick(Abstractformat *f,uint64_t n) { uint8_t h[10];return ng_probe(f,n,h,10)&&!xx_rt_memcmp(h,"MD5Version",10); }
static bool ng_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 lex q;int32_t frames,joints,components,rate,value,i,j,flags[256],starts[256];uint8_t used[1536];double base[256][3],values[1536],a,d,z;uint64_t at,end;char label[40];
 xx_mem_zero(&q,sizeof(q));xx_mem_zero(used,sizeof(used));q.b=b;q.n=n;q.pd=pd;
 if(!word(&q,"MD5Version")||!integer(&q,&value)||value!=10||!word(&q,"commandline")||!tok(&q)||!q.quoted||!word(&q,"numFrames")||!integer(&q,&frames)||frames<1||frames>1024||!word(&q,"numJoints")||!integer(&q,&joints)||joints<1||joints>256||!word(&q,"frameRate")||!integer(&q,&rate)||rate<1||rate>1000||!word(&q,"numAnimatedComponents")||!integer(&q,&components)||components<0||components>joints*6||!word(&q,"hierarchy"))return false;
 at=q.start;if(!ng_emit(f,s,"directives.md5text",0,at,n)||!word(&q,"{"))return false;
 for(i=0;i<joints;++i){int32_t count=0,k;if(!tok(&q)||!q.quoted||!q.token[0]||!integer(&q,&value)||(i==0?value!=-1:value<0||value>=i)||!integer(&q,&flags[i])||flags[i]<0||flags[i]>63||!integer(&q,&starts[i])||starts[i]<0||starts[i]>components)return false;
  for(k=0;k<6;++k) {if(flags[i]&(1<<k))++count; } if(count>components-starts[i])return false;for(k=0;k<count;++k){if(used[starts[i]+k])return false;used[starts[i]+k]=1;}
 }
 for(i=0;i<components;++i)if(!used[i])return false;
 if(!word(&q,"}")||!ng_emit(f,s,"hierarchy.md5text",at,q.end-at,n)||!word(&q,"bounds")) {return false; } at=q.start;if(!word(&q,"{"))return false;
 for(i=0;i<frames;++i){double lower[3];if(!word(&q,"("))return false;for(j=0;j<3;++j)if(!number(&q,&lower[j]))return false;if(!word(&q,")")||!word(&q,"("))return false;for(j=0;j<3;++j)if(!number(&q,&a)||a<lower[j])return false;if(!word(&q,")"))return false;}
 if(!word(&q,"}")||!ng_emit(f,s,"bounds.md5text",at,q.end-at,n)||!word(&q,"baseframe")) {return false; } at=q.start;if(!word(&q,"{"))return false;
 for(i=0;i<joints;++i){if(!vector3(&q,NULL)||!word(&q,"("))return false;for(j=0;j<3;++j)if(!number(&q,&base[i][j]))return false;if(!word(&q,")")||base[i][0]*base[i][0]+base[i][1]*base[i][1]+base[i][2]*base[i][2]>1.000001)return false;}
 if(!word(&q,"}")||!ng_emit(f,s,"baseframe.md5text",at,q.end-at,n))return false;
 for(i=0;i<frames;++i){if(!word(&q,"frame"))return false;at=q.start;if(!integer(&q,&value)||value!=i||!word(&q,"{"))return false;for(j=0;j<components;++j)if(!number(&q,&values[j]))return false;
  for(j=0;j<joints;++j){int32_t k=starts[j];if(flags[j]&1)++k;if(flags[j]&2)++k;if(flags[j]&4)++k;a=flags[j]&8?values[k++]:base[j][0];d=flags[j]&16?values[k++]:base[j][1];z=flags[j]&32?values[k++]:base[j][2];if(a*a+d*d+z*z>1.000001)return false;}
  if(!word(&q,"}")) {return false; } xx_rt_snprintf(label,sizeof(label),"frame-%u.md5text",(unsigned)i);if(!ng_emit(f,s,label,at,q.end-at,n))return false;
 }
 end=q.end;while(q.p<n){uint8_t c=b[q.p++];if(c!=' '&&c!='\t'&&c!='\r'&&c!='\n')return false;}s->size=(int64_t)end;return true;
}

void xx_idtech_md5anim_init(xx_idtech_md5anim *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_IDTECH_MD5ANIM,"md5anim");}}
xx_idtech_md5anim *xx_idtech_md5anim_create(xx_io_device *d,int64_t at) {xx_idtech_md5anim *r=(xx_idtech_md5anim *)xx_mem_alloc(sizeof(*r));if(r)xx_idtech_md5anim_init(r,d,at);return r;}
void xx_idtech_md5anim_destroy(xx_idtech_md5anim *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_idtech_md5anim_free(xx_idtech_md5anim *r) {if(r){xx_idtech_md5anim_destroy(r);xx_mem_free(r);}}
bool xx_idtech_md5anim_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_idtech_md5anim_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
