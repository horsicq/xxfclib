/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* Private bounded decoded-buffer cursor; no container grammar. */
#ifndef XX_BOUNDED_MEMBER_CURSOR_H
#define XX_BOUNDED_MEMBER_CURSOR_H
#include "xx_bounded_deflate_members.h"
typedef struct bdm_cursor { const uint8_t *data; size_t size,at; } bdm_cursor;
static XXFC_MAYBE_UNUSED bool bdm_take(bdm_cursor *c,size_t n,const uint8_t **out) {
    if(n>c->size-c->at) return false;
    if(out) *out=c->data+c->at;
    c->at+=n; return true;
}
static XXFC_MAYBE_UNUSED bool bdm_word(bdm_cursor *c,uint32_t *out) {
    const uint8_t *p;
    if(!bdm_take(c,4,&p)) return false;
    *out=bdm_u32(p); return true;
}
static XXFC_MAYBE_UNUSED bool bdm_string(bdm_cursor *c,char *name,size_t capacity) {
    const uint8_t *p; uint32_t n;
    if(!bdm_word(c,&n) || n>4096U || !bdm_take(c,n,&p)) return false;
    if(name && capacity) {
        size_t i,copy=n<capacity-1U?n:capacity-1U;
        for(i=0;i<copy;++i) name[i]=(p[i]>=32 && p[i]<127)?(char)p[i]:'_';
        name[copy]=0;
    }
    return true;
}

#endif
