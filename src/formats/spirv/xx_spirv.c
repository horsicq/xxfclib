/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html
 * SPIR-V 1.0-1.6 framing and required preamble, encoded instruction exports.
 * This is not a semantic shader validator or an execution engine.
 */
#include "xxfclib/formats/spirv/xx_spirv.h"
#include "../xx_payload_members.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[20],e[12]; bool be,model=false,capability=false; uint32_t version,bound; int64_t at=20,left=pm_available(f);
    if(!pm_read(f,0,h,20)) return false;
    be=pm_be32(h)==0x07230203U; if(!be && pm_le32(h)!=0x07230203U) return false;
    version=be?pm_be32(h+4):pm_le32(h+4); bound=be?pm_be32(h+12):pm_le32(h+12);
    if((version&0xffff00ffU)!=0x10000U || ((version>>8)&255)>6 || !bound || bound>0x3fffffU || pm_le32(h+16) || left%4) return false;
    while(at<left) {
        uint32_t word,words,opcode; char name[48];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at,e,4)) return false;
        word=be?pm_be32(e):pm_le32(e); words=word>>16; opcode=word&65535;
        if(!words || (uint64_t)words*4>(uint64_t)(left-at)) return false;
        if(!model && opcode!=17 && opcode!=10 && opcode!=11 && opcode!=14) return false;
        if(opcode==17) { if(words!=2 || model) return false; capability=true; }
        if(opcode==14) {
            uint32_t addressing,memory;
            if(model || !capability || words!=3 || !pm_read(f,at,e,12)) return false;
            addressing=be?pm_be32(e+4):pm_le32(e+4); memory=be?pm_be32(e+8):pm_le32(e+8);
            if(addressing>2 || memory>3) { return false; } model=true;
        }
        xx_rt_snprintf(name,sizeof(name),"instruction-%u.spvwords",(unsigned)opcode);
        if(!pm_add(f,s,name,at,(int64_t)words*4)) return false;
        at+=(int64_t)words*4;
    }
    s->size=at; return model && capability;
}

void xx_spirv_init(xx_spirv *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SPIRV,"spv"); } }
xx_spirv *xx_spirv_create(xx_io_device *d,int64_t b) { xx_spirv *r=(xx_spirv *)xx_mem_alloc(sizeof(*r)); if(r) xx_spirv_init(r,d,b); return r; }
void xx_spirv_destroy(xx_spirv *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_spirv_free(xx_spirv *r) { if(r) { xx_spirv_destroy(r); xx_mem_free(r); } }
bool xx_spirv_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_spirv_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
