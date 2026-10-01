/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc9112.html */
#include "xxfclib/formats/http1_message/xx_http1_message.h"
#include "../xx_thirteenth_wrappers.h"

static bool token(uint8_t c){return (c>=48&&c<=57)||(c>=65&&c<=90)||(c>=97&&c<=122)||c=='!'||c=='#'||c=='$'||c=='%'||c=='&'||c==39||c=='*'||c=='+'||c=='-'||c=='.'||c=='^'||c=='_'||c=='`'||c=='|'||c=='~';}
static bool field(nh_blob *b,uint64_t p,uint64_t n,uint64_t *colon){uint64_t i=0;while(i<n && token(b->p[(size_t)(p+i)]))++i;if(!i||i==n||b->p[(size_t)(p+i)]!=':')return false;*colon=p+i;for(++i;i<n;++i){uint8_t c=b->p[(size_t)(p+i)];if((c<32 && c!=9)||c==127)return false;}return true;}
static bool lower(nh_blob *b,uint64_t p,uint64_t n,const char *v){if(xx_rt_strlen(v)!=n)return false;for(uint64_t i=0;i<n;++i){uint8_t c=b->p[(size_t)(p+i)];if(c>=65&&c<=90)c=(uint8_t)(c+32);if(c!=(uint8_t)v[i])return false;}return true;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;uint64_t at=0,start,n,colon,len=0,body;unsigned messages=0;bool ok=false;if(!nh_load(f,&b,pd))return false;
 while(at<b.n){uint64_t headers=at;bool cl=false,chunked=false,host=false,response=false;NH_NEED(++messages<=128 && th_line(&b,&at,&start,&n,true) && n>=14);uint64_t a=0,c=0;while(a<n && b.p[(size_t)(start+a)]!=' ')++a;NH_NEED(a<n);c=a+1;while(c<n && b.p[(size_t)(start+c)]!=' ')++c;NH_NEED(c<n);for(uint64_t i=0;i<n;++i)NH_NEED(b.p[(size_t)(start+i)]>=32&&b.p[(size_t)(start+i)]<127);
 if(th_eq(&b,start,a,"HTTP/1.1")){response=true;NH_NEED(c-a==4 && b.p[(size_t)(start+a+1)]>='1'&&b.p[(size_t)(start+a+1)]<='5' && b.p[(size_t)(start+a+2)]>='0'&&b.p[(size_t)(start+a+2)]<='9'&&b.p[(size_t)(start+a+3)]>='0'&&b.p[(size_t)(start+a+3)]<='9');}else{NH_NEED((th_eq(&b,start,a,"GET")||th_eq(&b,start,a,"POST")||th_eq(&b,start,a,"PUT")||th_eq(&b,start,a,"DELETE")||th_eq(&b,start,a,"HEAD")||th_eq(&b,start,a,"OPTIONS")||th_eq(&b,start,a,"PATCH")) && th_eq(&b,start+c+1,n-c-1,"HTTP/1.1") && c>a+1);for(uint64_t i=a+1;i<c;++i)NH_NEED(b.p[(size_t)(start+i)]>32 && b.p[(size_t)(start+i)]<127);}
 unsigned fields=0;while(true){NH_NEED(++fields<=256 && th_line(&b,&at,&start,&n,true));if(!n)break;NH_NEED(field(&b,start,n,&colon));uint64_t v=colon+1,end=start+n;while(v<end&&(b.p[(size_t)v]==' '||b.p[(size_t)v]==9))++v;while(end>v&&(b.p[(size_t)end-1]==' '||b.p[(size_t)end-1]==9))--end;
 if(lower(&b,start,colon-start,"content-length")){NH_NEED(!cl&&!chunked && th_dec(&b,v,end-v,&len));cl=true;}else if(lower(&b,start,colon-start,"transfer-encoding")){NH_NEED(!cl&&!chunked&&lower(&b,v,end-v,"chunked"));chunked=true;}else if(lower(&b,start,colon-start,"host")){NH_NEED(!host&&end>v);host=true;}}
 NH_NEED((response||host) && (cl||chunked) && nh_add(f,s,&b,"http-headers",headers,at-headers));body=at;
 if(cl){NH_NEED(th_take(&b,&at,b.n,len)&&nh_add(f,s,&b,"body",body,len));}else{unsigned chunks=0;while(true){uint64_t count=0;NH_NEED(++chunks<=1024 && th_line(&b,&at,&start,&n,true) && n && n<=8);for(uint64_t i=0;i<n;++i){uint8_t z=b.p[(size_t)(start+i)];unsigned digit=z>='0'&&z<='9'?z-'0':z>='a'&&z<='f'?z-'a'+10:z>='A'&&z<='F'?z-'A'+10:16;NH_NEED(digit<16);count=(count<<4)|digit;}NH_NEED(count<=67108864);if(!count){uint64_t trailers=at;unsigned trailer_count=0;while(true){NH_NEED(++trailer_count<=256);NH_NEED(th_line(&b,&at,&start,&n,true));if(!n)break;NH_NEED(field(&b,start,n,&colon)&&!lower(&b,start,colon-start,"content-length")&&!lower(&b,start,colon-start,"transfer-encoding"));}NH_NEED(nh_add(f,s,&b,"chunk-trailers",trailers,at-trailers));break;}body=at;NH_NEED(th_take(&b,&at,b.n,count)&&nh_add(f,s,&b,"chunk",body,count)&&th_take(&b,&at,b.n,2)&&b.p[(size_t)at-2]=='\r'&&b.p[(size_t)at-1]=='\n');}}
 }s->size=(int64_t)b.n;ok=messages>0;done:xx_mem_free(b.p);return ok;}

void xx_http1_message_init(xx_http1_message *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_HTTP1_MESSAGE,"bin");}}
xx_http1_message *xx_http1_message_create(xx_io_device *d,int64_t b) {xx_http1_message *r=(xx_http1_message *)xx_mem_alloc(sizeof(*r));if(r)xx_http1_message_init(r,d,b);return r;}
void xx_http1_message_destroy(xx_http1_message *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_http1_message_free(xx_http1_message *r) {if(r){xx_http1_message_destroy(r);xx_mem_free(r);}}
bool xx_http1_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_http1_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
