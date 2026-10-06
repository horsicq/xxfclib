/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native ACU from published AppleLink format facts. Header CRC and <=256-byte
 * data CRC are checked. ACU's original larger-data CRC is undocumented/broken;
 * exact expansion length/framing is verified, not a invented checksum bypass.
 */
#include "xxfclib/formats/applelink_pe/xx_applelink_pe.h"
#include "../apple_family/xx_apple_family_private.h"
#include "../apple_family/xx_apple_squeeze.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    af_work w;af_blob b;uint32_t at=20U,i,count;bool ok=false;xx_applelink_pe *r=(xx_applelink_pe *)f;
    if(!af_init(&w,f,s,pd) || !af_load(&w,&b))return false;
    if(b.n<20U || xx_rt_memcmp(b.p+4,"fZink",5) || b.p[9]!=1U || pm_le16(b.p+10)!=54U ||
       !af_zero(b.p+12,7) || (count=pm_le16(b.p))>AF_COUNT_MAX)goto done;
    r->note="ACU header CRC verified; data CRC verified through 256 bytes; longer data CRC undefined in original producer";
    for(i=0;i<count;++i) {
        const uint8_t *h;char name[96],forkname[96];uint32_t fn,packed[2],plain[2],j;uint16_t crc;
        if(!af_range(&b,at,54U) || !af_poll(&w)) {goto done; } h=b.p+at;fn=pm_le16(h+50);
        if(!af_range(&b,at+54U,fn) || !af_name(name,sizeof(name),h+54,fn,false) || pm_le16(h+30))goto done;
        crc=af_crc16(&w,h,52U,0U);crc=af_crc16(&w,h+54U,fn,crc);
        if(!af_poll(&w) || crc!=pm_le16(h+52))goto done;
        packed[0]=pm_le32(h+14);packed[1]=pm_le32(h+18);plain[0]=pm_le32(h+34);plain[1]=pm_le32(h+38);at+=54U+fn;
        if(pm_le16(h+32)==13U) {
            if(packed[0] || packed[1] || plain[0] || plain[1])goto done;
            if(!af_add(&w,name,0,0,NULL)) {goto done; } s->items[s->count-1U].compression_method=65535U;continue;
        }
        for(j=0;j<2U;++j) {
            uint8_t *out;const uint8_t *p;const char *label;uint8_t method=h[j];uint16_t expected=pm_le16(h+2U+j*2U);
            if(!af_range(&b,at,packed[j]) || (method!=0U && method!=3U) || plain[j]>w.member_limit)goto done;
            p=b.p+at;at+=packed[j];
            if(!packed[j] && !plain[j]) { if(j==0U)continue; }
            if(j==0U) { if(xx_rt_strlen(name)+9U>=sizeof(forkname))goto done;
                xx_rt_snprintf(forkname,sizeof(forkname),"%s.resource",name);label=forkname; }
            else label=name;
            out=af_alloc(&w,plain[j],true);if(!out)goto done;
            if(method==0U) { if(packed[j]!=plain[j]) {af_release(&w,out,plain[j]);goto done;}if(plain[j])xx_rt_memcpy(out,p,plain[j]); }
            else if(!as_decode(&w,p,packed[j],out,plain[j])) {af_release(&w,out,plain[j]);goto done;}
            if((plain[j]<=256U && af_crc16(&w,out,plain[j],0U)!=expected) || !af_add(&w,label,0,plain[j],out)) {af_release(&w,out,plain[j]);goto done;}
            s->items[s->count-1U].packed_size=packed[j];s->items[s->count-1U].compression_method=method;
        }
    }
    if(at!=b.n) {goto done; } r->number_of_records=s->count;s->size=b.n;ok=af_poll(&w);
done:af_release(&w,b.p,b.n);return ok;
}
AF_DEFINE_READER(applelink_pe,XX_FILE_TYPE_APPLELINK_PE,"acu")

