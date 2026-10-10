/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc7515.html */
#include "xxfclib/formats/jose_jws/xx_jose_jws.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_packet.h"

static void whitespace(memory_blob *b,uint64_t *at){while(*at<b->n&&(b->p[(size_t)*at]==' '||b->p[(size_t)*at]=='\r'||b->p[(size_t)*at]=='\n'||b->p[(size_t)*at]=='\t'))++*at;}
static bool string(memory_blob *b,uint64_t *at,uint64_t *p,uint64_t *n){if(*at>=b->n||b->p[(size_t)(*at)++]!='"')return false;*p=*at;while(*at<b->n&&b->p[(size_t)*at]!='"'){uint8_t c=b->p[(size_t)(*at)++];if(c<32||c>126||c=='\\'||*at-*p>64)return false;}*n=*at-*p;return *at<b->n&&b->p[(size_t)(*at)++]=='"';}
static bool protected_header(memory_blob *b){uint64_t at=0,p,n,v,z;unsigned mask=0;whitespace(b,&at);if(at>=b->n||b->p[(size_t)at++]!='{')return false;while(true){whitespace(b,&at);if(!string(b,&at,&p,&n))return false;whitespace(b,&at);if(at>=b->n||b->p[(size_t)at++]!=':')return false;whitespace(b,&at);if(!string(b,&at,&v,&z))return false;if(protocol_eq(b,p,n,"alg")){if((mask&1)||!protocol_eq(b,v,z,"HS256"))return false;mask|=1;}else if(protocol_eq(b,p,n,"typ")){if((mask&2)||!protocol_eq(b,v,z,"JWT"))return false;mask|=2;}else return false;whitespace(b,&at);if(at>=b->n)return false;uint8_t c=b->p[(size_t)at++];if(c=='}')break;if(c!=',')return false;}whitespace(b,&at);return at==b->n&&(mask&1);}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b,header;uint64_t dots[2],count=0,at=0,z=0;uint8_t *decoded=NULL;bool ok=false;if(!blob_load(f,&b,pd))return false;BLOB_NEED(b.n>=48&&b.n<=3145728);for(uint64_t i=0;i<b.n;++i){BLOB_NEED(!binary_stop(pd));if(b.p[(size_t)i]=='.'){BLOB_NEED(count<2);dots[count++]=i;}}BLOB_NEED(count==2&&dots[0]>0&&dots[0]<4096&&dots[1]>dots[0]+1&&dots[1]+1<b.n);const char *labels[3]={"decoded-protected-header","decoded-payload","encoded-signature"};for(unsigned i=0;i<3;++i){uint64_t end=i<2?dots[i]:b.n;BLOB_NEED(packet_b64(&b,at,end-at,true,&decoded,&z));if(i==0){header.p=decoded;header.n=z;header.pd=pd;BLOB_NEED(z<=2048&&protected_header(&header));}else if(i==2)BLOB_NEED(z==32);BLOB_NEED(packet_decoded(f,s,labels[i],&decoded,z));at=end+1;}s->size=(int64_t)b.n;ok=true;done:xx_mem_free(decoded);xx_mem_free(b.p);return ok;}

void xx_jose_jws_init(xx_jose_jws *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_JOSE_JWS,"bin");}}
xx_jose_jws *xx_jose_jws_create(xx_io_device *d,int64_t b){xx_jose_jws *r=(xx_jose_jws *)xx_mem_alloc(sizeof(*r));if(r)xx_jose_jws_init(r,d,b);return r;}
void xx_jose_jws_destroy(xx_jose_jws *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_jose_jws_free(xx_jose_jws *r){if(r){xx_jose_jws_destroy(r);xx_mem_free(r);}}
bool xx_jose_jws_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_jose_jws_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
