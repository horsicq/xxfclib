/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/google/leveldb/blob/main/doc/table_format.md */
#include "xxfclib/formats/leveldb_sstable/xx_leveldb_sstable.h"
#include "../microsoft_msf/xx_tenth_containers.h"

typedef struct tb_handle {uint64_t at,n;} tb_handle;
static bool tb_block(Abstractformat *f,pm_stream *s,nh_blob *b,tb_handle h,unsigned mode,tb_handle *handles,unsigned *count) {
    uint64_t end=h.at+h.n,restart,at=h.at,keylen=0,j,nr,r=0,shared,added,value,start,entry;uint8_t *key=NULL,*owned=NULL;bool ok=false;
    if(h.n<8 || !nh_span(b,h.at,h.n+5) || b->p[(size_t)end] || !th_crc(b,h.at,h.n+1,pm_le32(b->p+(size_t)end+1),true)) return false;
    nr=pm_le32(b->p+(size_t)end-4);if(!nr || nr>4096 || nr*4>h.n-4) return false;restart=end-4-nr*4;if(pm_le32(b->p+(size_t)restart)) return false;
    for(j=0;j<nr;++j) {uint32_t v=pm_le32(b->p+(size_t)(restart+j*4));if(v>restart-h.at || (j && v<=pm_le32(b->p+(size_t)(restart+(j-1)*4)))) return false;}
    key=(uint8_t *)xx_mem_alloc(65536);if(!key) return false;
    while(at<restart) {entry=at-h.at;if(!th_var(b,&at,restart,&shared) || !th_var(b,&at,restart,&added) || !th_var(b,&at,restart,&value) || shared>keylen || added>65536-shared || !eh_span(at,added,restart)) goto done;
        if(r<nr && pm_le32(b->p+(size_t)(restart+r*4))<=entry) {if(pm_le32(b->p+(size_t)(restart+r*4))!=entry || shared) goto done;++r;}
        xx_rt_memcpy(key+(size_t)shared,b->p+(size_t)at,(size_t)added);keylen=shared+added;at+=added;start=at;if(!eh_span(at,value,restart)) goto done;at+=value;
        if(mode==2) goto done;
        if(mode==1) {uint64_t p=start;if(*count>=1024 || !th_var(b,&p,at,&handles[*count].at) || !th_var(b,&p,at,&handles[*count].n) || p!=at) goto done;++*count;}
        else {owned=(uint8_t *)xx_mem_alloc((size_t)(keylen ? keylen:1));if(!owned) goto done;xx_rt_memcpy(owned,key,(size_t)keylen);if(!th_mem(f,s,"key",&owned,keylen) || !nh_add(f,s,b,"value",start,value)) goto done;}
    }
    if(at!=restart || (at==h.at ? nr!=1 || pm_le32(b->p+(size_t)restart)!=0:r!=nr)) { goto done; } ok=true;
done:if(key) xx_mem_free(key);if(owned) xx_mem_free(owned);return ok;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b;tb_handle meta,index,handles[1024];uint64_t at,footer,end,i;uint8_t magic[8];unsigned count=0;bool ok=false;
    if(pm_available(f)<48 || !pm_read(f,pm_available(f)-8,magic,8) || fd_le64(magic)!=UINT64_C(0xdb4775248b80fb57) || !nh_load(f,&b,pd)) return false;
    footer=b.n-48;at=footer;NH_NEED(th_var(&b,&at,footer+40,&meta.at) && th_var(&b,&at,footer+40,&meta.n) && th_var(&b,&at,footer+40,&index.at) && th_var(&b,&at,footer+40,&index.n) && nh_zero(&b,at,footer+40-at));
    NH_NEED(meta.at<=footer && meta.n<=footer-meta.at && index.at<=footer && index.n<=footer-index.at && meta.at+meta.n+5==index.at && index.at+index.n+5==footer);
    NH_NEED(tb_block(f,s,&b,meta,2,handles,&count) && tb_block(f,s,&b,index,1,handles,&count) && count);end=0;
    for(i=0;i<count;++i) {NH_NEED(handles[i].at==end && handles[i].n<=meta.at-end && handles[i].n+5<=meta.at-end && tb_block(f,s,&b,handles[i],0,handles,&count));end+=handles[i].n+5;}NH_NEED(end==meta.at);
    NH_NEED(nh_add(f,s,&b,"metaindex",meta.at,meta.n+5) && nh_add(f,s,&b,"index",index.at,index.n+5) && nh_add(f,s,&b,"sstable-footer",footer,48));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_leveldb_sstable_init(xx_leveldb_sstable *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_LEVELDB_SSTABLE,"sst"); } }
xx_leveldb_sstable *xx_leveldb_sstable_create(xx_io_device *d,int64_t b) { xx_leveldb_sstable *r=(xx_leveldb_sstable *)xx_mem_alloc(sizeof(*r)); if(r) xx_leveldb_sstable_init(r,d,b); return r; }
void xx_leveldb_sstable_destroy(xx_leveldb_sstable *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_leveldb_sstable_free(xx_leveldb_sstable *r) { if(r) { xx_leveldb_sstable_destroy(r); xx_mem_free(r); } }
bool xx_leveldb_sstable_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_leveldb_sstable_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
