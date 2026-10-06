/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Grammar independently implemented from CookFS pages.tcl/fsindex.tcl and
 * Vladimir Petrigo's MIT bitrock-unpacker. See BITROCK_PROVENANCE.json.
 */
#include "xxfclib/formats/bitrock/xx_bitrock.h"
#include "../ue2_indexed.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/algo/crc/xx_crc.h"
#define BR_TYPE XX_FILE_TYPE_BITROCK
#define BR_MAX_PAGE (64U*1024U*1024U)
#define BR_MAX_INDEX BR_MAX_PAGE
#define BR_MAX_COUNT 65535U
#define BR_MAX_BLOCKS 1000000U
#define BR_MEMORY (UINT64_C(256)*1024*1024)
typedef struct br_page { int64_t offset;uint32_t stored;uint8_t digest[16]; } br_page;
typedef struct br_block { uint32_t page,offset,size; } br_block;
typedef struct br_item { br_block *blocks;uint32_t count;uint64_t mtime;bool hidden; } br_item;
typedef struct br_index { ue2_index records;br_page *pages;uint32_t page_count;br_item *items;size_t item_capacity;uint64_t metadata; } br_index;
typedef struct br_blob { uint8_t *data;size_t size,capacity;uint64_t *used,limit;bool failed; } br_blob;
typedef struct br_cache { br_blob bytes;bool ready; } br_cache;
typedef struct br_state { ue2_state reading;const br_index *index;br_cache *cache;uint64_t used,limit; } br_state;
typedef struct br_cursor { const uint8_t *data;size_t size,at;uint32_t blocks; br_index *index;xx_pd_struct *pd; } br_cursor;
static uint32_t br_be32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static uint64_t br_be64(const uint8_t *p) { return (uint64_t)br_be32(p)<<32|br_be32(p+4); }
static bool br_fail(xx_pd_struct *pd,const char *why) { xx_pd_set_error(pd,1,why);return false; }
static void br_release_blob(br_blob *blob) {
    if(blob->data){xx_mem_free(blob->data);if(blob->used)*blob->used-=blob->capacity;}blob->data=NULL;blob->size=blob->capacity=0;
}
static ssize_t br_sink_write(xx_io_device *device,const void *data,size_t n) {
    br_blob *b=device->priv;size_t need,cap;uint8_t *next;
    if(b->failed||n>BR_MAX_PAGE-b->size){b->failed=true;return -1;}need=b->size+n;
    if(need>b->capacity){cap=b->capacity?b->capacity:4096;while(cap<need){if(cap>BR_MAX_PAGE/2){cap=BR_MAX_PAGE;break;}cap*=2;}
        if(*b->used>b->limit||cap-b->capacity>b->limit-*b->used){b->failed=true;return -1;}
        next=xx_mem_realloc(b->data,cap);if(!next){b->failed=true;return -1;}*b->used+=cap-b->capacity;b->data=next;b->capacity=cap;}
    if(n) {xx_rt_memcpy(b->data+b->size,data,n); } b->size=need;return (ssize_t)n;
}
static bool br_decode_blob(const uint8_t *raw,size_t n,br_blob *out,xx_pd_struct *pd) {
    xx_io_device sink;uint64_t workspace=0;bool result=false;size_t consumed=0;
    if(!n) {return true; } xx_rt_memset(&sink,0,sizeof(sink));sink.priv=out;sink.write=br_sink_write;
    if(raw[0]==0)return br_sink_write(&sink,raw+1,n-1)==(ssize_t)(n-1);
    if(raw[0]==1)workspace=512*1024U;
    else if(raw[0]==2)workspace=16*1024*1024U;
    else if(raw[0]==255){uint32_t dictionary,property;if(n<14||raw[1]>=225)return br_fail(pd,"BitRock encrypted/custom page is unsupported");
        dictionary=ue2_u32(raw+2);property=raw[1];workspace=(uint64_t)dictionary+((uint64_t)0x300<<((property%9)+((property/9)%5)))*2+128*1024U;
        if(workspace>BR_MEMORY)return br_fail(pd,"BitRock LZMA dictionary exceeds memory limit");
    }else return br_fail(pd,"BitRock encrypted/custom page compression is unsupported");
    if(*out->used>out->limit||workspace>out->limit-*out->used)return br_fail(pd,"BitRock decoder exceeds archive memory limit");
    *out->used+=workspace;
    if(raw[0]==1)result=xx_deflate_unpack_memory_to_device_ex(raw+1,n-1,&sink,&consumed,false,pd)&&consumed==n-1;
    else if(raw[0]==2){if(n>=9)result=xx_bzip2_unpack_memory_to_device_ex(raw+5,n-5,&sink,&consumed,pd)&&consumed==n-5;}
    else {uint64_t size=ue2_u64(raw+6);if(size==UINT64_MAX||size<=BR_MAX_PAGE)result=xx_lzma_unpack_memory_to_device(raw+14,n-14,raw+1,5,size==UINT64_MAX?-1:(int64_t)size,&sink,pd);}
    *out->used-=workspace;
    return result&&!out->failed&&!(pd&&xx_pd_is_stopped(pd));
}
static void br_index_free(br_index *index) {
    size_t i;if(!index)return;for(i=0;i<index->records.count;++i)xx_mem_free(index->items[i].blocks);
    xx_mem_free(index->items);xx_mem_free(index->pages);ue2_index_free(&index->records);
}
static bool br_component(const char *name) {
    const uint8_t *p=(const uint8_t *)name;if(!ue2_safe_name(name)||xx_rt_strchr(name,'/'))return false;
    /* Tcl stores UTF-8 names; reject malformed encodings rather than aliasing them. */
    while(*p){uint32_t value;unsigned need,min;if(*p<128){++p;continue;}
        if(*p>=0xc2&&*p<=0xdf){need=1;value=*p++&31;min=0x80;}
        else if(*p>=0xe0&&*p<=0xef){need=2;value=*p++&15;min=0x800;}
        else if(*p>=0xf0&&*p<=0xf4){need=3;value=*p++&7;min=0x10000;}else return false;
        while(need--){if((*p&0xc0)!=0x80)return false;value=(value<<6)|(*p++&63);}
        if(value<min||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
    }return true;
}
static bool br_take(br_cursor *c,size_t n,const uint8_t **out) {
    if(c->at>c->size||n>c->size-c->at) {return false; } if(out)*out=c->data+c->at;c->at+=n;return true;
}
static bool br_add_item(br_cursor *c,const char *name,uint64_t mtime,uint32_t blocks,bool folder) {
    br_index *ix=c->index;br_item *item;const uint8_t *p;uint64_t compressed=0,size=0,extra;uint32_t i;size_t next_records=ix->records.capacity,next_items=ix->item_capacity;
    if(ix->records.count>=BR_MAX_COUNT||blocks>BR_MAX_BLOCKS-c->blocks)return false;
    if(ix->records.count==next_records)next_records=next_records?next_records*2:32;
    if(ix->records.count==next_items){next_items=next_items?next_items*2:32;if(next_items>BR_MAX_COUNT)next_items=BR_MAX_COUNT;}
    extra=xx_rt_strlen(name)+1+(uint64_t)blocks*sizeof(br_block)+(next_items-ix->item_capacity)*sizeof(br_item)+(next_records-ix->records.capacity)*sizeof(ue2_member);
    if(ix->metadata>BR_MAX_INDEX||extra>BR_MAX_INDEX-ix->metadata)return false;
    if(ix->records.count==ix->item_capacity){size_t cap=ix->item_capacity?ix->item_capacity*2:32;br_item *next;
        if(cap>BR_MAX_COUNT) {cap=BR_MAX_COUNT; } next=xx_mem_realloc(ix->items,cap*sizeof(*next));if(!next)return false;
        xx_rt_memset(next+ix->item_capacity,0,(cap-ix->item_capacity)*sizeof(*next));ix->items=next;ix->item_capacity=cap;}
    item=&ix->items[ix->records.count];item->mtime=mtime;item->count=blocks;
    if(blocks){item->blocks=xx_mem_calloc(blocks,sizeof(*item->blocks));if(!item->blocks)return false;}
    for(i=0;i<blocks;++i){br_block *b=&item->blocks[i];if(!br_take(c,12,&p))goto failed;
        b->page=br_be32(p);b->offset=br_be32(p+4);b->size=br_be32(p+8);
        if(b->page>=ix->page_count||b->offset>BR_MAX_PAGE||b->size>BR_MAX_PAGE-b->offset)goto failed;
        size+=b->size;compressed+=ix->pages[b->page].stored;if(size>INT64_MAX||compressed>INT64_MAX)goto failed;}
    if(!ue2_add(&ix->records,name,-1,(int64_t)compressed,0))goto failed;
    ix->records.members[ix->records.count-1].original_size=(int64_t)size;
    ix->records.members[ix->records.count-1].is_folder=folder;c->blocks+=blocks;ix->metadata+=extra;return true;
failed:
    xx_mem_free(item->blocks);item->blocks=NULL;item->count=0;return false;
}
static bool br_read_dir(br_cursor *c,const char *prefix,unsigned depth) {
    const uint8_t *p;uint32_t count,i;if(depth>64||!br_take(c,4,&p))return false;count=br_be32(p);
    if(count>BR_MAX_COUNT-c->index->records.count)return false;
    for(i=0;i<count;++i){unsigned n;char component[256],*full;uint64_t time;uint32_t blocks;bool ok;
        if((c->pd&&xx_pd_is_stopped(c->pd))||!br_take(c,1,&p)) {return false; } n=*p;
        if(!n||!br_take(c,(size_t)n+1,&p)||p[n]||xx_rt_memchr(p,0,n)) {return false; } xx_rt_memcpy(component,p,n);component[n]=0;
        if(!br_component(component)||!br_take(c,12,&p)) {return false; } time=br_be64(p);blocks=br_be32(p+8);
        full=prefix&&prefix[0]?xx_str_concat3(prefix,"/",component):xx_str_dup(component);if(!full)return false;
        if(xx_rt_strlen(full)>16384){xx_str_free(full);return false;}
        ok=br_add_item(c,full,time,blocks==UINT32_MAX?0:blocks,blocks==UINT32_MAX);
        if(ok&&blocks==UINT32_MAX) {ok=br_read_dir(c,full,depth+1); } xx_str_free(full);if(!ok)return false;}
    return true;
}
typedef struct br_name { const char *name;size_t item; } br_name;
static int br_compare_names(const void *x,const void *y) { const char *a=((const br_name *)x)->name,*b=((const br_name *)y)->name;while(*a&&*b){unsigned p=xx_rt_ascii_tolower((unsigned char)*a++),q=xx_rt_ascii_tolower((unsigned char)*b++);if(p!=q)return p<q?-1:1;}return *a?1:*b?-1:0; }
static br_name *br_find_name(const br_name *key,br_name *names,size_t count) {
    size_t low=0,high=count;while(low<high){size_t mid=low+(high-low)/2;int order=br_compare_names(key,names+mid);
        if(!order) {return names+mid; } if(order<0)high=mid;else low=mid+1;}return NULL;
}
typedef struct br_chunk { char *base;uint64_t number;size_t item; } br_chunk;
static int br_compare_chunks(const void *x,const void *y) { const br_chunk *a=x,*b=y;br_name an={a->base,0},bn={b->base,0};int r=br_compare_names(&an,&bn);return r?r:a->number<b->number?-1:a->number>b->number?1:0; }
static bool br_stitch(br_index *ix) {
    br_name *names=NULL;br_chunk *chunks=NULL;size_t i,count=0;bool ok=false;
    if(!ix->records.count)return true;
    names=xx_mem_alloc(ix->records.count*sizeof(*names));chunks=xx_mem_calloc(ix->records.count,sizeof(*chunks));if(!names||!chunks)goto done;
    for(i=0;i<ix->records.count;++i){const char *name=ix->records.members[i].name,*p=xx_rt_strstr(name,"___bitrockBigFile");names[i].name=name;names[i].item=i;
        if(p&&!ix->records.members[i].is_folder){const char *digit=p+17;uint64_t number=0;size_t n=(size_t)(p-name);
            if(!n||!*digit) {continue; } for(;*digit;++digit){if(*digit<'0'||*digit>'9'||number>(UINT64_MAX-9)/10)break;number=number*10+(*digit-'0');}if(*digit)continue;
            chunks[count].base=xx_mem_alloc(n+1);if(!chunks[count].base)goto done;xx_rt_memcpy(chunks[count].base,name,n);chunks[count].base[n]=0;chunks[count].number=number;chunks[count++].item=i;}}
    xx_rt_qsort(names,ix->records.count,sizeof(*names),br_compare_names);for(i=1;i<ix->records.count;++i)if(!br_compare_names(names+i-1,names+i))goto done;
    xx_rt_qsort(chunks,count,sizeof(*chunks),br_compare_chunks);
    for(i=0;i<count;){size_t end=i+1,j;uint64_t blocks,total,compressed;br_name key={chunks[i].base,0},*base;
        while(end<count){br_name next={chunks[end].base,0};if(br_compare_names(&key,&next))break;if(chunks[end].number==chunks[end-1].number)goto done;++end;}
        base=br_find_name(&key,names,ix->records.count);
        if(base&&!ix->records.members[base->item].is_folder){br_item *item=&ix->items[base->item];br_block *merged;size_t at=item->count;
            blocks=item->count;total=(uint64_t)ix->records.members[base->item].original_size;compressed=(uint64_t)ix->records.members[base->item].size;
            for(j=i;j<end;++j){blocks+=ix->items[chunks[j].item].count;total+=(uint64_t)ix->records.members[chunks[j].item].original_size;compressed+=(uint64_t)ix->records.members[chunks[j].item].size;}
            if(blocks>BR_MAX_BLOCKS||total>INT64_MAX||compressed>INT64_MAX) {goto done; } merged=xx_mem_alloc((size_t)(blocks?blocks:1)*sizeof(*merged));if(!merged)goto done;
            if(item->count)xx_rt_memcpy(merged,item->blocks,item->count*sizeof(*merged));
            for(j=i;j<end;++j){br_item *chunk=&ix->items[chunks[j].item];if(chunk->count)xx_rt_memcpy(merged+at,chunk->blocks,chunk->count*sizeof(*merged));at+=chunk->count;chunk->hidden=true;}
            xx_mem_free(item->blocks);item->blocks=merged;item->count=(uint32_t)blocks;ix->records.members[base->item].original_size=(int64_t)total;ix->records.members[base->item].size=(int64_t)compressed;}
        i=end;}
    {size_t out=0;for(i=0;i<ix->records.count;++i){if(ix->items[i].hidden){xx_mem_free(ix->items[i].blocks);xx_str_free(ix->records.members[i].name);}else{if(out!=i){ix->items[out]=ix->items[i];ix->records.members[out]=ix->records.members[i];}++out;}}ix->records.count=out;}
    ok=true;
done:
    if(chunks){for(i=0;i<count;++i)xx_mem_free(chunks[i].base);}xx_mem_free(chunks);xx_mem_free(names);return ok;
}
static br_index *br_at_end(Abstractformat *f,int64_t end,xx_pd_struct *pd) {
    uint8_t suffix[16],*table=NULL,*raw=NULL;uint32_t count,size,i;uint64_t directory,stored=0,used=0;int64_t start;bool ok=false;br_blob decoded={0};br_index *ix=NULL;br_cursor cursor;
    if(end<16||!ue2_read(f,f->base_address+end-16,suffix,16)||xx_rt_memcmp(suffix+9,"CFS0002",7))return NULL;
    size=br_be32(suffix);count=br_be32(suffix+4);directory=(uint64_t)size+(uint64_t)count*20+16;
    if(!size||size>BR_MAX_INDEX||count>BR_MAX_COUNT||directory>(uint64_t)end) {return NULL; } start=end-(int64_t)directory;
    ix=xx_mem_calloc(1,sizeof(*ix));table=xx_mem_alloc(count?(size_t)count*20:1);raw=xx_mem_alloc(size);
    if(!ix||!table||!raw||!ue2_read(f,f->base_address+start,table,(size_t)count*20)||!ue2_read(f,f->base_address+end-16-size,raw,size))goto done;
    ix->page_count=count;ix->metadata=sizeof(*ix)+(uint64_t)count*sizeof(br_page);if(count){ix->pages=xx_mem_calloc(count,sizeof(*ix->pages));if(!ix->pages)goto done;}
    for(i=0;i<count;++i){stored+=br_be32(table+(size_t)count*16+i*4);if(stored>(uint64_t)start)goto done;}
    start-=(int64_t)stored;
    for(i=0;i<count;++i){br_page *page=&ix->pages[i];page->offset=f->base_address+start;page->stored=br_be32(table+(size_t)count*16+i*4);xx_rt_memcpy(page->digest,table+i*16,16);
        if(page->stored>BR_MAX_PAGE) {goto done; } start+=page->stored;}
    used=(uint64_t)size+(uint64_t)count*(20+sizeof(br_page));decoded.used=&used;decoded.limit=BR_MEMORY;
    if(!br_decode_blob(raw,size,&decoded,pd)||decoded.size<12||xx_rt_memcmp(decoded.data,"CFS2.200",8))goto done;
    xx_rt_memset(&cursor,0,sizeof(cursor));cursor.data=decoded.data;cursor.size=decoded.size;cursor.at=8;cursor.index=ix;cursor.pd=pd;
    if(!br_read_dir(&cursor,"",0))goto done;
    if(cursor.at<cursor.size){const uint8_t *p;uint32_t meta,j;if(!br_take(&cursor,4,&p))goto done;meta=br_be32(p);if(meta>BR_MAX_COUNT)goto done;
        for(j=0;j<meta;++j){uint32_t n;if(!br_take(&cursor,4,&p))goto done;n=br_be32(p);if(!n||!br_take(&cursor,n,&p)||!xx_rt_memchr(p,0,n))goto done;}}
    if(cursor.at!=cursor.size||!br_stitch(ix))goto done;
    ix->records.size=end;ix->metadata=sizeof(*ix)+(uint64_t)count*sizeof(br_page)+(uint64_t)ix->item_capacity*sizeof(br_item)+(uint64_t)ix->records.capacity*sizeof(ue2_member);
    for(i=0;i<ix->records.count;++i)ix->metadata+=(uint64_t)ix->items[i].count*sizeof(br_block)+xx_rt_strlen(ix->records.members[i].name)+1;
    if(ix->metadata>BR_MAX_INDEX) {goto done; } ok=true;
done:
    br_release_blob(&decoded);xx_mem_free(table);xx_mem_free(raw);if(!ok){br_index_free(ix);return NULL;}return ix;
}
static int64_t br_decimal_offset(const uint8_t *data,size_t size) {
    static const char key[]="dist-endoffset";size_t i;
    for(i=0;i+sizeof(key)-1<size;++i)if(!xx_rt_memcmp(data+i,key,sizeof(key)-1)){size_t p=i+sizeof(key)-1;uint64_t n=0;bool digit=false;
        while(p<size&&(data[p]==' '||data[p]=='\t'||data[p]=='\r'||data[p]=='\n'))++p;
        while(p<size&&data[p]>='0'&&data[p]<='9'){digit=true;if(n>(INT64_MAX-9)/10)return -1;n=n*10+(data[p++]-'0');}
        if(digit)return (int64_t)n;}
    return -1;
}
static br_index *br_redirect(Abstractformat *,br_index *,int64_t,xx_pd_struct *);
static br_index *br_parse(Abstractformat *f,xx_pd_struct *pd) {
    int64_t size=f&&f->device?xx_io_total_size(f->device)-f->base_address:-1,preferred=-1,start,at;uint8_t *data=NULL;br_index *index=NULL;size_t n,i;unsigned candidates=0;
    if(!f||f->base_address<0||size<16||(pd&&xx_pd_is_stopped(pd)))return NULL;
    n=size>131072?131072:(size_t)size;data=xx_mem_alloc(131072+6);if(!data)return NULL;
    if(ue2_read(f,f->base_address,data,n))preferred=br_decimal_offset(data,n);
    if(preferred>=16&&preferred<=size){index=br_at_end(f,preferred,pd);if(index)goto done;}
    index=br_at_end(f,size,pd);if(index)goto done;
    /* Signed installers or launcher tails may follow the CookFS suffix. */
    start=size>16*1024*1024?size-16*1024*1024:0;at=size;
    while(at>start){int64_t low=at-start>131072?at-131072:start;size_t take=(size_t)(at-low);if(pd&&xx_pd_is_stopped(pd))break;
        if(!ue2_read(f,f->base_address+low,data,take))break;
        for(i=take;i>=7;--i)if(!xx_rt_memcmp(data+i-7,"CFS0002",7)){int64_t end=low+(int64_t)i;if(end==size||end<16)continue;if(++candidates>64)goto done;index=br_at_end(f,end,pd);if(index)goto done;}
        if(low==start) {break; } at=low+6;}
done:
    xx_mem_free(data);return index?br_redirect(f,index,size,pd):NULL;
}
bool xx_bitrock_has_candidate_device(xx_io_device *device,int64_t base) {
    Abstractformat f;uint8_t data[4096+64],suffix[16];int64_t saved,size,at;bool candidate=false;size_t carry=0;
    if(!device||base<0) {return false; } saved=xx_io_tell(device);size=xx_io_total_size(device)-base;
    xx_rt_memset(&f,0,sizeof(f));f.device=device;f.base_address=base;
    if(size<16)goto done;
    if(ue2_read(&f,base+size-16,suffix,16)&&!xx_rt_memcmp(suffix+9,"CFS0002",7)){candidate=true;goto done;}
    /* Bounded prefix/tail probes also cover ELF, shell and renamed images. */
    for(at=0;at<size&&at<131072;at+=4096){size_t n=(size_t)(size-at>4096?4096:size-at),total;int64_t end;
        if(!ue2_read(&f,base+at,data+carry,n)) {goto done; } end=br_decimal_offset(data,carry+n);
        if(end>=16&&end<=size&&ue2_read(&f,base+end-16,suffix,16)&&!xx_rt_memcmp(suffix+9,"CFS0002",7)){candidate=true;goto done;}
        total=carry+n;carry=total>64?64:total;if(carry)xx_rt_memmove(data,data+total-carry,carry);
    }
    carry=0;at=size>131072?size-131072:0;
    while(at<size){size_t n=(size_t)(size-at>4096?4096:size-at),total,i;
        if(!ue2_read(&f,base+at,data+carry,n)) {goto done; } total=carry+n;
        for(i=0;i+7<=total;++i)if(!xx_rt_memcmp(data+i,"CFS0002",7)){candidate=true;goto done;}
        carry=total>6?6:total;if(carry)xx_rt_memmove(data,data+total-carry,carry);at+=(int64_t)n;
    }
done:
    if(saved>=0) {xx_io_seek64(device,saved,XX_RT_SEEK_SET); } return candidate;
}
static bool br_valid(Abstractformat *f,xx_pd_struct *pd) { br_index *ix=br_parse(f,pd);bool valid=ix!=NULL;br_index_free(ix);return valid; }
static bool br_info(Abstractformat *f,xx_pd_struct *pd) {
    xx_bitrock *a=(xx_bitrock *)f;br_index *ix=br_parse(f,pd);if(!ix)return false;br_index_free(a->index);a->index=ix;
    f->format_size=ix->records.size;f->number_of_archive_records=ix->records.count;f->base_info_handled=f->is_valid=true;return true;
}
static bool br_page_get(Abstractformat *f,br_state *s,uint32_t n,xx_pd_struct *pd) {
    const br_page *page;uint8_t *raw,digest[16];bool ok;br_cache *cache;
    if(n>=s->index->page_count) {return false; } cache=&s->cache[n];if(cache->ready)return true;page=&s->index->pages[n];
    if(s->used>s->limit||page->stored>s->limit-s->used)return br_fail(pd,"BitRock page exceeds archive memory limit");
    raw=xx_mem_alloc(page->stored?page->stored:1);if(!raw)return false;s->used+=page->stored;
    cache->bytes.used=&s->used;cache->bytes.limit=s->limit;
    ok=ue2_read(f,page->offset,raw,page->stored)&&br_decode_blob(raw,page->stored,&cache->bytes,pd);
    s->used-=page->stored;xx_mem_free(raw);
    if(ok){if(!xx_rt_memcmp(page->digest,"\0\0\0\0\0\0\0\0",8)){
            uint32_t crc=xx_crc32_calc(0,cache->bytes.data,cache->bytes.size);ok=br_be32(page->digest+8)==cache->bytes.size&&br_be32(page->digest+12)==crc;
        }else ok=xx_md5_memory(cache->bytes.data,cache->bytes.size,digest)&&!xx_rt_memcmp(digest,page->digest,16);}
    if(!ok){br_release_blob(&cache->bytes);return br_fail(pd,"BitRock page is damaged, encrypted, unsupported or exceeds archive memory limit");}
    cache->ready=true;return true;
}
static void br_state_free(void *pointer) {
    br_state *s=pointer;uint32_t i;if(!s)return;for(i=0;i<s->index->page_count;++i)br_release_blob(&s->cache[i].bytes);xx_mem_free(s->cache);xx_mem_free(s);
}
static br_index *br_redirect(Abstractformat *f,br_index *index,int64_t size,xx_pd_struct *pd) {
    unsigned hop;
    for(hop=0;hop<4;++hop){size_t i,j;const br_item *item=NULL;br_state state;uint8_t *text=NULL;size_t at=0;int64_t end=-1;br_index *payload;
        for(i=0;i<index->records.count;++i){const char *name=index->records.members[i].name,*leaf=xx_rt_strrchr(name,'/');
            if(!xx_rt_strcmp(leaf?leaf+1:name,"cookfsinfo.txt")&&!index->records.members[i].is_folder&&index->records.members[i].original_size<=65536){item=&index->items[i];break;}}
        if(!item) {return index; } xx_rt_memset(&state,0,sizeof(state));state.index=index;state.limit=BR_MEMORY;
        state.used=index->metadata+(uint64_t)index->page_count*sizeof(br_cache)+65536;
        state.cache=xx_mem_calloc(index->page_count?index->page_count:1,sizeof(br_cache));text=xx_mem_alloc(65536);
        if(!state.cache||!text)goto clean;
        for(j=0;j<item->count;++j){const br_block *b=&item->blocks[j];
            if(!br_page_get(f,&state,b->page,pd)||b->offset>state.cache[b->page].bytes.size||b->size>state.cache[b->page].bytes.size-b->offset||b->size>65536-at)goto clean;
            if(b->size) {xx_rt_memcpy(text+at,state.cache[b->page].bytes.data+b->offset,b->size); } at+=b->size;}
        end=br_decimal_offset(text,at);
clean:
        if(state.cache){for(j=0;j<index->page_count;++j)br_release_blob(&state.cache[j].bytes);}xx_mem_free(state.cache);xx_mem_free(text);
        if(end<16||end>size||end==index->records.size)return index;
        payload=br_at_end(f,end,pd);if(!payload)return index;br_index_free(index);index=payload;
    }
    return index;
}
static xx_archive_record_state *br_records(Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd) {
    xx_archive_record_state *state=ue2_records(f,options,pd);br_state *s;uint64_t limit=BR_MEMORY;size_t i;
    if(!state) {return NULL; } for(i=0;options&&i<options->count;++i){const xx_meta *m=xx_list_at((const xx_list_t *)options,i);if(m&&m->meta_id==XX_META_ID_OPT_MEMORY_LIMIT)limit=xx_var_get_u64(&m->var);}
    if(limit>BR_MEMORY) {limit=BR_MEMORY; } s=xx_mem_calloc(1,sizeof(*s));if(!s){xx_archive_record_state_free(state);return NULL;}
    s->reading=*(ue2_state *)state->internal_state;s->index=((xx_bitrock *)f)->index;s->limit=limit;s->used=s->index->metadata+(uint64_t)s->index->page_count*sizeof(br_cache)+sizeof(*s);
    if(s->used>limit){xx_mem_free(s);xx_archive_record_state_free(state);br_fail(pd,"BitRock metadata exceeds archive memory limit");return NULL;}
    s->cache=xx_mem_calloc(s->index->page_count?s->index->page_count:1,sizeof(*s->cache));if(!s->cache){xx_mem_free(s);xx_archive_record_state_free(state);return NULL;}
    xx_mem_free(state->internal_state);state->internal_state=s;state->free_internal=br_state_free;return state;
}
static const xx_archive_record *br_current(Abstractformat *f,xx_archive_record_state *state) {
    const xx_archive_record *r=ue2_current(f,state);br_state *s=state?state->internal_state:NULL;
    if(r&&s){const br_item *item=&s->index->items[s->reading.cursor];
        if(!xx_archive_record_set_meta_u64(&state->current_record,XX_META_ID_TIMESTAMP,item->mtime))return NULL;}
    return r;
}
static bool br_unpack(Abstractformat *f,xx_archive_record_state *state,xx_pd_struct *pd) {
    const xx_archive_record *record=br_current(f,state);br_state *s=state?state->internal_state:NULL;const br_item *item;const ue2_member *member;
    const xx_var *option=NULL;const char *base=NULL;char *owned=NULL,*path=NULL;xx_io_device *out=NULL;bool result=false;size_t i;uint64_t done=0;int level=-1;
    if(!record||!s||(pd&&xx_pd_is_stopped(pd))) {return false; } item=&s->index->items[s->reading.cursor];member=&s->index->records.members[s->reading.cursor];
    for(i=0;i<state->options.count;++i){const xx_meta *m=xx_list_at((const xx_list_t *)&state->options,i);
        if(m&&m->meta_id==XX_META_ID_OPT_MAX_MEMBER_SIZE&&(uint64_t)member->original_size>xx_var_get_u64(&m->var))return br_fail(pd,"BitRock member exceeds configured size limit");}
    for(i=0;i<item->count;++i){const br_block *b=&item->blocks[i];if(!br_page_get(f,s,b->page,pd)||b->offset>s->cache[b->page].bytes.size||b->size>s->cache[b->page].bytes.size-b->offset)return false;}
    for(i=0;i<state->options.count;++i){const xx_meta *m=xx_list_at((const xx_list_t *)&state->options,i);if(m&&m->meta_id==XX_META_ID_OPT_UNPACK_PATH){option=&m->var;break;}}
    if(option){if(option->type==XX_VAR_TYPE_STRING||option->type==XX_VAR_TYPE_STRING_VIEW)base=xx_var_get_str(option);
        else if(option->type==XX_VAR_TYPE_WSTRING||option->type==XX_VAR_TYPE_WSTRING_VIEW){owned=xx_str_unicode_to_utf8(xx_var_get_wstr(option));base=owned;}
        if(base&&ue2_safe_name(member->name))path=xx_str_concat3(base,"/",member->name);
        if(!path||!xx_store_create_dirs_a(path,member->is_folder))goto done;
        if(!member->is_folder){out=xx_io_file_open(path,"wb");if(!out)goto done;}}
    level=xx_pd_enter_level(pd,(uint64_t)member->original_size,option?"Extracting BitRock member":"Testing BitRock member");
    for(i=0;i<item->count;++i){const br_block *b=&item->blocks[i];size_t copied=0;
        while(copied<b->size){size_t take=b->size-copied>65536?65536:b->size-copied;if(pd&&xx_pd_is_stopped(pd))goto done;
            if(out&&xx_io_write(out,s->cache[b->page].bytes.data+b->offset+copied,take)!=(ssize_t)take) {goto done; } copied+=take;done+=take;xx_pd_set_current(pd,level,done);}}
    result=done==(uint64_t)member->original_size&&!(pd&&xx_pd_is_stopped(pd));
done:
    if(level>=0) {xx_pd_leave_level(pd,level); } if(out)xx_io_close(out);xx_str_free(path);xx_str_free(owned);return result;
}
static void br_destroy(Abstractformat *f) { xx_bitrock *a=(xx_bitrock *)f;br_index_free(a->index);a->index=NULL;xx_format_cleanup_extra_parameters(f); }
void xx_bitrock_init(xx_bitrock *a,xx_io_device *device,int64_t base) {
    if(!a) {return; } xx_rt_memset(a,0,sizeof(*a));ue2_init_format(&a->format,device,base,BR_TYPE,"exe","application/x-bitrock-installer");
    a->format.check_is_valid=br_valid;a->format.handle_base_info=br_info;a->format.create_archive_records_reading=br_records;
    a->format.get_current_archive_record=br_current;a->format.unpack_current_archive_record=br_unpack;a->format.destroy=br_destroy;
}
xx_bitrock *xx_bitrock_create(xx_io_device *device,int64_t base) { xx_bitrock *a=xx_mem_alloc(sizeof(*a));if(a)xx_bitrock_init(a,device,base);return a; }
void xx_bitrock_destroy(xx_bitrock *a) { if(a)br_destroy(&a->format); }
void xx_bitrock_free(xx_bitrock *a) { if(a){xx_bitrock_destroy(a);xx_mem_free(a);} }
