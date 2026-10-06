/* SPDX-License-Identifier: MIT
 * Independently implemented from https://mcap.dev/spec */
#include "xxfclib/formats/mcap/xx_mcap.h"
#include "../xx_ninth_data.h"

static bool mc_map(nh_blob *b,uint64_t *at,uint64_t end) {
    uint64_t stop,keys[128];uint32_t sizes[128];unsigned count=0,i;if(!eh_span(*at,4,end)) return false;stop=*at+4+pm_le32(b->p+(size_t)*at);*at+=4;if(stop>end) return false;
    while(*at<stop) {uint64_t key=*at+4;uint32_t n;if(!eh_span(*at,4,stop) || count==128) return false;n=pm_le32(b->p+(size_t)*at);if(!nh_string(b,at,stop,4,false,false)) return false;
        for(i=0;i<count;++i) { if(n==sizes[i] && !xx_rt_memcmp(b->p+(size_t)key,b->p+(size_t)keys[i],n)) return false; } keys[count]=key;sizes[count++]=n;
        if(!nh_string(b,at,stop,4,false,true)) return false;
    }return *at==stop;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[8];nh_blob b={0};bool ok=false,data_end=false,footer=false;uint64_t at=8;unsigned records=0,sc=0,ch=0,payloads=0;uint16_t schema[256],channels[256];
    if(!pm_read(f,0,h,8) || xx_rt_memcmp(h,"\x89MCAP0\r\n",8)) { return false; } NH_NEED(nh_load(f,&b,pd) && b.n>=58 && !xx_rt_memcmp(b.p+(size_t)b.n-8,h,8));
    while(at<b.n-8) {uint64_t start=at,n,end,cursor;uint8_t op;unsigned i;NH_NEED(++records<=4094 && nh_span(&b,at,9));op=b.p[(size_t)at];n=fd_le64(b.p+(size_t)at+1);at+=9;end=at+n;cursor=at;NH_NEED(end>=at && end<=b.n-8 && !footer);
        if(records==1) {NH_NEED(op==1 && nh_string(&b,&cursor,end,4,false,true) && nh_string(&b,&cursor,end,4,false,true) && cursor==end && nh_add(f,s,&b,"header",0,end));}
        else if(op==2) {uint32_t crc;NH_NEED(data_end && n==20 && nh_zero(&b,at,16) && end==b.n-8);crc=pm_le32(b.p+(size_t)at+16);NH_NEED((!crc || crc==nh_crc32(b.p+(size_t)start,25)) && nh_add(f,s,&b,"footer",start,n+9));footer=true;}
        else if(op==15) {uint32_t crc;NH_NEED(!data_end && n==4);crc=pm_le32(b.p+(size_t)at);NH_NEED(!crc || crc==nh_crc32(b.p,(size_t)start));data_end=true;NH_NEED(nh_add(f,s,&b,"data-end",start,n+9));}
        else {NH_NEED(!data_end);
            if(op==3) {uint16_t id;uint32_t bytes;NH_NEED(n>=14 && sc<256);id=pm_le16(b.p+(size_t)cursor);cursor+=2;NH_NEED(id);for(i=0;i<sc;++i) NH_NEED(schema[i]!=id);schema[sc++]=id;
                NH_NEED(nh_string(&b,&cursor,end,4,false,false) && nh_string(&b,&cursor,end,4,false,true) && eh_span(cursor,4,end));bytes=pm_le32(b.p+(size_t)cursor);cursor+=4;NH_NEED(cursor+bytes==end && nh_add(f,s,&b,"schema",at,n));
            }else if(op==4) {uint16_t id,ref;NH_NEED(n>=16 && ch<256);id=pm_le16(b.p+(size_t)cursor);ref=pm_le16(b.p+(size_t)cursor+2);cursor+=4;for(i=0;i<ch;++i) NH_NEED(channels[i]!=id);channels[ch++]=id;if(ref) {for(i=0;i<sc && schema[i]!=ref;++i) {}NH_NEED(i<sc);}
                NH_NEED(nh_string(&b,&cursor,end,4,false,false) && nh_string(&b,&cursor,end,4,false,false) && mc_map(&b,&cursor,end) && cursor==end && nh_add(f,s,&b,"channel",at,n));
            }else if(op==5) {uint16_t id;NH_NEED(n>=22);id=pm_le16(b.p+(size_t)at);for(i=0;i<ch && channels[i]!=id;++i) {}NH_NEED(i<ch && nh_add(f,s,&b,"message",at+22,n-22));++payloads;
            }else if(op==9) {uint64_t bytes,payload;uint32_t crc;NH_NEED(n>=36);cursor+=16;NH_NEED(nh_string(&b,&cursor,end,4,false,false) && nh_string(&b,&cursor,end,4,false,false) && eh_span(cursor,8,end));bytes=fd_le64(b.p+(size_t)cursor);cursor+=8;payload=cursor;NH_NEED(eh_span(cursor,bytes,end) && cursor+bytes+4==end);crc=pm_le32(b.p+(size_t)(end-4));NH_NEED(!crc || crc==nh_crc32(b.p+(size_t)at,(size_t)(n-4)));NH_NEED(nh_add(f,s,&b,"attachment",payload,bytes));++payloads;
            }else if(op==12) {NH_NEED(nh_string(&b,&cursor,end,4,false,false) && mc_map(&b,&cursor,end) && cursor==end && nh_add(f,s,&b,"metadata",at,n));}
            else NH_NEED(false);
        }at=end;
    }NH_NEED(footer && payloads && at==b.n-8 && nh_add(f,s,&b,"magic",at,8));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_mcap_init(xx_mcap *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MCAP,"mcap"); } }
xx_mcap *xx_mcap_create(xx_io_device *d,int64_t b) { xx_mcap *r=(xx_mcap *)xx_mem_alloc(sizeof(*r)); if(r) xx_mcap_init(r,d,b); return r; }
void xx_mcap_destroy(xx_mcap *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_mcap_free(xx_mcap *r) { if(r) { xx_mcap_destroy(r); xx_mem_free(r); } }
bool xx_mcap_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_mcap_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
