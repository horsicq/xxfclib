/* SPDX-License-Identifier: MIT. Checked scientific-format primitives. */
#ifndef XX_TWELFTH_ROOT_H
#define XX_TWELFTH_ROOT_H
#include "xx_eleventh_data.h"
static const char *const tw_elements[]={"X","H","He","Li","Be","B","C","N","O","F","Ne","Na","Mg","Al","Si","P","S","Cl","Ar","K","Ca","Sc","Ti","V","Cr","Mn","Fe","Co","Ni","Cu","Zn","Ga","Ge","As","Se","Br","Kr","Rb","Sr","Y","Zr","Nb","Mo","Tc","Ru","Rh","Pd","Ag","Cd","In","Sn","Sb","Te","I","Xe","Cs","Ba","La","Ce","Pr","Nd","Pm","Sm","Eu","Gd","Tb","Dy","Ho","Er","Tm","Yb","Lu","Hf","Ta","W","Re","Os","Ir","Pt","Au","Hg","Tl","Pb","Bi","Po","At","Rn","Fr","Ra","Ac","Th","Pa","U","Np","Pu","Am","Cm","Bk","Cf","Es","Fm","Md","No","Lr","Rf","Db","Sg","Bh","Hs","Mt","Ds","Rg","Cn","Nh","Fl","Mc","Lv","Ts","Og"};
static XXFC_MAYBE_UNUSED bool tw_element(nh_blob *b,el_token v) {unsigned i;for(i=1;i<119;++i) if(el_eq(b,v,tw_elements[i])) return true;return false;}
static XXFC_MAYBE_UNUSED bool tw_z(nh_blob *b,el_token v) {uint64_t n;return el_uint(b,v,&n) && n>=1 && n<=118;}
static XXFC_MAYBE_UNUSED bool tw_words(el_lines *c,el_token *line,el_token *t,unsigned cap,unsigned *n) {return el_line(c,line) && el_split(c->b,*line,t,cap,n,false);}
static XXFC_MAYBE_UNUSED bool tw_floats(nh_blob *b,el_token *t,unsigned begin,unsigned end) {unsigned i;for(i=begin;i<end;++i) if(!el_float(b,t[i])) return false;return true;}
static XXFC_MAYBE_UNUSED bool tw_numbers(el_lines *c,uint64_t count) {el_token line,t[128];unsigned n,i;uint64_t seen=0;while(seen<count) {
    uint64_t begin=c->at;if(!el_line(c,&line)) {uint64_t j;if(fd_stop(c->b->pd) || c->at!=c->b->n || c->b->n-begin>1048576) return false;for(j=begin;j<c->b->n;++j) {uint8_t ch=c->b->p[(size_t)j];if(ch<32 && ch!='\t') return false;if(ch>126) return false;}line.at=begin;line.n=c->b->n-begin;}
    if(!el_split(c->b,line,t,128,&n,false) || !n || n>count-seen) { return false; } for(i=0;i<n;++i) if(!el_float(c->b,t[i])) return false;seen+=n;}return true;}
static XXFC_MAYBE_UNUSED bool tw_trailing(el_lines *c) {el_token line;while(c->at<c->b->n) if(!el_line(c,&line) || el_trim(c->b,line).n) return false;return true;}
static bool tw_fixed(nh_blob *b,el_token line,unsigned width,unsigned *count,bool real) {uint64_t at=0;unsigned n=0;if(!line.n || line.n%width) return false;while(at<line.n) {el_token v=el_trim(b,el_slice(line,at,width));if(!v.n || (real ? !el_float(b,v):!el_integer(b,v))) return false;at+=width;++n;}*count=n;return true;}
static XXFC_MAYBE_UNUSED bool tw_fixed_array(el_lines *c,uint64_t count,unsigned width,unsigned perline,bool real) {el_token line;uint64_t seen=0;while(seen<count) {unsigned n;if(!el_line(c,&line) || !tw_fixed(c->b,line,width,&n,real) || n!=((count-seen)<perline ? (unsigned)(count-seen):perline)) return false;seen+=n;}return true;}
static XXFC_MAYBE_UNUSED bool tw_name(nh_blob *b,el_token v) {uint64_t i;if(!v.n || v.n>255) return false;for(i=0;i<v.n;++i) {uint8_t ch=b->p[(size_t)(v.at+i)];if(ch<=32 || ch>126) return false;}return true;}
static XXFC_MAYBE_UNUSED double tw_value(nh_blob *b,el_token v) {char text[100];v=el_trim(b,v);xx_rt_memcpy(text,b->p+(size_t)v.at,(size_t)v.n);text[v.n]=0;return xx_rt_strtod(text,NULL);}
#endif
