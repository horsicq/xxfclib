/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc5321.html */
#include "xxfclib/formats/smtp_transcript/xx_smtp_transcript.h"
#include "xxfclib/data/xx_data.h"
#include "../xx_fourteenth_wrappers.h"

static bool address(nh_blob *b,uint64_t at,uint64_t n,const char *prefix){uint64_t z=xx_rt_strlen(prefix);if(n<=z+2||!fw_ci(b,at,z,prefix)||b->p[(size_t)(at+z)]!='<'||b->p[(size_t)(at+n-1)]!='>')return false;for(uint64_t i=z+1;i+1<n;++i){uint8_t c=b->p[(size_t)(at+i)];if(c<33||c>126||c=='<'||c=='>')return false;}return true;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;uint8_t *body=NULL;bool ok=false;uint64_t at=0,p,n,z=0;unsigned rcpt=0;if(!nh_load(f,&b,pd))return false;NH_NEED(th_line(&b,&at,&p,&n,true)&&n>5&&fw_ci(&b,p,5,"ehlo ")&&nh_ascii(b.p+(size_t)p,(size_t)n,false)&&nh_add(f,s,&b,"ehlo",p,n+2));NH_NEED(th_line(&b,&at,&p,&n,true)&&address(&b,p,n,"mail from:")&&nh_add(f,s,&b,"mail-from",p,n+2));while(at<b.n){uint64_t old=at;NH_NEED(th_line(&b,&at,&p,&n,true));if(fw_ci(&b,p,n,"data")){NH_NEED(rcpt>0&&nh_add(f,s,&b,"data-command",p,n+2));break;}NH_NEED(++rcpt<=256&&address(&b,p,n,"rcpt to:")&&nh_add(f,s,&b,"recipient",old,at-old));}body=(uint8_t *)xx_mem_alloc((size_t)b.n);NH_NEED(body);bool terminal=false;while(at<b.n){NH_NEED(th_line(&b,&at,&p,&n,true)&&n<=998);if(n==1&&b.p[(size_t)p]=='.'){terminal=true;break;}if(n&&b.p[(size_t)p]=='.'){NH_NEED(n>=2&&b.p[(size_t)p+1]=='.');++p;--n;}NH_NEED(nh_ascii(b.p+(size_t)p,(size_t)n,false));xx_rt_memcpy(body+(size_t)z,b.p+(size_t)p,(size_t)n);z+=n;body[z++]=13;body[z++]=10;}NH_NEED(terminal&&z>0&&th_mem(f,s,"decoded-mail-data",&body,z)&&th_line(&b,&at,&p,&n,true)&&fw_ci(&b,p,n,"quit")&&nh_add(f,s,&b,"quit",p,n+2)&&at==b.n);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(body);xx_mem_free(b.p);return ok;}

void xx_smtp_transcript_init(xx_smtp_transcript *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_SMTP_TRANSCRIPT,"bin");}}
xx_smtp_transcript *xx_smtp_transcript_create(xx_io_device *d,int64_t b){xx_smtp_transcript *r=(xx_smtp_transcript *)xx_mem_alloc(sizeof(*r));if(r)xx_smtp_transcript_init(r,d,b);return r;}
void xx_smtp_transcript_destroy(xx_smtp_transcript *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_smtp_transcript_free(xx_smtp_transcript *r){if(r){xx_smtp_transcript_destroy(r);xx_mem_free(r);}}
bool xx_smtp_transcript_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_smtp_transcript_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
