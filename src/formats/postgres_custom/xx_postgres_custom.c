/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/postgres/postgres/blob/REL_16_STABLE/src/bin/pg_dump/pg_backup_custom.c */
#include "xxfclib/formats/postgres_custom/xx_postgres_custom.h"
#include "../common/xx_serialized_value_helpers.h"

static bool pg_int(memory_blob *b,uint64_t *at,int64_t *v) {uint32_t n;uint8_t sign;if(!blob_span(b,*at,5)) return false;sign=b->p[(size_t)*at];n=xx_data_get_u32(b->p+(size_t)*at+1, 4, 0, false);*at+=5;if(sign>1 || n>2147483647U) return false;*v=sign ? -(int64_t)n:n;return true;}
static bool pg_str(memory_blob *b,uint64_t *at,bool *null) {int64_t n;if(!pg_int(b,at,&n) || n< -1 || n>65536) return false;*null=n==-1;if(*null) return true;if(!serialized_utf(b,*at,(uint64_t)n)) return false;*at+=(uint64_t)n;return true;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {memory_blob b;uint64_t at=11,start,i,j,n,offsets[1024];int64_t v,count,ids[1024];uint8_t states[1024],dump[1024],seen[1024];unsigned ver;bool ok=false,isnull;if(!blob_load(f,&b,pd)) return false;BLOB_NEED(blob_span(&b,0,12) && !xx_rt_memcmp(b.p,"PGDMP",5) && b.p[5]==1 && b.p[6]>=13 && b.p[6]<=15 && b.p[7]==0 && b.p[8]==4 && b.p[9]==8 && b.p[10]==1);ver=b.p[6];if(ver>=15) {BLOB_NEED(!b.p[(size_t)at++]);}else BLOB_NEED(pg_int(&b,&at,&v) && !v);
    for(i=0;i<7;++i) { BLOB_NEED(pg_int(&b,&at,&v)); } for(i=0;i<3;++i) BLOB_NEED(pg_str(&b,&at,&isnull) && !isnull);BLOB_NEED(blob_add(f,s,&b,"dump-header",0,at) && pg_int(&b,&at,&count) && count>0 && count<=1024);xx_mem_zero(seen,sizeof(seen));
    for(i=0;i<(uint64_t)count;++i) {start=at;BLOB_NEED(pg_int(&b,&at,&ids[i]) && ids[i]>0);for(j=0;j<i;++j) BLOB_NEED(ids[j]!=ids[i]);BLOB_NEED(pg_int(&b,&at,&v) && v>=0 && v<=1);dump[i]=(uint8_t)v;for(j=0;j<4;++j) BLOB_NEED(pg_str(&b,&at,&isnull) && !isnull);BLOB_NEED(pg_int(&b,&at,&v) && v>=0 && v<=3);for(j=0;j<(ver>=14 ? 8U:7U);++j) BLOB_NEED(pg_str(&b,&at,&isnull));j=0;do {BLOB_NEED(++j<=1024 && pg_str(&b,&at,&isnull));}while(!isnull);BLOB_NEED(blob_span(&b,at,9));states[i]=b.p[(size_t)at++];offsets[i]=xx_data_get_u64(b.p+(size_t)at, 8, 0, false);at+=8;BLOB_NEED(states[i]>=1 && states[i]<=3 && (states[i]==2 || !offsets[i]) && (dump[i] || states[i]==3));BLOB_NEED(blob_add(f,s,&b,"toc-entry",start,at-start));}
    while(at<b.n) {start=at;BLOB_NEED(blob_span(&b,at,1) && b.p[(size_t)at++]==1 && pg_int(&b,&at,&v));for(i=0;i<(uint64_t)count && ids[i]!=v;++i) {}BLOB_NEED(i<(uint64_t)count && dump[i] && states[i]!=3 && !seen[i] && (states[i]!=2 || offsets[i]==start));seen[i]=1;do {BLOB_NEED(pg_int(&b,&at,&v) && v>=0);n=(uint64_t)v;BLOB_NEED(blob_span(&b,at,n));if(n) BLOB_NEED(blob_add(f,s,&b,"table-data",at,n));at+=n;}while(n);}
    for(i=0;i<(uint64_t)count;++i) { BLOB_NEED(!dump[i] || states[i]==3 || seen[i]); } s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_postgres_custom_init(xx_postgres_custom *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_POSTGRES_CUSTOM,"dump"); } }
xx_postgres_custom *xx_postgres_custom_create(xx_io_device *d,int64_t b) { xx_postgres_custom *r=(xx_postgres_custom *)xx_mem_alloc(sizeof(*r)); if(r) xx_postgres_custom_init(r,d,b); return r; }
void xx_postgres_custom_destroy(xx_postgres_custom *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_postgres_custom_free(xx_postgres_custom *r) { if(r) { xx_postgres_custom_destroy(r); xx_mem_free(r); } }
bool xx_postgres_custom_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_postgres_custom_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
