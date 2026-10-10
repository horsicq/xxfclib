/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc5280.html */
#include "xxfclib/formats/x509_crl/xx_x509_crl.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_protocol_framing.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b;der_tlv root,tbs,alg,sig,x,y,before,after;uint64_t at=0,p,q,version;unsigned work=0,count=0;bool ok=false;if(!blob_load(f,&b,pd))return false;BLOB_NEED(protocol_tree(&b,0,b.n,0,&work)&&der_take(&b,&at,b.n,48,&root)&&at==b.n);p=root.value;BLOB_NEED(der_take(&b,&p,root.end,48,&tbs)&&der_take(&b,&p,root.end,48,&alg)&&der_alg(&b,&alg)&&der_take(&b,&p,root.end,3,&sig)&&x509_bits(&b,&sig,true)&&p==root.end);p=tbs.value;BLOB_NEED(der_take(&b,&p,tbs.end,2,&x)&&protocol_uint(&b,&x,&version)&&version==1&&der_take(&b,&p,tbs.end,48,&x)&&der_alg(&b,&x)&&x.end-x.start==alg.end-alg.start&&!xx_rt_memcmp(b.p+(size_t)x.start,b.p+(size_t)alg.start,(size_t)(x.end-x.start))&&der_take(&b,&p,tbs.end,48,&x)&&x509_name(&b,&x,false)&&protocol_tlv_add(f,s,&b,"issuer",&x)&&der_read(&b,&p,tbs.end,&before)&&(before.tag==23||before.tag==24)&&der_read(&b,&p,tbs.end,&after)&&(after.tag==23||after.tag==24)&&x509_time(&b,&before)<=x509_time(&b,&after)&&protocol_tlv_add(f,s,&b,"this-update",&before)&&protocol_tlv_add(f,s,&b,"next-update",&after));
 if(p<tbs.end&&b.p[(size_t)p]==48){BLOB_NEED(der_take(&b,&p,tbs.end,48,&x));q=x.value;while(q<x.end){BLOB_NEED(++count<=1024&&der_take(&b,&q,x.end,48,&y));uint64_t k=y.value;der_tlv serial,date,ext;BLOB_NEED(der_take(&b,&k,y.end,2,&serial)&&serial.end>serial.value&&serial.end-serial.value<=21&&!(b.p[(size_t)serial.value]&128)&&der_read(&b,&k,y.end,&date)&&(date.tag==23||date.tag==24));if(k<y.end){der_tlv wrap;wrap.tag=160;wrap.value=k;wrap.end=y.end;BLOB_NEED(protocol_extensions(&b,&wrap));k=y.end;}BLOB_NEED(k==y.end&&protocol_tlv_add(f,s,&b,"revoked-certificate",&y));(void)ext;}}
 if(p<tbs.end){BLOB_NEED(der_take(&b,&p,tbs.end,160,&x)&&protocol_extensions(&b,&x)&&protocol_tlv_add(f,s,&b,"crl-extensions",&x));}BLOB_NEED(p==tbs.end&&protocol_tlv_add(f,s,&b,"signature-algorithm",&alg)&&protocol_tlv_add(f,s,&b,"signature",&sig));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_x509_crl_init(xx_x509_crl *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_X509_CRL,"bin");}}
xx_x509_crl *xx_x509_crl_create(xx_io_device *d,int64_t b) {xx_x509_crl *r=(xx_x509_crl *)xx_mem_alloc(sizeof(*r));if(r)xx_x509_crl_init(r,d,b);return r;}
void xx_x509_crl_destroy(xx_x509_crl *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_x509_crl_free(xx_x509_crl *r) {if(r){xx_x509_crl_destroy(r);xx_mem_free(r);}}
bool xx_x509_crl_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_x509_crl_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
