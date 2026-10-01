/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/tracker_ptm/xx_tracker_ptm.h"
#include "../tracker_liquid/xx_eighth_components.h"
#ifndef TRACKER_PTM
#define XX_FILE_TYPE_TRACKER_PTM ((xx_file_type_t)808)
#endif
static bool e8_parse(e8_blob*c) {
 unsigned ins,pat,chn,ord,i;size_t meta,p,last;uint32_t offsets[384],sizes[384];unsigned count=0;
 if(!e8_range(c,0,608) || !e8_eq(c,44,"PTMF",4) || c->b[28]!=26 || c->b[30]!=2 || pm_le16(c->b+40) || !(ord=pm_le16(c->b+32)) || ord>256 || (ins=pm_le16(c->b+34))>255 || !(pat=pm_le16(c->b+36)) || pat>128 || !(chn=pm_le16(c->b+38)) || chn>32)return false;
 meta=608U+80U*ins;if(!e8_range(c,0,meta) || !e8_add(c,"headers.bin",0,meta))return false;for(i=0;i<ord;++i)if(c->b[96+i]>=pat && c->b[96+i]<254)return false;
 for(i=0;i<pat;++i){unsigned row=0;uint32_t seen=0;p=(size_t)pm_le16(c->b+352+2U*i)*16U;if(p<meta || p>=c->n)return false;offsets[count]=(uint32_t)p;while(row<64){unsigned v,k,need;if(!e8_range(c,p,1))return false;v=c->b[p++];if(!v){++row;seen=0;continue;}k=v&31;if(k>=chn || (seen&(1U<<k)))return false;seen|=1U<<k;need=((v&32) ? 2U:0U)+((v&64) ? 2U:0U)+((v&128) ? 1U:0U);if(!need || !e8_range(c,p,need))return false;p+=need;}sizes[count]=(uint32_t)(p-offsets[count]);if(!e8_add(c,"pattern.bin",offsets[count],sizes[count]))return false;++count;}
 for(i=0;i<ins;++i){size_t h=608U+80U*i;unsigned type=c->b[h];uint32_t at=pm_le32(c->b+h+18),z=pm_le32(c->b+h+22);if((!e8_eq(c,h+76,"PTMS",4) && !e8_zero(c,h+76,4)) || c->b[h+13]>64 || (type&~31U) || (type&3)>1 || pm_le32(c->b+h+26)>pm_le32(c->b+h+30) || pm_le32(c->b+h+30)>z || ((type&16) && (z&1)))return false;if(!z)continue;if((type&3)!=1 || at<meta || !e8_range(c,at,z))return false;offsets[count]=at;sizes[count]=z;if(!e8_add(c,"delta-sample.bin",at,z))return false;++count;}
 for(i=1;i<count;++i){unsigned j=i;uint32_t at=offsets[i],z=sizes[i];while(j && offsets[j-1]>at){offsets[j]=offsets[j-1];sizes[j]=sizes[j-1];--j;}offsets[j]=at;sizes[j]=z;}
 last=meta;for(i=0;i<count;++i){if(offsets[i]<last || offsets[i]-last>15 || !e8_zero(c,last,offsets[i]-last))return false;last=(size_t)offsets[i]+sizes[i];}return last==c->n;
}
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {return e8_loaded(f,s,pd,e8_parse);}
void xx_tracker_ptm_init(xx_tracker_ptm*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_PTM,"bin");}}
xx_tracker_ptm*xx_tracker_ptm_create(xx_io_device*d,int64_t b) {xx_tracker_ptm*r=(xx_tracker_ptm*)xx_mem_alloc(sizeof(*r));if(r)xx_tracker_ptm_init(r,d,b);return r;}
void xx_tracker_ptm_destroy(xx_tracker_ptm*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tracker_ptm_free(xx_tracker_ptm*r) {if(r){xx_tracker_ptm_destroy(r);xx_mem_free(r);}}
bool xx_tracker_ptm_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_tracker_ptm_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
