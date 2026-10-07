/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * CompDisk v5 cylinders; reference: libxad AMPK.c CompDisk wire format. */
#include "xxfclib/formats/compdisk/xx_compdisk.h"
#include "../xx_legacy_archive.h"
static bool compdisk_parse(Abstractformat *f,pm_stream *s,ac_blob *b) {
    uint32_t at=16,c; uint8_t *image; const uint32_t cylinder=11264U,total=901120U;
    if(b->n<16U || xx_rt_memcmp(b->p,"COMP",4) || xx_data_get_u32(b->p+4, 4, 0, true)!=5U || xx_data_get_u32(b->p+8, 4, 0, true)!=0U || xx_data_get_u32(b->p+12, 4, 0, true)!=0U) return false;
    image=ac_alloc(b,total); if(!image) return false;
    for(c=0;c<80U;++c) { uint32_t packed; uint16_t crc;
        if(!ac_poll(b) || !ac_span(b,at,6U)) goto fail;
        packed=xx_data_get_u32(b->p+at, 4, 0, true); crc=xx_data_get_u16(b->p+at+4U, 2, 0, true); at+=6U;
        if(!packed) { if(!ac_span(b,at,cylinder)) goto fail; xx_rt_memcpy(image+c*cylinder,b->p+at,cylinder); at+=cylinder; }
        else { if(!ac_span(b,at,packed) || !ac_unix(b,b->p+at,packed,image+c*cylinder,cylinder,12U)) goto fail; at+=packed; }
        if(ac_olaf(image+c*cylinder,cylinder)!=crc) { ac_error(b,"CompDisk cylinder checksum mismatch"); goto fail; }
    }
    if(at!=b->n) goto fail;
    ((xx_compdisk *)f)->note="80 cylinders, 2 heads, 11 sectors/track, 512-byte sectors; every Olaf checksum verified";
    return ac_memory(f,s,b,"disk.adf",image,total,b->n-16U,1);
fail: ac_release(b,image,total); return false;
}
AC_PARSE(compdisk_parse)
AC_DEFINE(compdisk,XX_FILE_TYPE_COMPDISK,"comp")
