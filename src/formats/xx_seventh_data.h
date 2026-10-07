/* SPDX-License-Identifier: MIT. Checked primitives for additional containers. */
#ifndef XX_SEVENTH_DATA_H
#define XX_SEVENTH_DATA_H
#include "xx_sixth_data.h"
static bool sv_finite64(uint64_t b) {return ((b>>52)&2047U)!=2047U;}
static XXFC_MAYBE_UNUSED bool sv_positive64(uint64_t b) {return !(b>>63) && (b&UINT64_C(0x7fffffffffffffff)) && sv_finite64(b);}
static XXFC_MAYBE_UNUSED uint64_t sv_ordered64(uint64_t x) {if(!(x&UINT64_C(0x7fffffffffffffff))) x=0;return x>>63 ? ~x : x^UINT64_C(0x8000000000000000);}
static XXFC_MAYBE_UNUSED unsigned sv_tokens(char *p,char **v,unsigned cap) {
    unsigned n=0;
    while(*p) {while(*p==' ' || *p=='\t') ++p;if(!*p) break;if(n==cap) return cap+1;v[n++]=p;
        while(*p && *p!=' ' && *p!='\t') { ++p; } if(*p) *p++=0;
    }return n;
}
#endif
