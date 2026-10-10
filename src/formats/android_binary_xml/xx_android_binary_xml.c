/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/aosp-mirror/platform_frameworks_base/blob/master/libs/androidfw/include/androidfw/ResourceTypes.h */
#include "xxfclib/formats/android_binary_xml/xx_android_binary_xml.h"
#include "../common/xx_serialized_value_helpers.h"

#include "../common/xx_android_resource_wire.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {memory_blob b;android_wire_chunk root,c;uint64_t at,ext,i,j;uint32_t strings=0,ns[32],names[32],prefix[32],uri[32],nsDepth[32];unsigned depth=0,namespaces=0,roots=0;bool pool=false,map=false,started=false,ok=false;if(!blob_load(f,&b,pd)) return false;BLOB_NEED(android_wire_read(&b,0,b.n,&root) && root.type==3 && root.header==8 && root.end==b.n && blob_add(f,s,&b,"xml-header",0,8));at=8;
    while(at<b.n) {BLOB_NEED(android_wire_read(&b,at,b.n,&c));if(c.type==1) {BLOB_NEED(!pool && !started && android_wire_pool(&b,&c,&strings));pool=true;}
        else if(c.type==384) {BLOB_NEED(pool && !map && !started && c.header==8 && (c.end-at-8)/4<=strings);map=true;}
        else {BLOB_NEED(pool && c.header==16 && c.end-at>=16 && android_wire_ref(xx_data_get_u32(b.p+(size_t)at+12, 4, 0, false),strings,true));ext=at+16;started=true;
            if(c.type==256 || c.type==257) {uint32_t a,v;BLOB_NEED(c.end-ext==8);a=xx_data_get_u32(b.p+(size_t)ext, 4, 0, false);v=xx_data_get_u32(b.p+(size_t)ext+4, 4, 0, false);BLOB_NEED(android_wire_ref(a,strings,true) && android_wire_ref(v,strings,false));if(c.type==256) {BLOB_NEED(namespaces<32);prefix[namespaces]=a;nsDepth[namespaces]=depth;uri[namespaces++]=v;}else {BLOB_NEED(namespaces && prefix[namespaces-1]==a && uri[namespaces-1]==v && nsDepth[namespaces-1]==depth);--namespaces;}}
            else if(c.type==258) {uint32_t a,v;uint16_t count,start,width;BLOB_NEED(record_span(ext,20,c.end));a=xx_data_get_u32(b.p+(size_t)ext, 4, 0, false);v=xx_data_get_u32(b.p+(size_t)ext+4, 4, 0, false);start=xx_data_get_u16(b.p+(size_t)ext+8, 2, 0, false);width=xx_data_get_u16(b.p+(size_t)ext+10, 2, 0, false);count=xx_data_get_u16(b.p+(size_t)ext+12, 2, 0, false);BLOB_NEED(android_wire_ref(a,strings,true) && android_wire_ref(v,strings,false) && depth<32 && start==20 && width==20 && count<=2048 && ext+start+(uint64_t)count*width==c.end);for(i=14;i<=18;i+=2) BLOB_NEED(xx_data_get_u16(b.p+(size_t)ext+(size_t)i, 2, 0, false)<=count);if(!depth) BLOB_NEED(++roots==1);ns[depth]=a;names[depth++]=v;for(i=0;i<count;++i) {uint64_t z=ext+20+i*20;BLOB_NEED(android_wire_ref(xx_data_get_u32(b.p+(size_t)z, 4, 0, false),strings,true) && android_wire_ref(xx_data_get_u32(b.p+(size_t)z+4, 4, 0, false),strings,false) && android_wire_ref(xx_data_get_u32(b.p+(size_t)z+8, 4, 0, false),strings,true) && android_wire_value(&b,z+12,c.end,strings));for(j=0;j<i;++j) BLOB_NEED(xx_rt_memcmp(b.p+(size_t)z,b.p+(size_t)(ext+20+j*20),8));}}
            else if(c.type==259) {BLOB_NEED(c.end-ext==8 && depth && xx_data_get_u32(b.p+(size_t)ext, 4, 0, false)==ns[depth-1] && xx_data_get_u32(b.p+(size_t)ext+4, 4, 0, false)==names[depth-1]);--depth;}
            else if(c.type==260) BLOB_NEED(depth && c.end-ext==12 && android_wire_ref(xx_data_get_u32(b.p+(size_t)ext, 4, 0, false),strings,false) && android_wire_value(&b,ext+4,c.end,strings));else BLOB_NEED(false);
        }BLOB_NEED(blob_add(f,s,&b,c.type==1 ? "string-pool":c.type==384 ? "resource-map":"xml-node",at,c.end-at));at=c.end;
    }BLOB_NEED(pool && roots==1 && !depth && !namespaces);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_android_binary_xml_init(xx_android_binary_xml *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ANDROID_BINARY_XML,"axml"); } }
xx_android_binary_xml *xx_android_binary_xml_create(xx_io_device *d,int64_t b) { xx_android_binary_xml *r=(xx_android_binary_xml *)xx_mem_alloc(sizeof(*r)); if(r) xx_android_binary_xml_init(r,d,b); return r; }
void xx_android_binary_xml_destroy(xx_android_binary_xml *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_android_binary_xml_free(xx_android_binary_xml *r) { if(r) { xx_android_binary_xml_destroy(r); xx_mem_free(r); } }
bool xx_android_binary_xml_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_android_binary_xml_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
