/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/aosp-mirror/platform_frameworks_base/blob/master/libs/androidfw/include/androidfw/ResourceTypes.h */
#include "xxfclib/formats/android_binary_xml/xx_android_binary_xml.h"
#include "../nix_nar/xx_eleventh_containers.h"

#include "../nix_nar/xx_android_wire.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {nh_blob b;aw_chunk root,c;uint64_t at,ext,i,j;uint32_t strings=0,ns[32],names[32],prefix[32],uri[32],nsDepth[32];unsigned depth=0,namespaces=0,roots=0;bool pool=false,map=false,started=false,ok=false;if(!nh_load(f,&b,pd)) return false;NH_NEED(aw_read(&b,0,b.n,&root) && root.type==3 && root.header==8 && root.end==b.n && nh_add(f,s,&b,"xml-header",0,8));at=8;
    while(at<b.n) {NH_NEED(aw_read(&b,at,b.n,&c));if(c.type==1) {NH_NEED(!pool && !started && aw_pool(&b,&c,&strings));pool=true;}
        else if(c.type==384) {NH_NEED(pool && !map && !started && c.header==8 && (c.end-at-8)/4<=strings);map=true;}
        else {NH_NEED(pool && c.header==16 && c.end-at>=16 && aw_ref(pm_le32(b.p+(size_t)at+12),strings,true));ext=at+16;started=true;
            if(c.type==256 || c.type==257) {uint32_t a,v;NH_NEED(c.end-ext==8);a=pm_le32(b.p+(size_t)ext);v=pm_le32(b.p+(size_t)ext+4);NH_NEED(aw_ref(a,strings,true) && aw_ref(v,strings,false));if(c.type==256) {NH_NEED(namespaces<32);prefix[namespaces]=a;nsDepth[namespaces]=depth;uri[namespaces++]=v;}else {NH_NEED(namespaces && prefix[namespaces-1]==a && uri[namespaces-1]==v && nsDepth[namespaces-1]==depth);--namespaces;}}
            else if(c.type==258) {uint32_t a,v;uint16_t count,start,width;NH_NEED(eh_span(ext,20,c.end));a=pm_le32(b.p+(size_t)ext);v=pm_le32(b.p+(size_t)ext+4);start=pm_le16(b.p+(size_t)ext+8);width=pm_le16(b.p+(size_t)ext+10);count=pm_le16(b.p+(size_t)ext+12);NH_NEED(aw_ref(a,strings,true) && aw_ref(v,strings,false) && depth<32 && start==20 && width==20 && count<=2048 && ext+start+(uint64_t)count*width==c.end);for(i=14;i<=18;i+=2) NH_NEED(pm_le16(b.p+(size_t)ext+(size_t)i)<=count);if(!depth) NH_NEED(++roots==1);ns[depth]=a;names[depth++]=v;for(i=0;i<count;++i) {uint64_t z=ext+20+i*20;NH_NEED(aw_ref(pm_le32(b.p+(size_t)z),strings,true) && aw_ref(pm_le32(b.p+(size_t)z+4),strings,false) && aw_ref(pm_le32(b.p+(size_t)z+8),strings,true) && aw_value(&b,z+12,c.end,strings));for(j=0;j<i;++j) NH_NEED(xx_rt_memcmp(b.p+(size_t)z,b.p+(size_t)(ext+20+j*20),8));}}
            else if(c.type==259) {NH_NEED(c.end-ext==8 && depth && pm_le32(b.p+(size_t)ext)==ns[depth-1] && pm_le32(b.p+(size_t)ext+4)==names[depth-1]);--depth;}
            else if(c.type==260) NH_NEED(depth && c.end-ext==12 && aw_ref(pm_le32(b.p+(size_t)ext),strings,false) && aw_value(&b,ext+4,c.end,strings));else NH_NEED(false);
        }NH_NEED(nh_add(f,s,&b,c.type==1 ? "string-pool":c.type==384 ? "resource-map":"xml-node",at,c.end-at));at=c.end;
    }NH_NEED(pool && roots==1 && !depth && !namespaces);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_android_binary_xml_init(xx_android_binary_xml *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ANDROID_BINARY_XML,"axml"); } }
xx_android_binary_xml *xx_android_binary_xml_create(xx_io_device *d,int64_t b) { xx_android_binary_xml *r=(xx_android_binary_xml *)xx_mem_alloc(sizeof(*r)); if(r) xx_android_binary_xml_init(r,d,b); return r; }
void xx_android_binary_xml_destroy(xx_android_binary_xml *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_android_binary_xml_free(xx_android_binary_xml *r) { if(r) { xx_android_binary_xml_destroy(r); xx_mem_free(r); } }
bool xx_android_binary_xml_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_android_binary_xml_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
