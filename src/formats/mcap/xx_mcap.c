/* SPDX-License-Identifier: MIT
 * Independently implemented from https://mcap.dev/spec */
#include "xxfclib/formats/mcap/xx_mcap.h"
#include "../common/xx_memory_blob.h"

static bool mc_map(memory_blob *b,uint64_t *at,uint64_t end) {
    uint64_t stop,keys[128];uint32_t sizes[128];unsigned count=0,i;if(!record_span(*at,4,end)) return false;stop=*at+4+xx_data_get_u32(b->p+(size_t)*at, 4, 0, false);*at+=4;if(stop>end) return false;
    while(*at<stop) {uint64_t key=*at+4;uint32_t n;if(!record_span(*at,4,stop) || count==128) return false;n=xx_data_get_u32(b->p+(size_t)*at, 4, 0, false);if(!blob_string(b,at,stop,4,false,false)) return false;
        for(i=0;i<count;++i) { if(n==sizes[i] && !xx_rt_memcmp(b->p+(size_t)key,b->p+(size_t)keys[i],n)) return false; } keys[count]=key;sizes[count++]=n;
        if(!blob_string(b,at,stop,4,false,true)) return false;
    }return *at==stop;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[8];memory_blob b={0};bool ok=false,data_end=false,footer=false;uint64_t at=8;unsigned records=0,sc=0,ch=0,payloads=0;uint16_t schema[256],channels[256];
    if(!pm_read(f,0,h,8) || xx_rt_memcmp(h,"\x89MCAP0\r\n",8)) { return false; } BLOB_NEED(blob_load(f,&b,pd) && b.n>=58 && !xx_rt_memcmp(b.p+(size_t)b.n-8,h,8));
    while(at<b.n-8) {uint64_t start=at,n,end,cursor;uint8_t op;unsigned i;BLOB_NEED(++records<=4094 && blob_span(&b,at,9));op=b.p[(size_t)at];n=xx_data_get_u64(b.p+(size_t)at+1, 8, 0, false);at+=9;end=at+n;cursor=at;BLOB_NEED(end>=at && end<=b.n-8 && !footer);
        if(records==1) {BLOB_NEED(op==1 && blob_string(&b,&cursor,end,4,false,true) && blob_string(&b,&cursor,end,4,false,true) && cursor==end && blob_add(f,s,&b,"header",0,end));}
        else if(op==2) {uint32_t crc;BLOB_NEED(data_end && n==20 && blob_zero(&b,at,16) && end==b.n-8);crc=xx_data_get_u32(b.p+(size_t)at+16, 4, 0, false);BLOB_NEED((!crc || crc==blob_crc32(b.p+(size_t)start,25)) && blob_add(f,s,&b,"footer",start,n+9));footer=true;}
        else if(op==15) {uint32_t crc;BLOB_NEED(!data_end && n==4);crc=xx_data_get_u32(b.p+(size_t)at, 4, 0, false);BLOB_NEED(!crc || crc==blob_crc32(b.p,(size_t)start));data_end=true;BLOB_NEED(blob_add(f,s,&b,"data-end",start,n+9));}
        else {BLOB_NEED(!data_end);
            if(op==3) {uint16_t id;uint32_t bytes;BLOB_NEED(n>=14 && sc<256);id=xx_data_get_u16(b.p+(size_t)cursor, 2, 0, false);cursor+=2;BLOB_NEED(id);for(i=0;i<sc;++i) BLOB_NEED(schema[i]!=id);schema[sc++]=id;
                BLOB_NEED(blob_string(&b,&cursor,end,4,false,false) && blob_string(&b,&cursor,end,4,false,true) && record_span(cursor,4,end));bytes=xx_data_get_u32(b.p+(size_t)cursor, 4, 0, false);cursor+=4;BLOB_NEED(cursor+bytes==end && blob_add(f,s,&b,"schema",at,n));
            }else if(op==4) {uint16_t id,ref;BLOB_NEED(n>=16 && ch<256);id=xx_data_get_u16(b.p+(size_t)cursor, 2, 0, false);ref=xx_data_get_u16(b.p+(size_t)cursor+2, 2, 0, false);cursor+=4;for(i=0;i<ch;++i) BLOB_NEED(channels[i]!=id);channels[ch++]=id;if(ref) {for(i=0;i<sc && schema[i]!=ref;++i) {}BLOB_NEED(i<sc);}
                BLOB_NEED(blob_string(&b,&cursor,end,4,false,false) && blob_string(&b,&cursor,end,4,false,false) && mc_map(&b,&cursor,end) && cursor==end && blob_add(f,s,&b,"channel",at,n));
            }else if(op==5) {uint16_t id;BLOB_NEED(n>=22);id=xx_data_get_u16(b.p+(size_t)at, 2, 0, false);for(i=0;i<ch && channels[i]!=id;++i) {}BLOB_NEED(i<ch && blob_add(f,s,&b,"message",at+22,n-22));++payloads;
            }else if(op==9) {uint64_t bytes,payload;uint32_t crc;BLOB_NEED(n>=36);cursor+=16;BLOB_NEED(blob_string(&b,&cursor,end,4,false,false) && blob_string(&b,&cursor,end,4,false,false) && record_span(cursor,8,end));bytes=xx_data_get_u64(b.p+(size_t)cursor, 8, 0, false);cursor+=8;payload=cursor;BLOB_NEED(record_span(cursor,bytes,end) && cursor+bytes+4==end);crc=xx_data_get_u32(b.p+(size_t)(end-4), 4, 0, false);BLOB_NEED(!crc || crc==blob_crc32(b.p+(size_t)at,(size_t)(n-4)));BLOB_NEED(blob_add(f,s,&b,"attachment",payload,bytes));++payloads;
            }else if(op==12) {BLOB_NEED(blob_string(&b,&cursor,end,4,false,false) && mc_map(&b,&cursor,end) && cursor==end && blob_add(f,s,&b,"metadata",at,n));}
            else BLOB_NEED(false);
        }at=end;
    }BLOB_NEED(footer && payloads && at==b.n-8 && blob_add(f,s,&b,"magic",at,8));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_mcap_init(xx_mcap *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MCAP,"mcap"); } }
xx_mcap *xx_mcap_create(xx_io_device *d,int64_t b) { xx_mcap *r=(xx_mcap *)xx_mem_alloc(sizeof(*r)); if(r) xx_mcap_init(r,d,b); return r; }
void xx_mcap_destroy(xx_mcap *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_mcap_free(xx_mcap *r) { if(r) { xx_mcap_destroy(r); xx_mem_free(r); } }
bool xx_mcap_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_mcap_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
