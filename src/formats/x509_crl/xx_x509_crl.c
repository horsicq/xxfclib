/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc5280.html */
#include "xxfclib/formats/x509_crl/xx_x509_crl.h"
#include "xxfclib/data/xx_data.h"
#include "../xx_thirteenth_wrappers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;cm_tlv root,tbs,alg,sig,x,y,before,after;uint64_t at=0,p,q,version;unsigned work=0,count=0;bool ok=false;if(!nh_load(f,&b,pd))return false;NH_NEED(th_tree(&b,0,b.n,0,&work)&&cm_take(&b,&at,b.n,48,&root)&&at==b.n);p=root.value;NH_NEED(cm_take(&b,&p,root.end,48,&tbs)&&cm_take(&b,&p,root.end,48,&alg)&&cm_alg(&b,&alg)&&cm_take(&b,&p,root.end,3,&sig)&&ci_bits(&b,&sig,true)&&p==root.end);p=tbs.value;NH_NEED(cm_take(&b,&p,tbs.end,2,&x)&&th_uint(&b,&x,&version)&&version==1&&cm_take(&b,&p,tbs.end,48,&x)&&cm_alg(&b,&x)&&x.end-x.start==alg.end-alg.start&&!xx_rt_memcmp(b.p+(size_t)x.start,b.p+(size_t)alg.start,(size_t)(x.end-x.start))&&cm_take(&b,&p,tbs.end,48,&x)&&ci_name(&b,&x,false)&&th_tlv_add(f,s,&b,"issuer",&x)&&cm_read(&b,&p,tbs.end,&before)&&(before.tag==23||before.tag==24)&&cm_read(&b,&p,tbs.end,&after)&&(after.tag==23||after.tag==24)&&ci_time(&b,&before)<=ci_time(&b,&after)&&th_tlv_add(f,s,&b,"this-update",&before)&&th_tlv_add(f,s,&b,"next-update",&after));
 if(p<tbs.end&&b.p[(size_t)p]==48){NH_NEED(cm_take(&b,&p,tbs.end,48,&x));q=x.value;while(q<x.end){NH_NEED(++count<=1024&&cm_take(&b,&q,x.end,48,&y));uint64_t k=y.value;cm_tlv serial,date,ext;NH_NEED(cm_take(&b,&k,y.end,2,&serial)&&serial.end>serial.value&&serial.end-serial.value<=21&&!(b.p[(size_t)serial.value]&128)&&cm_read(&b,&k,y.end,&date)&&(date.tag==23||date.tag==24));if(k<y.end){cm_tlv wrap;wrap.tag=160;wrap.value=k;wrap.end=y.end;NH_NEED(th_extensions(&b,&wrap));k=y.end;}NH_NEED(k==y.end&&th_tlv_add(f,s,&b,"revoked-certificate",&y));(void)ext;}}
 if(p<tbs.end){NH_NEED(cm_take(&b,&p,tbs.end,160,&x)&&th_extensions(&b,&x)&&th_tlv_add(f,s,&b,"crl-extensions",&x));}NH_NEED(p==tbs.end&&th_tlv_add(f,s,&b,"signature-algorithm",&alg)&&th_tlv_add(f,s,&b,"signature",&sig));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_x509_crl_init(xx_x509_crl *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_X509_CRL,"bin");}}
xx_x509_crl *xx_x509_crl_create(xx_io_device *d,int64_t b) {xx_x509_crl *r=(xx_x509_crl *)xx_mem_alloc(sizeof(*r));if(r)xx_x509_crl_init(r,d,b);return r;}
void xx_x509_crl_destroy(xx_x509_crl *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_x509_crl_free(xx_x509_crl *r) {if(r){xx_x509_crl_destroy(r);xx_mem_free(r);}}
bool xx_x509_crl_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_x509_crl_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
