/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/sqlite/sqlite/blob/master/src/pager.c */
#include "xxfclib/formats/sqlite_rollback_journal/xx_sqlite_rollback_journal.h"
#include "../xx_tenth_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};bool ok=false;uint64_t at=0;unsigned headers=0,total=0;uint32_t first_page=0,first_sector=0,orig=0;
    NH_NEED(nh_load(f,&b,pd));while(at<b.n) {uint32_t count,seed,pages,sector,page,i;uint64_t bytes;
        NH_NEED(++headers<=64 && nh_span(&b,at,28) && !xx_rt_memcmp(b.p+(size_t)at,"\xd9\xd5\x05\xf9\x20\xa1\x63\xd7",8));
        count=xx_data_get_u32(b.p+(size_t)at+8, 4, 0, true);seed=xx_data_get_u32(b.p+(size_t)at+12, 4, 0, true);pages=xx_data_get_u32(b.p+(size_t)at+16, 4, 0, true);sector=xx_data_get_u32(b.p+(size_t)at+20, 4, 0, true);page=xx_data_get_u32(b.p+(size_t)at+24, 4, 0, true);
        NH_NEED(pages && pages<=0x7fffffff && sector>=32 && sector<=65536 && !(sector&(sector-1)) && page>=512 && page<=65536 && !(page&(page-1)));
        if(headers==1) {first_page=page;first_sector=sector;orig=pages;}else NH_NEED(page==first_page && sector==first_sector && pages==orig);
        NH_NEED(nh_span(&b,at,sector) && nh_zero(&b,at+28,sector-28) && nh_add(f,s,&b,"journal-header",at,sector));at+=sector;
        if(count==UINT32_MAX) {NH_NEED((b.n-at)%(page+8)==0);count=(uint32_t)((b.n-at)/(page+8));}
        NH_NEED(count && count<=4090 && total<=4090-count && fd_mul(count,page+8,&bytes) && nh_span(&b,at,bytes));
        for(i=0;i<count;++i) {uint32_t number=xx_data_get_u32(b.p+(size_t)at, 4, 0, true),checksum=seed;int j;NH_NEED(number && number<=pages && number!=0x40000000U/page+1);for(j=(int)page-200;j>0;j-=200) checksum+=b.p[(size_t)at+4+(size_t)j];NH_NEED(checksum==xx_data_get_u32(b.p+(size_t)at+4+page, 4, 0, true) && nh_add(f,s,&b,"page",at,page+8));at+=page+8;++total;}
        if(at<b.n) {uint64_t aligned=(at+sector-1)&~(uint64_t)(sector-1);NH_NEED(aligned<b.n && nh_zero(&b,at,aligned-at));at=aligned;}
    }NH_NEED(total);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_sqlite_rollback_journal_init(xx_sqlite_rollback_journal *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SQLITE_ROLLBACK_JOURNAL,"sqlite_rollback_journal"); } }
xx_sqlite_rollback_journal *xx_sqlite_rollback_journal_create(xx_io_device *d,int64_t b) { xx_sqlite_rollback_journal *r=(xx_sqlite_rollback_journal *)xx_mem_alloc(sizeof(*r)); if(r) xx_sqlite_rollback_journal_init(r,d,b); return r; }
void xx_sqlite_rollback_journal_destroy(xx_sqlite_rollback_journal *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sqlite_rollback_journal_free(xx_sqlite_rollback_journal *r) { if(r) { xx_sqlite_rollback_journal_destroy(r); xx_mem_free(r); } }
bool xx_sqlite_rollback_journal_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sqlite_rollback_journal_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
