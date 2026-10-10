/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Private bounded resource-reader utilities; each format owns its grammar.
 */
#ifndef XX_GAME_RESOURCE_HELPERS_H
#define XX_GAME_RESOURCE_HELPERS_H
#include "xx_payload_members.h"
#include <string.h>

#define XGR_MAX_NAME 4096U
#define XGR_MAX_RECORDS 100000U
static XXFC_MAYBE_UNUSED uint16_t xgr_le16(const uint8_t *b) { return (uint16_t)((uint16_t)b[0] | (uint16_t)b[1]<<8); }
static XXFC_MAYBE_UNUSED uint32_t xgr_le32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1]<<8 | (uint32_t)b[2]<<16 | (uint32_t)b[3]<<24; }
static XXFC_MAYBE_UNUSED uint16_t xgr_be16(const uint8_t *b) { return (uint16_t)((uint16_t)b[0]<<8 | b[1]); }
static XXFC_MAYBE_UNUSED uint32_t xgr_be32(const uint8_t *b) { return (uint32_t)b[0]<<24 | (uint32_t)b[1]<<16 | (uint32_t)b[2]<<8 | b[3]; }
static XXFC_MAYBE_UNUSED bool xgr_stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static XXFC_MAYBE_UNUSED bool xgr_range(uint64_t at,uint64_t size,uint64_t end) { return at<=end && size<=end-at && at<=INT64_MAX && size<=INT64_MAX; }
static XXFC_MAYBE_UNUSED bool xgr_write(xx_io_device *out,const uint8_t *data,size_t size) {
    size_t done=0;
    if(!out) return true;
    while(done<size) { ssize_t n=xx_io_write(out,data+done,size-done); if(n<=0 || (size_t)n>size-done) return false; done+=(size_t)n; }
    return true;
}
/* Keep original paths, but never allow absolute paths, parent traversal or
 * Windows alternate streams. The payload adapter owns the normalized name. */
static XXFC_MAYBE_UNUSED bool xgr_name(char *name) {
    size_t i,start=0,n=xx_rt_strlen(name);
    /* A relative-root prefix is customary in several resource packers. */
    while(n>=2 && name[0]=='.' && (name[1]=='/' || name[1]=='\\')) { memmove(name,name+2,n-1);n-=2; }
    if(!n || n>XGR_MAX_NAME || name[0]=='/' || name[0]=='\\') return false;
    for(i=0;i<=n;++i) {
        unsigned char c=(unsigned char)name[i];
        if(c=='\\') name[i]='/';
        if(c && (c<32 || c==127 || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*')) return false;
        if(!c || name[i]=='/') {
            size_t len=i-start;
            if(!len || (len==1 && name[start]=='.') || (len==2 && name[start]=='.' && name[start+1]=='.') || name[i-1]=='.' || name[i-1]==' ') return false;
            start=i+1;
        }
    }
    return true;
}
static XXFC_MAYBE_UNUSED bool xgr_add(Abstractformat *f,pm_stream *s,char *name,uint64_t at,uint64_t size) {
    if(s->count>=XGR_MAX_RECORDS || !xgr_name(name) || !pm_add(f,s,name,(int64_t)at,(int64_t)size)) return false;
    s->items[s->count-1].display_name=xx_str_dup(name);
    return s->items[s->count-1].display_name!=NULL;
}
static XXFC_MAYBE_UNUSED bool xgr_zstr(Abstractformat *f,uint64_t *at,uint64_t end,char *name) {
    size_t used=0;
    while(*at<end && used<XGR_MAX_NAME) {
        uint8_t b[256];size_t i,n=end-*at<sizeof(b)?(size_t)(end-*at):sizeof(b);
        if(!pm_read(f,(int64_t)*at,b,n)) return false;
        for(i=0;i<n;++i) { ++*at; if(!b[i]) { name[used]=0;return true; } if(used==XGR_MAX_NAME) return false;name[used++]=(char)b[i]; }
    }
    return false;
}
static XXFC_MAYBE_UNUSED void xgr_free(void *p) { xx_mem_free(p); }
#endif
