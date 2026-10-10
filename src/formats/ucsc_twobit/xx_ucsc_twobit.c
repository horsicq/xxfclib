/* SPDX-License-Identifier: MIT
 * Independently implemented from https://genome.ucsc.edu/FAQ/FAQformat.html#format7 */
#include "xxfclib/formats/ucsc_twobit/xx_ucsc_twobit.h"
#include "../common/xx_memory_blob.h"

static bool runs(memory_blob *b,uint64_t *at,uint64_t end,uint32_t dna,bool be) {
    uint32_t count,i;uint64_t starts,sizes,last=0;if(!record_span(*at,4,end)) return false;count=xx_data_get_u32(b->p+(size_t)*at, 4, 0, be);*at+=4;
    if(count>65536 || !record_span(*at,(uint64_t)count*8,end)) { return false; } starts=*at;sizes=starts+(uint64_t)count*4;
    for(i=0;i<count;++i) {uint32_t start=xx_data_get_u32(b->p+(size_t)starts+i*4, 4, 0, be),n=xx_data_get_u32(b->p+(size_t)sizes+i*4, 4, 0, be);if(!n || start<last || start>dna || n>dna-start) return false;last=(uint64_t)start+n;}
    *at+=(uint64_t)count*8;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[16];memory_blob b={0};bool be,ok=false;uint64_t at=16,next;uint32_t count,i,*offsets=NULL;char (*names)[256]=NULL;
    if(!pm_read(f,0,h,16)) { return false; } if(xx_data_get_u32(h, 4, 0, false)==0x1a412743U) be=false;else if(xx_data_get_u32(h, 4, 0, true)==0x1a412743U) be=true;else return false;
    count=xx_data_get_u32(h+8, 4, 0, be);BLOB_NEED(!xx_data_get_u32(h+4, 4, 0, be) && !xx_data_get_u32(h+12, 4, 0, be) && count && count<=1024 && blob_load(f,&b,pd));
    /* Allocate only the validated index size; detection has a small stack. */
    offsets=(uint32_t *)xx_mem_alloc((size_t)count*sizeof(*offsets));
    names=(char (*)[256])xx_mem_alloc((size_t)count*sizeof(*names));
    BLOB_NEED(offsets && names);
    for(i=0;i<count;++i) {uint8_t n;unsigned j;BLOB_NEED(blob_span(&b,at,1));n=b.p[(size_t)at++];BLOB_NEED(n && blob_span(&b,at,(uint64_t)n+4) && blob_ascii(b.p+(size_t)at,n,false));xx_rt_memcpy(names[i],b.p+(size_t)at,n);names[i][n]=0;at+=n;offsets[i]=xx_data_get_u32(b.p+(size_t)at, 4, 0, be);at+=4;for(j=0;j<i;++j) BLOB_NEED(xx_rt_strcmp(names[i],names[j]));}
    BLOB_NEED(blob_add(f,s,&b,"header-index",0,at));next=at;
    for(i=0;i<count;++i) {uint64_t start=next,end=i+1<count ? offsets[i+1]:b.n,bytes;uint32_t dna;BLOB_NEED(offsets[i]==next && end>next && end<=b.n && blob_span(&b,next,4));dna=xx_data_get_u32(b.p+(size_t)next, 4, 0, be);next+=4;BLOB_NEED(dna && runs(&b,&next,end,dna,be) && runs(&b,&next,end,dna,be) && record_span(next,4,end) && !xx_data_get_u32(b.p+(size_t)next, 4, 0, be));next+=4;bytes=((uint64_t)dna+3)/4;
        BLOB_NEED(next+bytes==end && blob_add(f,s,&b,names[i],start,end-start));next=end;
    }s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(names);xx_mem_free(offsets);xx_mem_free(b.p);return ok;
}

void xx_ucsc_twobit_init(xx_ucsc_twobit *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_UCSC_TWOBIT,"ucsc_twobit"); } }
xx_ucsc_twobit *xx_ucsc_twobit_create(xx_io_device *d,int64_t b) { xx_ucsc_twobit *r=(xx_ucsc_twobit *)xx_mem_alloc(sizeof(*r)); if(r) xx_ucsc_twobit_init(r,d,b); return r; }
void xx_ucsc_twobit_destroy(xx_ucsc_twobit *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_ucsc_twobit_free(xx_ucsc_twobit *r) { if(r) { xx_ucsc_twobit_destroy(r); xx_mem_free(r); } }
bool xx_ucsc_twobit_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_ucsc_twobit_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
