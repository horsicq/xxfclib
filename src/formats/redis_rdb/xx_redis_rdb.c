/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/redis/redis/blob/7.2/src/rdb.c */
#include "xxfclib/formats/redis_rdb/xx_redis_rdb.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../nix_nar/xx_eleventh_containers.h"

static bool rd_len(nh_blob *b,uint64_t *at,uint64_t *n,bool *encoded) {uint8_t c;if(!nh_span(b,*at,1)) return false;c=b->p[(size_t)(*at)++];*encoded=(c>>6)==3;if(*encoded) {*n=c&63;return *n<=3;}if(c<64) *n=c;else if(c<128) {if(!nh_span(b,*at,1)) return false;*n=((uint64_t)(c&63)<<8)|b->p[(size_t)(*at)++];}else if(c==128 || c==129) {unsigned width=c==128 ? 4:8;if(!nh_span(b,*at,width)) return false;*n=width==4 ? pm_be32(b->p+(size_t)*at):ec_be64(b->p+(size_t)*at);*at+=width;}else return false;return *n<=67108864;}
static bool rd_plain(nh_blob *b,uint64_t *at,uint64_t *n) {bool encoded;return rd_len(b,at,n,&encoded) && !encoded;}
static bool rd_string(nh_blob *b,uint64_t *at) {uint64_t n,m,made=0,i;bool enc;if(!rd_len(b,at,&n,&enc)) return false;if(!enc) {if(!nh_span(b,*at,n)) return false;*at+=n;return true;}if(n<3) {n=(uint64_t)1<<n;if(!nh_span(b,*at,n)) return false;*at+=n;return true;}if(!rd_plain(b,at,&n) || !rd_plain(b,at,&m) || !m || m>1048576 || !nh_span(b,*at,n)) return false;/* Validate LZF without allocating/decompressing: produced extent and backward distances. */
    i=0;while(i<n) {uint8_t c=b->p[(size_t)(*at+i++)];uint64_t len,dist;if(c<32) {len=(uint64_t)c+1;if(!eh_span(i,len,n) || !eh_span(made,len,m)) return false;i+=len;made+=len;}else {len=c>>5;dist=(uint64_t)(c&31)<<8;if(len==7) {if(i>=n) return false;len+=b->p[(size_t)(*at+i++)];}if(i>=n) return false;dist+=b->p[(size_t)(*at+i++)]+1;len+=2;if(dist>made || !eh_span(made,len,m)) return false;made+=len;}if(fd_stop(b->pd)) return false;}*at+=n;return made==m;
}
static const xx_crc_model rd_crc_model = {
    64U, UINT64_C(0xad93d23594c935a9), 0U, true, true, 0U, "Redis CRC64"
};
static bool rd_crc(nh_blob *b,uint64_t n,uint64_t want) {
    xx_crc_context crc;
    uint64_t at=0U;
    if(!xx_crc_context_init(&crc,&rd_crc_model)) return false;
    while(at<n) {
        size_t part=n-at>65536U ? 65536U : (size_t)(n-at);
        if(fd_stop(b->pd)) return false;
        xx_crc_context_update(&crc,b->p+(size_t)at,part);
        at+=part;
    }
    return xx_crc_context_final(&crc)==want;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {nh_blob b;uint64_t at=9,start,n,i,v;unsigned ver=0,pending=0,keys=0;bool ok=false;if(!nh_load(f,&b,pd)) return false;NH_NEED(nh_span(&b,0,18) && !xx_rt_memcmp(b.p,"REDIS",5));for(i=5;i<9;++i) {NH_NEED(b.p[(size_t)i]>='0' && b.p[(size_t)i]<='9');ver=ver*10+b.p[(size_t)i]-'0';}NH_NEED(ver>=6 && ver<=11 && nh_add(f,s,&b,"rdb-header",0,9));
    while(at<b.n-8) {uint8_t type=b.p[(size_t)at++];start=at-1;if(type==255) {NH_NEED(!pending && at==b.n-8);v=ec_le64(b.p+(size_t)at);NH_NEED(!v || rd_crc(&b,at,v));NH_NEED(nh_add(f,s,&b,"rdb-trailer",start,9));at+=8;break;}
        if(type==250) {NH_NEED(!pending && rd_string(&b,&at) && rd_string(&b,&at));NH_NEED(nh_add(f,s,&b,"aux-field",start,at-start));continue;}
        if(type==254 || type==251) {NH_NEED(!pending && rd_plain(&b,&at,&n));if(type==251) NH_NEED(rd_plain(&b,&at,&v) && v<=n);NH_NEED(nh_add(f,s,&b,type==254 ? "database":"resize-hint",start,at-start));continue;}
        if(type==252 || type==253 || type==249 || type==248) {unsigned bit=type==252 || type==253 ? 1U:type==249 ? 2U:4U;NH_NEED(!(pending&bit));pending|=bit;if(type==248) NH_NEED(rd_plain(&b,&at,&n));else {n=type==252 ? 8:type==253 ? 4:1;NH_NEED(nh_span(&b,at,n));at+=n;}NH_NEED(nh_add(f,s,&b,"key-metadata",start,at-start));continue;}
        NH_NEED(type<=5 && ++keys<=2048 && rd_string(&b,&at));if(type==0) NH_NEED(rd_string(&b,&at));else {NH_NEED(rd_plain(&b,&at,&n) && n<=65536);for(i=0;i<n;++i) {NH_NEED(rd_string(&b,&at));if(type==4) NH_NEED(rd_string(&b,&at));else if(type==5) {NH_NEED(nh_span(&b,at,8) && sv_finite64(ec_le64(b.p+(size_t)at)));at+=8;}else if(type==3) {NH_NEED(nh_span(&b,at,1));v=b.p[(size_t)at++];NH_NEED(v!=253);if(v<253) {NH_NEED(v && v<=64 && nh_span(&b,at,v) && ec_number(b.p+(size_t)at,v,true));at+=v;}}}}pending=0;NH_NEED(at<=b.n-8 && nh_add(f,s,&b,"encoded-key-value",start,at-start));
    }NH_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_redis_rdb_init(xx_redis_rdb *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_REDIS_RDB,"rdb"); } }
xx_redis_rdb *xx_redis_rdb_create(xx_io_device *d,int64_t b) { xx_redis_rdb *r=(xx_redis_rdb *)xx_mem_alloc(sizeof(*r)); if(r) xx_redis_rdb_init(r,d,b); return r; }
void xx_redis_rdb_destroy(xx_redis_rdb *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_redis_rdb_free(xx_redis_rdb *r) { if(r) { xx_redis_rdb_destroy(r); xx_mem_free(r); } }
bool xx_redis_rdb_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_redis_rdb_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
