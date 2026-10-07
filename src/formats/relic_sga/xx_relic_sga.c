/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/panzi/HLLib/master/HLLib/SGAFile.h
 * Relic SGA6.0, one section/root folder, flat stored files with directory32-bit indices and26-byte file records, up to1024 members. Validates directory/table/name bounds and stored CRC32. Exports file bytes with safe numeric names; zlib compression, nested/multiple roots, other versions and package trust unsupported.
 */
#include "xxfclib/formats/relic_sga/xx_relic_sga.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static XXFC_MAYBE_UNUSED uint16_t g16(const uint8_t *p,bool be) { return be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false); }
static XXFC_MAYBE_UNUSED uint32_t g32(const uint8_t *p,bool be) { return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false); }
static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) { return n && m && a<b+m && b<a+n; }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i;
    if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) if(overlap(at,n,(uint64_t)(s->items[i].offset-f->base_address),(uint64_t)s->items[i].size)) return false;
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static bool zname(Abstractformat *f,uint64_t at,uint64_t end,bool empty) {
    uint8_t c; uint64_t i; if(at>=end || end>(uint64_t)pm_available(f)) return false;
    for(i=0;i<4096 && at+i<end;++i) { if(!pm_read(f,(int64_t)(at+i),&c,1)) return false; if(!c) return empty || i!=0; } return false;
}
static XXFC_MAYBE_UNUSED bool bom(const uint8_t *p,bool *be) { *be=p[0]==0xfe && p[1]==0xff; return *be || (p[0]==0xff && p[1]==0xfe); }


static bool crc_range(Abstractformat *f,uint64_t at,uint64_t n,uint32_t expected,xx_pd_struct *pd) {
    size_t capacity=xx_get_file_buffer_size(); uint8_t *b=NULL; bool buffer_result=false; uint32_t crc=0U;
    while(n) {if(!b) { if((uint64_t)(n)<capacity) capacity=(size_t)(n); b=(uint8_t *)xx_mem_alloc(capacity); if(!b) { buffer_result=false; goto buffer_done; } }  size_t part=n>capacity ? capacity : (size_t)n;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)at,b,part)) { buffer_result = (false); goto buffer_done; }
        crc=xx_crc32_calc(crc,b,part);
        at+=part; n-=part;
    }
    { buffer_result = (crc==expected); goto buffer_done; }

buffer_done:
    xx_mem_free(b);
    return buffer_result;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[152],d[32],section[148],folder[20],e[26]; uint32_t len,data,offsets[4],counts[4],i,j; uint64_t table_sizes[4],total=0,base=152; char label[40];
    if(!pm_read(f,0,h,152) || xx_rt_memcmp(h,"_ARCHIVE",8) || xx_data_get_u16(h+8, 2, 0, false)!=6 || xx_data_get_u16(h+10, 2, 0, false) || xx_data_get_u32(h+148, 4, 0, false)) return false;
    len=xx_data_get_u32(h+140, 4, 0, false); data=xx_data_get_u32(h+144, 4, 0, false); if(len<32 || data<base+len || !span(base,len,(uint64_t)pm_available(f)) || !pm_read(f,152,d,32)) return false;
    for(i=0;i<4;++i) { offsets[i]=xx_data_get_u32(d+i*8, 4, 0, false); counts[i]=xx_data_get_u32(d+i*8+4, 4, 0, false); }
    if(counts[0]!=1 || counts[1]!=1 || !counts[2] || counts[2]>1024 || !counts[3] || offsets[3]>=len) return false;
    table_sizes[0]=148; table_sizes[1]=20; table_sizes[2]=(uint64_t)counts[2]*26; table_sizes[3]=len-offsets[3];
    for(i=0;i<4;++i) { if(offsets[i]<32 || !span(offsets[i],table_sizes[i],len)) return false; for(j=0;j<i;++j) if(overlap(offsets[i],table_sizes[i],offsets[j],table_sizes[j])) return false; }
    if(!pm_read(f,(int64_t)(base+offsets[0]),section,148) || !pm_read(f,(int64_t)(base+offsets[1]),folder,20) || !xx_rt_memchr(section,0,64) || !xx_rt_memchr(section+64,0,64)) return false;
    if(xx_data_get_u32(section+128, 4, 0, false) || xx_data_get_u32(section+132, 4, 0, false)!=1 || xx_data_get_u32(section+136, 4, 0, false) || xx_data_get_u32(section+140, 4, 0, false)!=counts[2] || xx_data_get_u32(section+144, 4, 0, false) || xx_data_get_u32(folder+4, 4, 0, false) || xx_data_get_u32(folder+8, 4, 0, false) || xx_data_get_u32(folder+12, 4, 0, false) || xx_data_get_u32(folder+16, 4, 0, false)!=counts[2] || xx_data_get_u32(folder, 4, 0, false)>=table_sizes[3] || !zname(f,base+offsets[3]+xx_data_get_u32(folder, 4, 0, false),base+len,true)) return false;
    total=base+len;
    for(i=0;i<counts[2];++i) { uint32_t name,off,packed,n; uint64_t at;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)(base+offsets[2]+i*26),e,26)) return false;
        name=xx_data_get_u32(e, 4, 0, false); off=xx_data_get_u32(e+4, 4, 0, false); packed=xx_data_get_u32(e+8, 4, 0, false); n=xx_data_get_u32(e+12, 4, 0, false); at=(uint64_t)data+off;
        if(e[20] || e[21] || packed!=n || name>=table_sizes[3] || !zname(f,base+offsets[3]+name,base+len,false) || !span(at,n,(uint64_t)pm_available(f)) || !crc_range(f,at,n,xx_data_get_u32(e+22, 4, 0, false),pd)) return false;
        xx_rt_snprintf(label,sizeof(label),"file-%u.bin",i); if(!emit(f,s,label,at,n,(uint64_t)pm_available(f))) return false; if(at+n>total) total=at+n;
    }
    s->size=(int64_t)total; return true;

}

void xx_relic_sga_init(xx_relic_sga *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_RELIC_SGA,"sga"); } }
xx_relic_sga *xx_relic_sga_create(xx_io_device *d,int64_t b) { xx_relic_sga *r=(xx_relic_sga *)xx_mem_alloc(sizeof(*r)); if(r) xx_relic_sga_init(r,d,b); return r; }
void xx_relic_sga_destroy(xx_relic_sga *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_relic_sga_free(xx_relic_sga *r) { if(r) { xx_relic_sga_destroy(r); xx_mem_free(r); } }
bool xx_relic_sga_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_relic_sga_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
