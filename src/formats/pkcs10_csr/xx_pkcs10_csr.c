/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://www.rfc-editor.org/rfc/rfc2986.html */
#include "xxfclib/formats/pkcs10_csr/xx_pkcs10_csr.h"
#include "../xx_twelfth_a.h"

#include "../xx_twelfth_der.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {nh_blob b;cm_tlv root,info,alg,sig,x,y;uint64_t at=0,p,q;unsigned work=0;bool ok=false;if(!nh_load(f,&b,pd)) return false;NH_NEED(b.n>=24 && cm_tree(&b,0,b.n,0,&work) && cm_take(&b,&at,b.n,48,&root) && at==b.n);p=root.value;NH_NEED(cm_take(&b,&p,root.end,48,&info) && cm_take(&b,&p,root.end,48,&alg) && cm_alg(&b,&alg) && cm_take(&b,&p,root.end,3,&sig) && p==root.end && sig.end-sig.value>1 && !b.p[(size_t)sig.value]);at=info.value;NH_NEED(cm_take(&b,&at,info.end,2,&x) && x.end-x.value==1 && !b.p[(size_t)x.value] && nh_add(f,s,&b,"request-version",x.start,x.end-x.start));NH_NEED(cm_take(&b,&at,info.end,48,&x) && ci_name(&b,&x,true) && nh_add(f,s,&b,"subject",x.start,x.end-x.start) && cm_take(&b,&at,info.end,48,&x));q=x.value;NH_NEED(cm_take(&b,&q,x.end,48,&y) && cm_alg(&b,&y) && cm_take(&b,&q,x.end,3,&y) && y.end-y.value>1 && !b.p[(size_t)y.value] && q==x.end && nh_add(f,s,&b,"public-key-info",x.start,x.end-x.start));NH_NEED(cm_take(&b,&at,info.end,160,&x) && at==info.end && (x.value==x.end || cm_attr(&b,&x)) && nh_add(f,s,&b,"attributes",x.start,x.end-x.start) && nh_add(f,s,&b,"signature-algorithm",alg.start,alg.end-alg.start) && nh_add(f,s,&b,"signature",sig.value+1,sig.end-sig.value-1));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_pkcs10_csr_init(xx_pkcs10_csr *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_PKCS10_CSR,"csr"); } }
xx_pkcs10_csr *xx_pkcs10_csr_create(xx_io_device *d,int64_t b) { xx_pkcs10_csr *r=(xx_pkcs10_csr *)xx_mem_alloc(sizeof(*r)); if(r) xx_pkcs10_csr_init(r,d,b); return r; }
void xx_pkcs10_csr_destroy(xx_pkcs10_csr *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_pkcs10_csr_free(xx_pkcs10_csr *r) { if(r) { xx_pkcs10_csr_destroy(r); xx_mem_free(r); } }
bool xx_pkcs10_csr_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_pkcs10_csr_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
