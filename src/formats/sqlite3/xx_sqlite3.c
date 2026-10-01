/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://www.sqlite.org/fileformat.html
 * Validated database header and page framing; export raw pages, no SQL.
 */
#include "xxfclib/formats/sqlite3/xx_sqlite3.h"
#include "../xx_payload_members.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[100],b[8]; uint32_t page,pages,i,usable,encoding; int64_t end;
    if(!pm_read(f,0,h,100) || xx_rt_memcmp(h,"SQLite format 3\0",16)) return false;
    page=pm_be16(h+16); if(page==1) page=65536;
    if(page<512 || page>65536 || (page&(page-1)) || (h[18]!=1 && h[18]!=2) || (h[19]!=1 && h[19]!=2)) return false;
    usable=page-h[20]; if(usable<480 || h[21]!=64 || h[22]!=32 || h[23]!=32) return false;
    pages=pm_be32(h+28); encoding=pm_be32(h+56);
    if(!pages || pages>65536 || pm_be32(h+24)!=pm_be32(h+92) || encoding<1 || encoding>3 || pm_be32(h+44)<1 || pm_be32(h+44)>4) return false;
    if(pm_be32(h+32)>pages || pm_be32(h+36)>=pages || pm_be32(h+52)>pages || pm_be32(h+64)>1) return false;
    if((!pm_be32(h+32))!=(!pm_be32(h+36)) || pm_be32(h+32)==1 ||
       (!pm_be32(h+52) && pm_be32(h+64))) return false;
    for(i=72;i<92;++i) if(h[i]) return false;
    end=(int64_t)page*pages; if(end>pm_available(f)) return false;
    if(!pm_read(f,100,b,8) || (b[0]!=5 && b[0]!=13)) return false;
    {
        uint32_t header=b[0]==5?12:8, cells=pm_be16(b+3),start=pm_be16(b+5),freeblock=pm_be16(b+1);
        if(!start && page==65536) start=65536;
        if(100U+header+2U*cells>start || start>usable || b[7]>60 || (freeblock && (freeblock<100U+header+2U*cells || freeblock>usable-4))) return false;
    }
    for(i=0;i<pages;++i) {
        char name[48]; if(pd && xx_pd_is_stopped(pd)) return false;
        xx_rt_snprintf(name,sizeof(name),"page-%u.bin",(unsigned)i+1);
        if(!pm_add(f,s,name,(int64_t)i*page,page)) return false;
    }
    s->size=end; return true;
}

void xx_sqlite3_init(xx_sqlite3 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SQLITE3,"sqlite"); } }
xx_sqlite3 *xx_sqlite3_create(xx_io_device *d,int64_t b) { xx_sqlite3 *r=(xx_sqlite3 *)xx_mem_alloc(sizeof(*r)); if(r) xx_sqlite3_init(r,d,b); return r; }
void xx_sqlite3_destroy(xx_sqlite3 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sqlite3_free(xx_sqlite3 *r) { if(r) { xx_sqlite3_destroy(r); xx_mem_free(r); } }
bool xx_sqlite3_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sqlite3_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
