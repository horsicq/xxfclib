/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://www.rfc-editor.org/rfc/rfc5280.html */
#include "xxfclib/formats/x509_certificate/xx_x509_certificate.h"
#include "../xx_twelfth_a.h"

#include "../xx_twelfth_der.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {nh_blob b;cm_tlv root,tbs,alg,sig,t;uint64_t at=0,p;unsigned work=0;bool ok=false;if(!nh_load(f,&b,pd)) return false;NH_NEED(b.n>=32 && cm_tree(&b,0,b.n,0,&work) && cm_take(&b,&at,b.n,48,&root) && at==b.n && cm_cert(&b,&root));p=root.value;NH_NEED(cm_take(&b,&p,root.end,48,&tbs) && cm_take(&b,&p,root.end,48,&alg) && cm_take(&b,&p,root.end,3,&sig) && sig.end-sig.value>1 && !b.p[(size_t)sig.value]);at=tbs.value;while(at<tbs.end) {NH_NEED(cm_read(&b,&at,tbs.end,&t) && nh_add(f,s,&b,"tbs-field",t.start,t.end-t.start));}NH_NEED(nh_add(f,s,&b,"signature-algorithm",alg.start,alg.end-alg.start) && nh_add(f,s,&b,"signature",sig.value+1,sig.end-sig.value-1));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_x509_certificate_init(xx_x509_certificate *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_X509_CERTIFICATE,"crt"); } }
xx_x509_certificate *xx_x509_certificate_create(xx_io_device *d,int64_t b) { xx_x509_certificate *r=(xx_x509_certificate *)xx_mem_alloc(sizeof(*r)); if(r) xx_x509_certificate_init(r,d,b); return r; }
void xx_x509_certificate_destroy(xx_x509_certificate *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_x509_certificate_free(xx_x509_certificate *r) { if(r) { xx_x509_certificate_destroy(r); xx_mem_free(r); } }
bool xx_x509_certificate_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_x509_certificate_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
