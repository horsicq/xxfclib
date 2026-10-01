/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/facebook/rocksdb/blob/main/db/blob/blob_log_format.h */
#include "xxfclib/formats/rocksdb_blob/xx_rocksdb_blob.h"
#include "../xx_twelfth_a.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {nh_blob b;uint64_t at=30,end,key,value,exp,count=0,lo=UINT64_MAX,hi=0,hlo,hhi,flo,fhi;bool ttl,ok=false;if(!nh_load(f,&b,pd)) return false;NH_NEED(b.n>=94 && pm_le32(b.p)==2395959 && pm_le32(b.p+4)==1 && b.p[12]<=1 && !b.p[13]);ttl=b.p[12]!=0;hlo=ec_le64(b.p+14);hhi=ec_le64(b.p+22);end=b.n-32;NH_NEED((ttl ? hlo<=hhi:!hlo && !hhi) && pm_le32(b.p+(size_t)end)==2395959 && tw_crc(&b,end,28,pm_le32(b.p+(size_t)end+28)) && nh_add(f,s,&b,"blob-header",0,30));while(at<end) {NH_NEED(eh_span(at,32,end) && nh_span(&b,at,32));key=ec_le64(b.p+(size_t)at);value=ec_le64(b.p+(size_t)at+8);exp=ec_le64(b.p+(size_t)at+16);NH_NEED(key<=16777216 && value<=33554432 && key<=end-at-32 && value<=end-at-32-key && tw_crc(&b,at,24,pm_le32(b.p+(size_t)at+24)) && tw_crc(&b,at+32,key+value,pm_le32(b.p+(size_t)at+28)) && (ttl ? exp>=hlo && exp<=hhi:!exp));if(exp<lo) lo=exp;if(exp>hi) hi=exp;NH_NEED(++count<=1024 && nh_add(f,s,&b,"blob-record-header",at,32) && nh_add(f,s,&b,"key",at+32,key) && nh_add(f,s,&b,"value",at+32+key,value));at+=32+key+value;}flo=ec_le64(b.p+(size_t)end+12);fhi=ec_le64(b.p+(size_t)end+20);NH_NEED(count && count==ec_le64(b.p+(size_t)end+4) && flo==lo && fhi==hi && nh_add(f,s,&b,"blob-footer",end,32));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_rocksdb_blob_init(xx_rocksdb_blob *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ROCKSDB_BLOB,"blob"); } }
xx_rocksdb_blob *xx_rocksdb_blob_create(xx_io_device *d,int64_t b) { xx_rocksdb_blob *r=(xx_rocksdb_blob *)xx_mem_alloc(sizeof(*r)); if(r) xx_rocksdb_blob_init(r,d,b); return r; }
void xx_rocksdb_blob_destroy(xx_rocksdb_blob *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_rocksdb_blob_free(xx_rocksdb_blob *r) { if(r) { xx_rocksdb_blob_destroy(r); xx_mem_free(r); } }
bool xx_rocksdb_blob_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_rocksdb_blob_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
