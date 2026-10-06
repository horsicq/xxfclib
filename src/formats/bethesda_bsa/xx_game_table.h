/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Small internal helpers for independently implemented bounded game readers.
 * Original names are validated, then replaced by numbered safe output names.
 */
#ifndef XX_GAME_TABLE_H
#define XX_GAME_TABLE_H
#include "../xx_payload_members.h"
static XXFC_MAYBE_UNUSED uint64_t gm_le64(const uint8_t *p) { return (uint64_t)pm_le32(p) | (uint64_t)pm_le32(p+4)<<32; }
static XXFC_MAYBE_UNUSED uint64_t gm_be64(const uint8_t *p) { return (uint64_t)pm_be32(p)<<32 | pm_be32(p+4); }
static bool gm_range(int64_t total,uint64_t at,uint64_t n) { return total>=0 && at<=(uint64_t)total && n<=(uint64_t)total-at; }
static bool gm_read(Abstractformat *f,int64_t total,uint64_t at,void *p,size_t n) { return gm_range(total,at,n) && pm_read(f,(int64_t)at,p,n); }
static XXFC_MAYBE_UNUSED bool gm_string(Abstractformat *f,int64_t total,uint64_t at,uint64_t limit,uint64_t *used) {
    size_t capacity=xx_get_file_buffer_size(); uint8_t *buffer=NULL; bool buffer_result=false; uint64_t i=0;
    if(limit>4096) limit=4096;
    if(!gm_range(total,at,limit)) { buffer_result = (false); goto buffer_done; }
    while(i<limit) {if(!buffer) { if((uint64_t)(limit-i)<capacity) capacity=(size_t)(limit-i); buffer=(uint8_t *)xx_mem_alloc(capacity); if(!buffer) { buffer_result=false; goto buffer_done; } } 
        size_t j,n=(size_t)(limit-i); if(n>capacity) n=capacity;
        if(!pm_read(f,(int64_t)(at+i),buffer,n)) { buffer_result = (false); goto buffer_done; }
        for(j=0;j<n;++j) if(!buffer[j]) { *used=i+j+1; { buffer_result = (true); goto buffer_done; } }
        i+=n;
    }
    { buffer_result = (false); goto buffer_done; }

buffer_done:
    xx_mem_free(buffer);
    return buffer_result;
}
static bool gm_add(Abstractformat *f,pm_stream *s,const char *label,uint64_t at,uint64_t n,uint64_t floor,int64_t total) {
    if(at<floor || !gm_range(total,at,n) || !pm_add(f,s,label,(int64_t)at,(int64_t)n)) return false;
    if(at+n>(uint64_t)s->size) { s->size=(int64_t)(at+n); } return true;
}
static bool gm_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
#endif
