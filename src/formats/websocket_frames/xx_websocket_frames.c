/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc6455.html */
#include "xxfclib/formats/websocket_frames/xx_websocket_frames.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_protocol_framing.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b;uint64_t at=0,n,start,payload;unsigned frames=0;bool close=false,ok=false;uint8_t *copy=NULL;if(!blob_load(f,&b,pd))return false;
 while(at<b.n){start=at;BLOB_NEED(!close&&++frames<=1024&&protocol_take(&b,&at,b.n,2));uint8_t h=b.p[(size_t)start],v=b.p[(size_t)start+1],opcode=h&15;BLOB_NEED((h&128)&&!(h&112)&&(opcode==1||opcode==2||opcode==8||opcode==9||opcode==10));n=v&127;if(n==126){BLOB_NEED(protocol_take(&b,&at,b.n,2));n=xx_data_get_u16(b.p+(size_t)at-2, 2, 0, true);BLOB_NEED(n>=126);}else if(n==127){BLOB_NEED(protocol_take(&b,&at,b.n,8));n=xx_data_get_u64(b.p+(size_t)at-8, 8, 0, true);BLOB_NEED(n>=65536&&n<=67108864);}BLOB_NEED(opcode<8||n<=125);uint64_t mask=at;if(v&128)BLOB_NEED(protocol_take(&b,&at,b.n,4));payload=at;BLOB_NEED(protocol_take(&b,&at,b.n,n));const uint8_t *p=b.p+(size_t)payload;
 if(v&128){copy=(uint8_t *)xx_mem_alloc((size_t)(n?n:1));BLOB_NEED(copy);for(uint64_t i=0;i<n;++i){if(!(i&65535U))BLOB_NEED(!binary_stop(pd));copy[(size_t)i]=(uint8_t)(p[i]^b.p[(size_t)(mask+(i&3))]);}p=copy;}
 BLOB_NEED(opcode!=1||bounded_utf8(p,(size_t)n,pd));if(opcode==8){BLOB_NEED(n!=1);if(n>=2){uint16_t code=xx_data_get_u16(p, 2, 0, true);BLOB_NEED((code>=1000&&code<=1014&&code!=1004&&code!=1005&&code!=1006)|| (code>=3000&&code<=4999));BLOB_NEED(bounded_utf8(p+2,(size_t)n-2,pd));}close=true;}
 BLOB_NEED(blob_add(f,s,&b,"frame-header",start,payload-start));if(copy){BLOB_NEED(protocol_mem(f,s,"unmasked-payload",&copy,n));}else BLOB_NEED(blob_add(f,s,&b,"payload",payload,n));
 }BLOB_NEED(frames>=2&&close);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(copy);xx_mem_free(b.p);return ok;}

void xx_websocket_frames_init(xx_websocket_frames *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_WEBSOCKET_FRAMES,"bin");}}
xx_websocket_frames *xx_websocket_frames_create(xx_io_device *d,int64_t b) {xx_websocket_frames *r=(xx_websocket_frames *)xx_mem_alloc(sizeof(*r));if(r)xx_websocket_frames_init(r,d,b);return r;}
void xx_websocket_frames_destroy(xx_websocket_frames *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_websocket_frames_free(xx_websocket_frames *r) {if(r){xx_websocket_frames_destroy(r);xx_mem_free(r);}}
bool xx_websocket_frames_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_websocket_frames_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
