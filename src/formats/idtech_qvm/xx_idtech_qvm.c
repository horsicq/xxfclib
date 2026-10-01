/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/id-Software/Quake-III-Arena/master/code/qcommon/qfiles.h
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#include "xxfclib/formats/idtech_qvm/xx_idtech_qvm.h"
#include "../bethesda_bsa/xx_game_table.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[32]; uint32_t co,cn,at,dn,ln; uint64_t end; int64_t total=pm_available(f);
    if(!gm_read(f,total,0,h,32) || pm_le32(h)!=0x12721444 || !pm_le32(h+4) || pm_le32(h+4)>16777216 || gm_stopped(pd)) return false;
    co=pm_le32(h+8); cn=pm_le32(h+12); at=pm_le32(h+16); dn=pm_le32(h+20); ln=pm_le32(h+24);
    end=(uint64_t)at+dn+ln; if(co<32 || !cn || pm_le32(h+4)>cn || (dn&3) || at<(uint64_t)co+cn || !gm_range(total,at,(uint64_t)dn+ln)) return false;
    s->size=(int64_t)end; if(!gm_add(f,s,"code.bin",co,cn,32,(int64_t)end)) return false;
    if(dn && !gm_add(f,s,"data.bin",at,dn,32,(int64_t)end)) return false;
    if(ln && !gm_add(f,s,"literals.bin",(uint64_t)at+dn,ln,32,(int64_t)end)) return false;
    return true;
}
void xx_idtech_qvm_init(xx_idtech_qvm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_IDTECH_QVM,"bin"); } }
xx_idtech_qvm *xx_idtech_qvm_create(xx_io_device *d,int64_t b) { xx_idtech_qvm *r=(xx_idtech_qvm *)xx_mem_alloc(sizeof(*r)); if(r) xx_idtech_qvm_init(r,d,b); return r; }
void xx_idtech_qvm_destroy(xx_idtech_qvm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_idtech_qvm_free(xx_idtech_qvm *r) { if(r) { xx_idtech_qvm_destroy(r); xx_mem_free(r); } }
bool xx_idtech_qvm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_idtech_qvm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
