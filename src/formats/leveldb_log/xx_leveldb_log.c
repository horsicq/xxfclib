/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/google/leveldb/blob/main/doc/log_format.md */
#include "xxfclib/formats/leveldb_log/xx_leveldb_log.h"
#include "../common/xx_container_wire_helpers.h"

static bool ld_slice(Abstractformat *f,pm_stream *s,memory_blob *b,uint64_t *at,uint64_t end,const char *name) {uint64_t n,start=*at;if(!serialized_var(b,at,end,&n) || *at-start>5 || n>16777216 || !record_span(*at,n,end) || !blob_add(f,s,b,name,*at,n)) return false;*at+=n;return true;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {memory_blob b;uint64_t at=0,start,end,count,seq,last=0;unsigned records=0;bool ok=false;if(!blob_load(f,&b,pd)) return false;while(at<b.n) {uint64_t left=32768-(at%32768),n;if(left<7) {BLOB_NEED(left<=b.n-at && container_wire_zero(&b,at,left));at+=left;continue;}BLOB_NEED(blob_span(&b,at,7));n=xx_data_get_u16(b.p+(size_t)at+4, 2, 0, false);BLOB_NEED(n>=12 && n<=left-7 && b.p[(size_t)at+6]==1 && blob_span(&b,at+7,n) && container_wire_crc(&b,at+6,n+1,xx_data_get_u32(b.p+(size_t)at, 4, 0, false)));start=at+7;end=start+n;seq=xx_data_get_u64(b.p+(size_t)start, 8, 0, false);count=xx_data_get_u32(b.p+(size_t)start+8, 4, 0, false);BLOB_NEED(count>=1 && count<=2048 && (!records || seq>=last) && seq<=UINT64_MAX-count && blob_add(f,s,&b,"wal-physical-header",at,7) && blob_add(f,s,&b,"writebatch-header",start,12));last=seq+count;at=start+12;for(uint64_t i=0;i<count;++i) {uint8_t kind;BLOB_NEED(at<end);kind=b.p[(size_t)at++];BLOB_NEED(kind<=1 && ld_slice(f,s,&b,&at,end,kind ? "put-key":"delete-key"));if(kind) BLOB_NEED(ld_slice(f,s,&b,&at,end,"value"));}BLOB_NEED(at==end && ++records<=512);}BLOB_NEED(records);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_leveldb_log_init(xx_leveldb_log *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_LEVELDB_LOG,"log"); } }
xx_leveldb_log *xx_leveldb_log_create(xx_io_device *d,int64_t b) { xx_leveldb_log *r=(xx_leveldb_log *)xx_mem_alloc(sizeof(*r)); if(r) xx_leveldb_log_init(r,d,b); return r; }
void xx_leveldb_log_destroy(xx_leveldb_log *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_leveldb_log_free(xx_leveldb_log *r) { if(r) { xx_leveldb_log_destroy(r); xx_mem_free(r); } }
bool xx_leveldb_log_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_leveldb_log_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
