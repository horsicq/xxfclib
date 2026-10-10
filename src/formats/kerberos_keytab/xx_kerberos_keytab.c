/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://web.mit.edu/~kerberos/krb5-1.20/doc/formats/keytab_file_format.html */
#include "xxfclib/formats/kerberos_keytab/xx_kerberos_keytab.h"
#include "../common/xx_container_wire_helpers.h"

static bool kt_data(memory_blob *b,uint64_t *at,uint64_t end) {uint64_t n;if(!record_span(*at,2,end)) return false;n=xx_data_get_u16(b->p+(size_t)*at, 2, 0, true);*at+=2;if(!n || n>65535 || !record_span(*at,n,end) || !serialized_utf(b,*at,n)) return false;*at+=n;return true;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {memory_blob b;uint64_t at=2,end,start,n,key,p;unsigned entries=0;bool ok=false;if(!blob_load(f,&b,pd)) return false;BLOB_NEED(b.n>=20 && b.p[0]==5 && b.p[1]==2 && blob_add(f,s,&b,"keytab-version",0,2));while(at<b.n) {int64_t length;BLOB_NEED(blob_span(&b,at,4));start=at;uint32_t u=xx_data_get_u32(b.p+(size_t)at, 4, 0, true);length=(u&0x80000000U) ? (int64_t)u-4294967296LL:u;at+=4;if(!length) {BLOB_NEED(at==b.n && blob_add(f,s,&b,"keytab-end",start,4));break;}if(length<0) {n=(uint64_t)(-length);BLOB_NEED(container_wire_zero(&b,at,n) && blob_add(f,s,&b,"deleted-hole",start,n+4));at+=n;continue;}n=(uint64_t)length;BLOB_NEED(n<=1048576 && blob_span(&b,at,n));end=at+n;p=at;BLOB_NEED(record_span(at,2,end));uint16_t count=xx_data_get_u16(b.p+(size_t)at, 2, 0, true);at+=2;BLOB_NEED(count && count<=32 && kt_data(&b,&at,end));for(unsigned i=0;i<count;++i) BLOB_NEED(kt_data(&b,&at,end));BLOB_NEED(record_span(at,13,end));at+=4;BLOB_NEED(blob_add(f,s,&b,"principal",p,at-p));p=at;key=xx_data_get_u16(b.p+(size_t)at+7, 2, 0, true);BLOB_NEED(xx_data_get_u16(b.p+(size_t)at+5, 2, 0, true) && key && key<=4096 && record_span(at+9,key,end) && blob_add(f,s,&b,"key-metadata",p,9));at+=9;BLOB_NEED(blob_add(f,s,&b,"key",at,key));at+=key;if(end-at>=4) {BLOB_NEED(blob_add(f,s,&b,"extended-kvno",at,4));at+=4;}BLOB_NEED(container_wire_zero(&b,at,end-at) && ++entries<=1024);at=end;}BLOB_NEED(entries);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_kerberos_keytab_init(xx_kerberos_keytab *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_KERBEROS_KEYTAB,"keytab"); } }
xx_kerberos_keytab *xx_kerberos_keytab_create(xx_io_device *d,int64_t b) { xx_kerberos_keytab *r=(xx_kerberos_keytab *)xx_mem_alloc(sizeof(*r)); if(r) xx_kerberos_keytab_init(r,d,b); return r; }
void xx_kerberos_keytab_destroy(xx_kerberos_keytab *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_kerberos_keytab_free(xx_kerberos_keytab *r) { if(r) { xx_kerberos_keytab_destroy(r); xx_mem_free(r); } }
bool xx_kerberos_keytab_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_kerberos_keytab_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
