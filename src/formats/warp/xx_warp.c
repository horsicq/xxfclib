/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Warp v1.1 independent track-side records. Each record has an IBM CRC16
 * over 5632 sector bytes plus 176 label bytes. Reference: libxad AMPK.c. */
#include "xxfclib/formats/warp/xx_warp.h"
#include "../xx_legacy_archive.h"
static bool warp_parse(Abstractformat *f,pm_stream *s,ac_blob *b) {
    uint32_t at=0,seen[5]={0};
    while(at<b->n) { uint32_t packed,key; uint16_t track,method,crc; uint8_t *out,*labels; char name[64];
        if(!ac_poll(b) || !ac_span(b,at,26U) || xx_rt_memcmp(b->p+at,"Warp v1.1",10)) return false;
        track=pm_be16(b->p+at+10); method=pm_be16(b->p+at+18); crc=pm_be16(b->p+at+20); packed=pm_be32(b->p+at+22);
        if(track>=80U || pm_be16(b->p+at+16)!=1U || method>3U ||
            (xx_rt_memcmp(b->p+at+12,"TOP",4) && xx_rt_memcmp(b->p+at+12,"BOT",4))) return false;
        key=track*2U+(b->p[at+12]=='B'); if(seen[key>>5U]&(1U<<(key&31U))) return false; seen[key>>5U]|=1U<<(key&31U);
        at+=26U; if(!ac_span(b,at,packed)) return false;
        out=ac_alloc(b,5808U); if(!out) return false;
        if(!ac_warp_decode(b,b->p+at,packed,out,5808U,method) || ac_crc16(out,5808U)!=crc) { ac_release(b,out,5808U); return ac_error(b,"Warp decode or track checksum failed"); }
        labels=ac_alloc(b,176U); if(!labels) { ac_release(b,out,5808U); return false; }
        xx_rt_memcpy(labels,out+5632U,176U);
        xx_rt_snprintf(name,sizeof(name),"track-%02u-side-%u.bin",track,key&1U);
        if(!ac_compact(b,&out,5808U,5632U)) { ac_release(b,out,5808U); ac_release(b,labels,176U); return false; }
        if(!ac_memory(f,s,b,name,out,5632U,packed,method)) { ac_release(b,labels,176U); return false; }
        xx_rt_snprintf(name,sizeof(name),"track-%02u-side-%u.labels",track,key&1U);
        if(!ac_memory(f,s,b,name,labels,176U,0,0)) return false;
        at+=packed;
    }
    ((xx_warp *)f)->note="decoded track components; no unrepresented tracks synthesized; sector labels preserved separately";
    return at==b->n && s->count;
}
AC_PARSE(warp_parse)
AC_DEFINE(warp,XX_FILE_TYPE_WARP,"wrp")
