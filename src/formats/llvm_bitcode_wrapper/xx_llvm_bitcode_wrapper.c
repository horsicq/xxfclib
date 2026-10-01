/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://llvm.org/docs/BitCodeFormat.html
 * Version 0 wrapper; exports its declared raw BC stream. No LLVM IR semantic decoding.
 */
#include "xxfclib/formats/llvm_bitcode_wrapper/xx_llvm_bitcode_wrapper.h"
#include "../xx_payload_members.h"


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[20],bc[4]; uint32_t off,size; (void)pd;
    if(!pm_read(f,0,h,20) || pm_le32(h)!=0x0b17c0de || pm_le32(h+4)!=0) return false;
    off=pm_le32(h+8); size=pm_le32(h+12);
    if(off<20 || size<4 || off>pm_available(f) || size>(uint64_t)(pm_available(f)-off) || !pm_read(f,off,bc,4) || xx_rt_memcmp(bc,"BC\xc0\xde",4)) return false;
    if(!pm_add(f,s,"module.bc",off,size)) return false;
    s->size=(int64_t)off+size; return true;
}

void xx_llvm_bitcode_wrapper_init(xx_llvm_bitcode_wrapper *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_LLVM_BITCODE_WRAPPER,"llvm_bitcode_wrapper"); } }
xx_llvm_bitcode_wrapper *xx_llvm_bitcode_wrapper_create(xx_io_device *d,int64_t b) { xx_llvm_bitcode_wrapper *r=(xx_llvm_bitcode_wrapper *)xx_mem_alloc(sizeof(*r)); if(r) xx_llvm_bitcode_wrapper_init(r,d,b); return r; }
void xx_llvm_bitcode_wrapper_destroy(xx_llvm_bitcode_wrapper *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_llvm_bitcode_wrapper_free(xx_llvm_bitcode_wrapper *r) { if(r) { xx_llvm_bitcode_wrapper_destroy(r); xx_mem_free(r); } }
bool xx_llvm_bitcode_wrapper_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_llvm_bitcode_wrapper_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
