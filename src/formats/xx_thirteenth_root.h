/* SPDX-License-Identifier: MIT. Bounded scientific text and token helpers. */
#ifndef XX_THIRTEENTH_ROOT_H
#define XX_THIRTEENTH_ROOT_H
#include "xx_twelfth_root.h"
static bool th_eq(nh_blob *b,el_token t,const char *s) {
    size_t n=xx_rt_strlen(s);uint64_t i;if(t.n!=n) return false;
    for(i=0;i<t.n;++i) {unsigned a=b->p[(size_t)(t.at+i)],z=(unsigned char)s[i];if(a>='A' && a<='Z') a+=32;if(z>='A' && z<='Z') z+=32;if(a!=z) return false;}return true;
}
static el_token th_comment(nh_blob *b,el_token t,const char *chars) {
    uint64_t i;for(i=0;i<t.n;++i) {size_t j;for(j=0;chars[j];++j) if(b->p[(size_t)(t.at+i)]==(uint8_t)chars[j]) {t.n=i;return el_trim(b,t);}}return el_trim(b,t);
}
static bool th_words(el_lines *c,el_token *line,el_token *t,unsigned cap,unsigned *nt,const char *comment) {
    for(;;) {uint64_t begin=c->at;el_token v;if(!el_line(c,line)) {
            if(begin<c->b->n) {uint64_t i;bool whitespace=c->at==c->b->n;for(i=begin;i<c->b->n && whitespace;++i) if(c->b->p[(size_t)i]!=32 && c->b->p[(size_t)i]!=9 && c->b->p[(size_t)i]!=13) whitespace=false;if(!whitespace) c->at=c->b->n+1;}
            return false;
        }v=th_comment(c->b,*line,comment);if(v.n) {if(el_split(c->b,v,t,cap,nt,false)) return true;c->at=c->b->n+1;return false;}}
}
static XXFC_MAYBE_UNUSED bool th_symbol(nh_blob *b,el_token t) {unsigned i;for(i=1;i<=118;++i) if(th_eq(b,t,tw_elements[i])) return true;return false;}
static XXFC_MAYBE_UNUSED bool th_positive(nh_blob *b,el_token t) {return el_float(b,t) && tw_value(b,t)>0;}
static bool th_floats(nh_blob *b,el_token *t,unsigned n) {return tw_floats(b,t,0,n);}
static XXFC_MAYBE_UNUSED bool th_same(nh_blob *b,el_token a,el_token z) {return a.n==z.n && !xx_rt_memcmp(b->p+(size_t)a.at,b->p+(size_t)z.at,(size_t)a.n);}
static XXFC_MAYBE_UNUSED bool th_ascii(nh_blob *b) {uint64_t at=0;while(at<b->n) {size_t n=(size_t)(b->n-at>65536 ? 65536:b->n-at);if(fd_stop(b->pd) || !nh_ascii(b->p+(size_t)at,n,false)) return false;at+=n;}return true;}
static XXFC_MAYBE_UNUSED bool th_vectors(el_lines *c,unsigned n,unsigned width) {
    el_token line,t[8];unsigned i,nt;if(width>8) return false;for(i=0;i<n;++i) if(!th_words(c,&line,t,8,&nt,"#") || nt!=width || !th_floats(c->b,t,width)) return false;return true;
}
static XXFC_MAYBE_UNUSED bool th_cell(nh_blob *b,el_token t[3][3]) {
    double a[3][3],d;unsigned i,j;for(i=0;i<3;++i) for(j=0;j<3;++j) {if(!el_float(b,t[i][j])) return false;a[i][j]=tw_value(b,t[i][j]);}
    d=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);return d==d && d>1e-18 && d<1e240;
}
/* OpenFOAM tokens: no directives, expansion, code streams or external reads. */
typedef struct th_lexer {nh_blob *b;uint64_t at,work;} th_lexer;
static bool th_charge(th_lexer *c) {if(!c->work) {c->at=c->b->n+1;return false;}--c->work;return true;}
static bool th_token(th_lexer *c,el_token *t) {
    uint64_t start;nh_blob *b=c->b;
    for(;;) {
        if(!c->work || fd_stop(b->pd)) return false;
        while(c->at<b->n && b->p[(size_t)c->at]<=32) {uint8_t ch=b->p[(size_t)c->at++];if(ch!=32 && ch!=9 && ch!=10 && ch!=13) return false;if(!th_charge(c)) return false;}
        if(c->at==b->n) return false;
        if(c->at+1<b->n && b->p[(size_t)c->at]=='/' && b->p[(size_t)c->at+1]=='/') {c->at+=2;while(c->at<b->n && b->p[(size_t)c->at]!='\n') {if(!th_charge(c)) return false;++c->at;}continue;}
        if(c->at+1<b->n && b->p[(size_t)c->at]=='/' && b->p[(size_t)c->at+1]=='*') {bool ended=false;c->at+=2;while(c->at+1<b->n) {if(!th_charge(c)) return false;if(b->p[(size_t)c->at]=='*' && b->p[(size_t)c->at+1]=='/') {c->at+=2;ended=true;break;}++c->at;}if(!ended) {c->at=b->n+1;return false;}continue;}
        break;
    }
    start=c->at;
    if(b->p[(size_t)c->at]=='"') {++c->at;while(c->at<b->n && b->p[(size_t)c->at]!='"') {uint8_t ch=b->p[(size_t)c->at++];if(ch<32 || ch>126 || ch=='\\' || c->at-start>255 || !th_charge(c)) {c->at=b->n+1;return false;}}if(c->at==b->n) {c->at=b->n+1;return false;}++c->at;}
    else if(b->p[(size_t)c->at]=='{' || b->p[(size_t)c->at]=='}' || b->p[(size_t)c->at]=='(' || b->p[(size_t)c->at]==')' || b->p[(size_t)c->at]==';') ++c->at;
    else {while(c->at<b->n) {uint8_t ch=b->p[(size_t)c->at];if(ch<=32 || ch=='{' || ch=='}' || ch=='(' || ch==')' || ch==';' || ch=='/') break;if(ch>126 || ch=='#' || ch=='$' || ch=='"' || ch=='\\' || c->at-start>255 || !th_charge(c)) return false;++c->at;}if(c->at==start) return false;}
    t->at=start;t->n=c->at-start;return true;
}
static XXFC_MAYBE_UNUSED bool th_expect(th_lexer *c,const char *s) {el_token t;return th_token(c,&t) && el_eq(c->b,t,s);}
static XXFC_MAYBE_UNUSED bool th_finish(th_lexer *c) {el_token t;if(th_token(c,&t)) return false;return c->at==c->b->n && c->work && !fd_stop(c->b->pd);}
#endif
