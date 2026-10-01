/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.xfree86.org/current/xpm.pdf
 * Stored encoded component extraction; no image rendering or execution.
 */
#include "xxfclib/formats/xpm/xx_xpm.h"
#include "../xx_payload_members.h"

typedef struct xp_text { const uint8_t *p; size_t size,pos; xx_pd_struct *pd; } xp_text;
static bool xp_ws(unsigned b) { return b==32 || (b>=9 && b<=13); }
static bool xp_space(xp_text *r) {
    for(;;) {
        if(r->pd && xx_pd_is_stopped(r->pd)) return false;
        while(r->pos<r->size && xp_ws(r->p[r->pos])) ++r->pos;
        if(r->size-r->pos>=2 && r->p[r->pos]=='/' && r->p[r->pos+1]=='*') {
            r->pos+=2;
            while(r->size-r->pos>=2 && !(r->p[r->pos]=='*' && r->p[r->pos+1]=='/')) { if((r->pos&4095)==0 && r->pd && xx_pd_is_stopped(r->pd)) return false; ++r->pos; }
            if(r->size-r->pos<2) return false; r->pos+=2; continue;
        }
        return true;
    }
}
static bool xp_char(xp_text *r,uint8_t c) { return xp_space(r) && r->pos<r->size && r->p[r->pos++]==c; }
static bool xp_word(xp_text *r,const char *word) {
    size_t n=xx_rt_strlen(word); if(!xp_space(r) || n>r->size-r->pos || xx_rt_memcmp(r->p+r->pos,word,n)) return false;
    r->pos+=n; return r->pos==r->size || !(r->p[r->pos]=='_' || (r->p[r->pos]>='a' && r->p[r->pos]<='z') || (r->p[r->pos]>='A' && r->p[r->pos]<='Z') || (r->p[r->pos]>='0' && r->p[r->pos]<='9'));
}
static bool xp_string(xp_text *r,size_t *at,size_t *length) {
    if(!xp_char(r,'"')) return false; *at=r->pos;
    while(r->pos<r->size && r->p[r->pos]!='"') { unsigned b=r->p[r->pos]; if(b<32 || b>126 || b=='\\') return false; ++r->pos; }
    if(r->pos==r->size) return false; *length=r->pos-*at; ++r->pos; return true;
}
static bool xp_value(const uint8_t *p,size_t n,size_t *at,unsigned *value) {
    unsigned v=0,count=0; while(*at<n && (p[*at]==' ' || p[*at]=='\t')) ++*at;
    while(*at<n && p[*at]>='0' && p[*at]<='9') { if(v>6553 || (v==6553 && p[*at]>'5')) return false; v=v*10U+(p[(*at)++]-'0'); ++count; }
    if(!count || (*at<n && p[*at]!=' ' && p[*at]!='\t')) return false; *value=v; return true;
}
static bool xp_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd,const uint8_t *data,size_t n,uint8_t *keys) {
    xp_text r; size_t at,len,q,k; unsigned width,height,colors,cpp,i,hotx,hoty; char label[48];
    r.p=data; r.size=n; r.pos=9; r.pd=pd;
    if(!xp_word(&r,"static")) return false;
    if(!xp_space(&r)) return false; if(r.size-r.pos>=5 && !xx_rt_memcmp(r.p+r.pos,"const",5) && !xp_word(&r,"const")) return false;
    if(!xp_word(&r,"char") || !xp_char(&r,'*') || !xp_space(&r)) return false;
    at=r.pos; if(at==n || !((data[at]>='A' && data[at]<='Z') || (data[at]>='a' && data[at]<='z') || data[at]=='_')) return false;
    while(r.pos<n && ((data[r.pos]>='A' && data[r.pos]<='Z') || (data[r.pos]>='a' && data[r.pos]<='z') || (data[r.pos]>='0' && data[r.pos]<='9') || data[r.pos]=='_')) { if(r.pos-at>127) return false; ++r.pos; }
    if(!xp_char(&r,'[') || !xp_char(&r,']') || !xp_char(&r,'=') || !xp_char(&r,'{') || !xp_string(&r,&at,&len)) return false;
    q=0; if(!xp_value(data+at,len,&q,&width) || !xp_value(data+at,len,&q,&height) || !xp_value(data+at,len,&q,&colors) || !xp_value(data+at,len,&q,&cpp)) return false;
    while(q<len && (data[at+q]==' ' || data[at+q]=='\t')) ++q;
    if(q<len) { if(!xp_value(data+at,len,&q,&hotx) || !xp_value(data+at,len,&q,&hoty) || hotx>=width || hoty>=height) return false; while(q<len && (data[at+q]==' ' || data[at+q]=='\t')) ++q; }
    if(q!=len || !width || width>4096 || !height || height>4096 || !colors || colors>4096 || (cpp!=1 && cpp!=2) || (uint64_t)width*height>16777216 || !pm_add(f,s,"values.txt",(int64_t)at,(int64_t)len)) return false;
    for(i=0;i<colors;++i) {
        unsigned key;
        if(!xp_char(&r,',') || !xp_string(&r,&at,&len) || len<cpp+4U) return false;
        key=data[at]; if(cpp==2) key=key*256U+data[at+1]; if(keys[key]) return false; keys[key]=1;
        q=cpp; if(data[at+q]!=' ' && data[at+q]!='\t') return false; while(q<len && (data[at+q]==' ' || data[at+q]=='\t')) ++q;
        if(q+2>=len || data[at+q++]!='c' || (data[at+q]!=' ' && data[at+q]!='\t')) return false;
        while(q<len && (data[at+q]==' ' || data[at+q]=='\t')) ++q; if(q==len) return false;
        if(data[at+q]=='#') { size_t digits=len-q-1; if(digits!=3 && digits!=6 && digits!=9 && digits!=12) return false;
            for(k=q+1;k<len;++k) if(!((data[at+k]>='0' && data[at+k]<='9') || (data[at+k]>='A' && data[at+k]<='F') || (data[at+k]>='a' && data[at+k]<='f'))) return false;
        }
        xx_rt_snprintf(label,sizeof(label),"color-%u.txt",i); if(!pm_add(f,s,label,(int64_t)at,(int64_t)len)) return false;
    }
    for(i=0;i<height;++i) {
        if((pd && xx_pd_is_stopped(pd)) || !xp_char(&r,',') || !xp_string(&r,&at,&len) || len!=(size_t)width*cpp) return false;
        for(q=0;q<len;q+=cpp) { unsigned key=data[at+q]; if(cpp==2) key=key*256U+data[at+q+1]; if(!keys[key]) return false; }
        xx_rt_snprintf(label,sizeof(label),"row-%u.txt",i); if(!pm_add(f,s,label,(int64_t)at,(int64_t)len)) return false;
    }
    if(!xp_space(&r)) return false; if(r.pos<n && data[r.pos]==',') ++r.pos;
    if(!xp_char(&r,'}') || !xp_char(&r,';')) return false; s->size=(int64_t)r.pos; return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    int64_t available=pm_available(f); size_t n; uint8_t *data,*keys; bool result=false;
    if(available<9) return false; n=available>8388608 ? 8388608U : (size_t)available;
    data=(uint8_t *)xx_mem_alloc(n); keys=(uint8_t *)xx_mem_alloc(65536);
    if(data && keys && pm_read(f,0,data,n) && !xx_rt_memcmp(data,"/* XPM */",9)) { xx_mem_zero(keys,65536); result=xp_parse(f,s,pd,data,n,keys); }
    if(data) xx_mem_free(data); if(keys) xx_mem_free(keys); return result;
}

void xx_xpm_init(xx_xpm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_XPM,"xpm"); } }
xx_xpm *xx_xpm_create(xx_io_device *d,int64_t b) { xx_xpm *r=(xx_xpm *)xx_mem_alloc(sizeof(*r)); if(r) xx_xpm_init(r,d,b); return r; }
void xx_xpm_destroy(xx_xpm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_xpm_free(xx_xpm *r) { if(r) { xx_xpm_destroy(r); xx_mem_free(r); } }
bool xx_xpm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_xpm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
