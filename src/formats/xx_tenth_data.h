/* SPDX-License-Identifier: MIT. Private bounded tenth-batch wire primitives. */
#ifndef XX_TENTH_DATA_H
#define XX_TENTH_DATA_H
#include "xx_ninth_data.h"
typedef struct tn_text {nh_blob *b;uint64_t at,label_left;unsigned nodes,leaves,taxa;uint64_t *tax_at,*tax_len;uint8_t *tax_used;} tn_text;
static bool tn_space(uint8_t c) {return c==' ' || c=='\t' || c=='\r' || c=='\n';}
static bool tn_skip(tn_text *t) {
    nh_blob *b=t->b;while(t->at<b->n) {uint8_t c=b->p[(size_t)t->at];if(tn_space(c)) {++t->at;continue;}
        if(c=='[') {unsigned depth=1;++t->at;while(t->at<b->n && depth) {c=b->p[(size_t)t->at++];if(c=='[' && ++depth>64) return false;if(c==']') --depth;if(!c || fd_stop(b->pd)) return false;}if(depth) return false;continue;}
        break;
    }return !fd_stop(b->pd);
}
static bool tn_word(tn_text *t,const char *word) {
    uint64_t at;size_t i,n=xx_rt_strlen(word);if(!tn_skip(t)) return false;at=t->at;if(!nh_span(t->b,at,n)) return false;
    for(i=0;i<n;++i) {uint8_t c=t->b->p[(size_t)at+i];if(c>='A' && c<='Z') c=(uint8_t)(c+32);if(c!=(uint8_t)word[i]) return false;}
    if(at+n<t->b->n) {uint8_t c=t->b->p[(size_t)(at+n)];if((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_') return false;}
    t->at+=n;return true;
}
static bool tn_char(tn_text *t,uint8_t c) {if(!tn_skip(t) || t->at>=t->b->n || t->b->p[(size_t)t->at]!=c) return false;++t->at;return true;}
static bool tn_label(tn_text *t,bool required) {
    uint64_t start;nh_blob *b=t->b;if(!tn_skip(t)) return false;start=t->at;
    if(t->at<b->n && b->p[(size_t)t->at]=='\'') {++t->at;while(t->at<b->n) {uint8_t c=b->p[(size_t)t->at++];if(c=='\'') {if(t->at<b->n && b->p[(size_t)t->at]=='\'') {++t->at;continue;}return t->at-start<=4096 && (!required || t->at-start>2);}if(!c || c<32 || t->at-start>4096) return false;}return false;}
    while(t->at<b->n) {uint8_t c=b->p[(size_t)t->at];if(tn_space(c) || c=='(' || c==')' || c==',' || c==':' || c==';' || c=='[' || c==']' || c=='=') break;if(c<32 || c=='\'' || t->at-start>=4096) return false;++t->at;}
    return !required || t->at>start;
}
static bool tn_number(tn_text *t) {
    uint64_t start=t->at;unsigned digits=0,exdigits=0;int exponent=0;nh_blob *b=t->b;
    if(t->at<b->n && (b->p[(size_t)t->at]=='+' || b->p[(size_t)t->at]=='-')) ++t->at;
    while(t->at<b->n && b->p[(size_t)t->at]>='0' && b->p[(size_t)t->at]<='9') {++t->at;++digits;}
    if(t->at<b->n && b->p[(size_t)t->at]=='.') {++t->at;while(t->at<b->n && b->p[(size_t)t->at]>='0' && b->p[(size_t)t->at]<='9') {++t->at;++digits;}}
    if(!digits || digits>64) return false;
    if(t->at<b->n && (b->p[(size_t)t->at]=='e' || b->p[(size_t)t->at]=='E')) {++t->at;if(t->at<b->n && (b->p[(size_t)t->at]=='+' || b->p[(size_t)t->at]=='-')) ++t->at;while(t->at<b->n && b->p[(size_t)t->at]>='0' && b->p[(size_t)t->at]<='9') {exponent=exponent*10+b->p[(size_t)t->at++]-'0';if(++exdigits>3) return false;}if(!exdigits || exponent>240) return false;}
    return t->at-start<=80;
}
/* Compare names after removing quote delimiters and doubled quote escapes. */
static bool tn_label_eq(nh_blob *b,uint64_t a,uint64_t na,uint64_t c,uint64_t nc,uint64_t *budget) {
    uint64_t ae=a+na,ce=c+nc;bool aq,cq;if(!na || !nc || !nh_span(b,a,na) || !nh_span(b,c,nc)) return false;aq=b->p[(size_t)a]=='\'';cq=b->p[(size_t)c]=='\'';if(aq) {if(na<2 || b->p[(size_t)ae-1]!='\'') return false;++a;--ae;}if(cq) {if(nc<2 || b->p[(size_t)ce-1]!='\'') return false;++c;--ce;}
    while(a<ae && c<ce) {uint8_t x,y;if(!*budget) return false;--*budget;x=b->p[(size_t)a++];y=b->p[(size_t)c++];if(aq && x=='\'' && a<ae && b->p[(size_t)a]=='\'') ++a;if(cq && y=='\'' && c<ce && b->p[(size_t)c]=='\'') ++c;if(x!=y) return false;}return a==ae && c==ce;
}
static bool tn_clade(tn_text *t,unsigned depth) {
    nh_blob *b=t->b;unsigned children=0;if(depth>64 || ++t->nodes>4096 || !tn_skip(t) || t->at>=b->n) return false;
    if(b->p[(size_t)t->at]=='(') {++t->at;do {if(!tn_clade(t,depth+1)) return false;++children;if(!tn_skip(t) || t->at>=b->n) return false;if(b->p[(size_t)t->at]!=',') break;++t->at;}while(true);if(children<2 || !tn_char(t,')') || !tn_label(t,false)) return false;}
    else {uint64_t start=t->at;unsigned i;if(!tn_label(t,true)) return false;if(t->taxa) {for(i=0;i<t->taxa;++i) {bool equal=tn_label_eq(b,start,t->at-start,t->tax_at[i],t->tax_len[i],&t->label_left);if(!t->label_left) return false;if(equal) break;}if(i==t->taxa || t->tax_used[i]) return false;t->tax_used[i]=1;}++t->leaves;}
    if(!tn_skip(t)) return false;if(t->at<b->n && b->p[(size_t)t->at]==':') {++t->at;if(!tn_skip(t) || !tn_number(t)) return false;}
    return tn_skip(t);
}
static bool tn_tree(tn_text *t) {t->nodes=0;t->leaves=0;if(t->taxa) xx_mem_zero(t->tax_used,t->taxa);return tn_clade(t,0) && t->leaves>=2 && (!t->taxa || t->leaves==t->taxa) && tn_char(t,';');}
static bool tn_decimal(const uint8_t *p,size_t n,uint64_t *value) {size_t i;uint64_t v=0;if(!n || n>18) return false;for(i=0;i<n;++i) {if(p[i]<'0' || p[i]>'9' || v>(UINT64_MAX-(p[i]-'0'))/10) return false;v=v*10+p[i]-'0';}*value=v;return true;}
static bool tn_line_uint(nh_blob *b,uint64_t end,const char *key,uint64_t *value) {
    uint64_t at=0;size_t n=xx_rt_strlen(key);unsigned seen=0;
    while(at<end) {uint64_t line=at,last;while(at<end && b->p[(size_t)at]!='\n') ++at;last=at;if(last>line && b->p[(size_t)(last-1)]=='\r') --last;
        if(last-line>n && !xx_rt_memcmp(b->p+(size_t)line,key,n) && b->p[(size_t)line+n]==' ') {uint64_t start=line+n+1,stop=start;while(stop<last && b->p[(size_t)stop]!=' ') ++stop;if(++seen>1 || !tn_decimal(b->p+(size_t)start,(size_t)(stop-start),value)) return false;}
        if(at<end) ++at;
    }return seen==1;
}
#endif
