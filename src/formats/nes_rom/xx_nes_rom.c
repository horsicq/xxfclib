/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/libretro/libretro-fceumm/blob/master/src/ines.c
 * Independently implemented bounded parser; input ownership remains with caller.
 */
#include "xxfclib/formats/nes_rom/xx_nes_rom.h"
#include "../xx_payload_members.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    (void)pd;

    uint8_t h[16]; int64_t at=16,prg,chr; bool nes2;
    if(!pm_read(f,0,h,sizeof(h)) || xx_rt_memcmp(h,"NES\x1a",4)) return false;
    nes2=(h[7]&0x0c)==8;
    if((h[7]&0x0c)!=0 && !nes2) return false;
    if(!nes2) { size_t i; for(i=12;i<16;++i) if(h[i]) return false; }
    /* NES 2.0 linear ROM sizes. Exponent/multiplier sizes are refused. */
    if(nes2 && ((h[9]&15)==15 || (h[9]>>4)==15)) return false;
    prg=(h[4]+(nes2 ? (h[9]&15)*256 : 0))*(int64_t)16384;
    chr=(h[5]+(nes2 ? (h[9]>>4)*256 : 0))*(int64_t)8192;
    if(prg==0) return false;
    if(h[6]&4) { if(!pm_add(f,s,"trainer.bin",at,512)) return false; at+=512; }
    if(!pm_add(f,s,"prg.bin",at,prg)) { return false; } at+=prg;
    if(chr) { if(!pm_add(f,s,"chr.bin",at,chr)) return false; at+=chr; }
    /* Unclassified trailing data (including NES2 miscellaneous ROMs) is overlay. */
    if(nes2 && (h[14]&3)) return false;
    s->size=at; return true;

}
void xx_nes_rom_init(xx_nes_rom *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NES_ROM,"nes"); } }
xx_nes_rom *xx_nes_rom_create(xx_io_device *d,int64_t b) { xx_nes_rom *r=(xx_nes_rom *)xx_mem_alloc(sizeof(*r)); if(r) xx_nes_rom_init(r,d,b); return r; }
void xx_nes_rom_destroy(xx_nes_rom *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nes_rom_free(xx_nes_rom *r) { if(r) { xx_nes_rom_destroy(r); xx_mem_free(r); } }
bool xx_nes_rom_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nes_rom_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
