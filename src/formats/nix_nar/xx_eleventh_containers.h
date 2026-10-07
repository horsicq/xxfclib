/* SPDX-License-Identifier: MIT. Private eleventh container primitives. */
#ifndef XX_ELEVENTH_CONTAINERS_H
#define XX_ELEVENTH_CONTAINERS_H
#include "../xx_ninth_data.h"
static XXFC_MAYBE_UNUSED bool ec_utf(nh_blob *b,uint64_t at,uint64_t n) {return nh_span(b,at,n) && n<=65536 && fourth_utf8(b->p+(size_t)at,(size_t)n,b->pd);}
static XXFC_MAYBE_UNUSED bool ec_pad(nh_blob *b,uint64_t *at,uint64_t align) {uint64_t n=(align-(*at&(align-1)))&(align-1);if(!nh_zero(b,*at,n)) return false;*at+=n;return true;}
static bool ec_var(nh_blob *b,uint64_t *at,uint64_t end,uint64_t *value) {unsigned shift=0;uint64_t v=0;uint8_t c;do {if(*at>=end || !nh_span(b,*at,1) || shift>=70) return false;c=b->p[(size_t)(*at)++];if(shift==63 && (c&254)) return false;v|=(uint64_t)(c&127)<<shift;shift+=7;}while(c&128);*value=v;return true;}
static XXFC_MAYBE_UNUSED bool ec_zig(nh_blob *b,uint64_t *at,uint64_t end,int64_t *v) {uint64_t u;if(!ec_var(b,at,end,&u)) return false;*v=(int64_t)(u>>1)^-(int64_t)(u&1);return true;}
static XXFC_MAYBE_UNUSED bool ec_crc(nh_blob *b,uint64_t at,uint64_t n,uint32_t want,bool c32c) {uint32_t c=0;uint64_t i=0;if(!nh_span(b,at,n)) return false;while(i<n) {size_t part=n-i>65536 ? 65536:(size_t)(n-i);if(fd_stop(b->pd)) return false;c=c32c ? xx_crc32c_calc(c,b->p+(size_t)(at+i),part):xx_crc32_calc(c,b->p+(size_t)(at+i),part);i+=part;}return c==want;}
static XXFC_MAYBE_UNUSED bool ec_leaf(const uint8_t *p,uint64_t n) {uint64_t i;if(!n || n>255 || (n==1 && p[0]=='.') || (n==2 && p[0]=='.' && p[1]=='.')) return false;for(i=0;i<n;++i) if(!p[i] || p[i]=='/' || p[i]=='\\') return false;return true;}
static XXFC_MAYBE_UNUSED int ec_cmp(const uint8_t *a,uint64_t an,const uint8_t *b,uint64_t bn) {uint64_t i,n=an<bn ? an:bn;for(i=0;i<n;++i) if(a[i]!=b[i]) return a[i]<b[i] ? -1:1;return an==bn ? 0:(an<bn ? -1:1);}
static XXFC_MAYBE_UNUSED bool ec_number(const uint8_t *p,uint64_t n,bool plus) {uint64_t at=0,start;if(!n || n>1024) return false;if(p[at]=='-' || (plus && p[at]=='+')) ++at;if(at>=n) return false;start=at;if(p[at]=='0') ++at;else {while(at<n && p[at]>='0' && p[at]<='9') ++at;if(at==start) return false;}if(at<n && p[at]=='.') {start=++at;while(at<n && p[at]>='0' && p[at]<='9') ++at;if(at==start) return false;}if(at<n && (p[at]=='e' || p[at]=='E')) {++at;if(at<n && (p[at]=='+' || p[at]=='-')) ++at;start=at;while(at<n && p[at]>='0' && p[at]<='9') ++at;if(at==start) return false;}return at==n;}
#endif
