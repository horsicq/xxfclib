/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original bounded reader of AaruFormat V2 block-media layout.
 */
#include "xxfclib/formats/aaruformat/xx_aaruformat.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/store/xx_store.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xxfclib/data/xx_data.h"
#ifdef AARUFORMAT
#define AF_TYPE XX_FILE_TYPE_AARUFORMAT
#else
#define AF_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define AF_INDEX_MAX 100000U
#define AF_DDT_MAX UINT64_C(67108864)
#define AF_BLOCK_MAX UINT32_C(33554432)
#define AF_IMAGE_MAX UINT64_C(1125899906842624)
#define AF_COPY 65536U
#define AF_HEADER 147U
#define AF_DDT_HEAD 85U
#define AF_INDEX_HEAD 28U
#define AF_BLOCK_HEAD 36U
#define AF_DBLK UINT32_C(0x4b4c4244)
#define AF_DDT2 UINT32_C(0x32544444)
#define AF_IDX3 UINT32_C(0x33584449)
typedef struct af_block_s {
    uint64_t offset,end,crc;
    uint32_t length;
} af_block;
typedef struct af_view_s {
    int64_t base;uint64_t size,sectors,media_size,ddt_offset,ddt_length;
    uint8_t alignment,data_shift;uint32_t sector_size;
    uint8_t *ddt;af_block *blocks;size_t block_count,refs;
} af_view;
typedef struct af_cursor_s {af_view *view;} af_cursor;
static bool af_stop(xx_pd_struct *pd){return pd&&xx_pd_is_stopped(pd);}
static uint64_t af_crc(uint64_t crc,const uint8_t *p,size_t n){
    return xx_crc64_xz_calc(crc ^ UINT64_MAX,p,n) ^ UINT64_MAX;
}
static bool af_read(xx_io_device *d,int64_t at,void *out,size_t n,xx_pd_struct *pd){
    int64_t saved;size_t done=0;bool ok=false;
    if(!d||at<0||(!out&&n)||af_stop(pd))return false;
    saved=xx_io_tell(d);if(saved<0)return false;
    if(xx_io_seek64(d,at,SEEK_SET)==0){
        while(done<n&&!af_stop(pd)){
            ssize_t got=xx_io_read(d,(uint8_t *)out+done,n-done);
            if(got<=0||(size_t)got>n-done||af_stop(pd)) {break; } done+=(size_t)got;
        }ok=done==n&&!af_stop(pd);
    }
    if(xx_io_seek64(d,saved,SEEK_SET)!=0) {ok=false; } return ok;
}
static bool af_rel(xx_io_device *d,const af_view *v,uint64_t at,void *out,size_t n,xx_pd_struct *pd){
    return at<=v->size&&n<=v->size-at&&at<=(uint64_t)(INT64_MAX-v->base)&&
        n<=(uint64_t)(INT64_MAX-v->base)-at&&af_read(d,v->base+(int64_t)at,out,n,pd);
}
static bool af_crc_range(xx_io_device *d,const af_view *v,uint64_t at,uint64_t n,uint64_t expected,xx_pd_struct *pd){
    uint8_t buf[AF_COPY];uint64_t crc=UINT64_MAX,pos=0;
    if(at>v->size||n>v->size-at)return false;
    while(pos<n&&!af_stop(pd)){
        size_t part=(size_t)(n-pos<AF_COPY?n-pos:AF_COPY);
        if(!af_rel(d,v,at+pos,buf,part,pd))return false;
        crc=af_crc(crc,buf,part);pos+=part;
    }
    return !af_stop(pd)&&pos==n&&(crc^UINT64_MAX)==expected;
}
static bool af_write(xx_io_device *d,const uint8_t *p,size_t n,xx_pd_struct *pd){
    size_t done=0;
    while(done<n&&!af_stop(pd)){
        ssize_t got=xx_io_write(d,p+done,n-done);
        if(got<=0||(size_t)got>n-done||af_stop(pd)) {return false; } done+=(size_t)got;
    }
    return done==n&&!af_stop(pd);
}
static int af_compare_block(const void *a,const void *b){
    const af_block *x=(const af_block *)a,*y=(const af_block *)b;
    return x->offset<y->offset?-1:x->offset>y->offset?1:0;
}
static const af_block *af_find(const af_view *v,uint64_t at){
    size_t lo=0,hi=v->block_count;
    while(lo<hi){size_t m=lo+(hi-lo)/2U;
        if(v->blocks[m].offset<at)lo=m+1U;else hi=m;}
    return lo<v->block_count&&v->blocks[lo].offset==at?&v->blocks[lo]:NULL;
}
static void af_release(af_view *v){
    if(!v||--v->refs) {return; } xx_mem_free(v->ddt);xx_mem_free(v->blocks);xx_mem_free(v);
}
static af_view *af_parse(Abstractformat *f,xx_pd_struct *pd){
    af_view *v=NULL;uint8_t head[AF_HEADER],idx[AF_INDEX_HEAD],ddt[AF_DDT_HEAD],bh[AF_BLOCK_HEAD];
    uint8_t *entries=NULL;uint64_t index_offset,ddt_offset=0,ddt_end=0,align,entry_count,entry_bytes;
    uint64_t i,crc;size_t nblocks=0;int64_t total;
    if(!f||!f->device||f->base_address<0||af_stop(pd))return NULL;
    total=xx_io_total_size(f->device);
    if(total<f->base_address||(uint64_t)(total-f->base_address)>AF_IMAGE_MAX)return NULL;
    v=(af_view *)xx_mem_calloc(1U,sizeof(*v));if(!v)return NULL;
    v->base=f->base_address;v->size=(uint64_t)(total-f->base_address);v->refs=1U;
    if(!af_rel(f->device,v,0U,head,sizeof(head),pd)||memcmp(head,"AARUFRMT",8U)||
       head[72U]!=2U||head[73U]!=0U||
       (xx_data_get_u32(head+76U, 4, 0, false)!=1U&&xx_data_get_u32(head+76U, 4, 0, false)!=2U)||
       head[120U]<9U||head[120U]>20U||
       xx_data_get_u16(head+121U, 2, 0, false)<128U||
       xx_data_get_u64(head+123U, 8, 0, false)||xx_data_get_u64(head+131U, 8, 0, false)||xx_data_get_u64(head+139U, 8, 0, false))goto fail;
    /* biggestSectorSize is uint16; 65536 is intentionally excluded. */
    v->alignment=head[120U];v->sector_size=xx_data_get_u16(head+121U, 2, 0, false);align=UINT64_C(1)<<v->alignment;
    index_offset=xx_data_get_u64(head+80U, 8, 0, false);
    if(index_offset<AF_HEADER||index_offset%align||index_offset>v->size||
       v->size-index_offset<AF_INDEX_HEAD||
       !af_rel(f->device,v,index_offset,idx,sizeof(idx),pd)||xx_data_get_u32(idx, 4, 0, false)!=AF_IDX3||xx_data_get_u64(idx+20U, 8, 0, false))goto fail;
    entry_count=xx_data_get_u64(idx+4U, 8, 0, false);
    if(entry_count<2U||entry_count>AF_INDEX_MAX||entry_count>UINT64_MAX/14U)goto fail;
    entry_bytes=entry_count*14U;
    if(entry_bytes!=v->size-index_offset-AF_INDEX_HEAD)goto fail;
    entries=(uint8_t *)xx_mem_alloc((size_t)entry_bytes);if(!entries||
       !af_rel(f->device,v,index_offset+AF_INDEX_HEAD,entries,(size_t)entry_bytes,pd))goto fail;
    crc=af_crc(UINT64_MAX,entries,(size_t)entry_bytes)^UINT64_MAX;
    if(crc!=xx_data_get_u64(idx+12U, 8, 0, false))goto fail;
    for(i=0;i<entry_count;++i){const uint8_t *e=entries+i*14U;uint32_t type=xx_data_get_u32(e, 4, 0, false);
        if(xx_data_get_u16(e+4U, 2, 0, false)!=1U)goto fail;
        if(type==AF_DBLK)++nblocks;
        else if(type==AF_DDT2){if(ddt_offset)goto fail;ddt_offset=xx_data_get_u64(e+6U, 8, 0, false);}
        else goto fail;
    }
    if(!nblocks||!ddt_offset||nblocks>AF_INDEX_MAX-1U)goto fail;
    v->blocks=(af_block *)xx_mem_calloc(nblocks,sizeof(*v->blocks));if(!v->blocks)goto fail;
    v->block_count=nblocks;nblocks=0;
    if(ddt_offset<AF_HEADER||ddt_offset%align||ddt_offset>=index_offset||
       !af_rel(f->device,v,ddt_offset,ddt,sizeof(ddt),pd)||
       xx_data_get_u32(ddt, 4, 0, false)!=AF_DDT2||xx_data_get_u16(ddt+4U, 2, 0, false)!=1U||xx_data_get_u16(ddt+6U, 2, 0, false)!=0U||
       ddt[8U]!=1U||ddt[9U]!=0U||xx_data_get_u64(ddt+10U, 8, 0, false)||xx_data_get_u32(ddt+18U, 4, 0, false)||
       xx_data_get_u32(ddt+30U, 4, 0, false)||xx_data_get_u64(ddt+34U, 8, 0, false)||ddt[42U]!=v->alignment||
       ddt[43U]>16U||ddt[44U])goto fail;
    v->sectors=xx_data_get_u64(ddt+22U, 8, 0, false);v->data_shift=ddt[43U];v->ddt_length=xx_data_get_u64(ddt+61U, 8, 0, false);
    if(!v->sectors||v->sectors>AF_DDT_MAX/8U||
       v->sectors>UINT64_MAX/v->sector_size||
       xx_data_get_u64(ddt+45U, 8, 0, false)!=v->sectors||v->ddt_length!=v->sectors*8U||
       xx_data_get_u64(ddt+53U, 8, 0, false)!=v->ddt_length||xx_data_get_u64(ddt+69U, 8, 0, false)!=xx_data_get_u64(ddt+77U, 8, 0, false))goto fail;
    v->media_size=v->sectors*v->sector_size;v->ddt_offset=ddt_offset;
    ddt_end=ddt_offset+AF_DDT_HEAD+v->ddt_length;
    if(ddt_end<ddt_offset||ddt_end>index_offset)goto fail;
    v->ddt=(uint8_t *)xx_mem_alloc((size_t)v->ddt_length);
    if(!v->ddt||!af_rel(f->device,v,ddt_offset+AF_DDT_HEAD,v->ddt,(size_t)v->ddt_length,pd))goto fail;
    if((af_crc(UINT64_MAX,v->ddt,(size_t)v->ddt_length)^UINT64_MAX)!=xx_data_get_u64(ddt+77U, 8, 0, false))goto fail;
    for(i=0;i<entry_count;++i){const uint8_t *e=entries+i*14U;uint64_t at;
        af_block *b;
        if(xx_data_get_u32(e, 4, 0, false)!=AF_DBLK)continue;
        at=xx_data_get_u64(e+6U, 8, 0, false);if(at<AF_HEADER||at%align||at>=index_offset||
            !af_rel(f->device,v,at,bh,sizeof(bh),pd)||xx_data_get_u32(bh, 4, 0, false)!=AF_DBLK||
            xx_data_get_u16(bh+4U, 2, 0, false)!=1U||xx_data_get_u16(bh+6U, 2, 0, false)!=0U||
            xx_data_get_u32(bh+8U, 4, 0, false)!=v->sector_size||xx_data_get_u32(bh+12U, 4, 0, false)!=xx_data_get_u32(bh+16U, 4, 0, false)||
            !xx_data_get_u32(bh+16U, 4, 0, false)||xx_data_get_u32(bh+16U, 4, 0, false)>AF_BLOCK_MAX||
            xx_data_get_u32(bh+16U, 4, 0, false)%v->sector_size||
            xx_data_get_u32(bh+16U, 4, 0, false)/v->sector_size>(UINT32_C(1)<<v->data_shift)||
            xx_data_get_u64(bh+20U, 8, 0, false)!=xx_data_get_u64(bh+28U, 8, 0, false))goto fail;
        b=&v->blocks[nblocks++];b->offset=at;b->length=xx_data_get_u32(bh+16U, 4, 0, false);
        b->end=at+AF_BLOCK_HEAD+b->length;b->crc=xx_data_get_u64(bh+28U, 8, 0, false);
        if(b->end<at||b->end>index_offset||
           !af_crc_range(f->device,v,at+AF_BLOCK_HEAD,b->length,b->crc,pd))goto fail;
    }
    qsort(v->blocks,v->block_count,sizeof(*v->blocks),af_compare_block);
    for(i=0;i<v->block_count;++i){
        const af_block *b=&v->blocks[i];
        if(i&&v->blocks[i-1U].end>b->offset)goto fail;
        if(b->offset<ddt_end&&b->end>ddt_offset)goto fail;
    }
    for(i=0;i<v->sectors;++i){
        uint64_t encoded=xx_data_get_u64(v->ddt+i*8U, 8, 0, false),address,ordinal;const af_block *b;
        if(af_stop(pd)||(encoded>>60)!=1U)goto fail;
        encoded&=UINT64_C(0x0fffffffffffffff);
        ordinal=encoded&((UINT64_C(1)<<v->data_shift)-1U);
        address=(encoded>>v->data_shift);
        if(address>UINT64_MAX/align) {goto fail; } address*=align;
        b=af_find(v,address);
        if(!b||ordinal>=b->length/v->sector_size)goto fail;
    }
    xx_mem_free(entries);return v;
fail:xx_mem_free(entries);af_release(v);return NULL;
}
static void af_destroy_format(Abstractformat *f){xx_aaruformat_destroy((xx_aaruformat *)f);}
void xx_aaruformat_init(xx_aaruformat *v,xx_io_device *d,int64_t base){
    if(!v) {return; } xx_mem_zero(v,sizeof(*v));xx_format_init(&v->format,d,base);
    v->format.file_type=AF_TYPE;v->format.format_type=XX_TYPE_ARCHIVE;v->format.is_archive=true;
    xx_format_set_mime_type(&v->format,"application/x-aaruformat");
    xx_format_set_extension(&v->format,"aif");
    v->format.check_is_valid=xx_aaruformat_check_is_valid;
    v->format.handle_base_info=xx_aaruformat_handle_base_info;
    v->format.get_format_size=xx_aaruformat_get_format_size;
    v->format.get_number_of_archive_records=xx_aaruformat_get_number_of_archive_records;
    v->format.create_archive_records_reading=xx_aaruformat_create_archive_records_reading;
    v->format.get_current_archive_record=xx_aaruformat_get_current_archive_record;
    v->format.archive_record_move_to_next=xx_aaruformat_archive_record_move_to_next;
    v->format.unpack_current_archive_record=xx_aaruformat_unpack_current_archive_record;
    v->format.free_archive_records_reading=xx_aaruformat_free_archive_records_reading;
    v->format.destroy=af_destroy_format;
}
xx_aaruformat *xx_aaruformat_create(xx_io_device *d,int64_t base){
    xx_aaruformat *v=(xx_aaruformat *)xx_mem_alloc(sizeof(*v));if(v)xx_aaruformat_init(v,d,base);return v;
}
void xx_aaruformat_destroy(xx_aaruformat *v){
    if(!v) {return; } af_release((af_view *)v->internal);v->internal=NULL;
    xx_format_cleanup_extra_parameters(&v->format);
}
void xx_aaruformat_free(xx_aaruformat *v){if(v){xx_aaruformat_destroy(v);xx_mem_free(v);}}
bool xx_aaruformat_check_is_valid(Abstractformat *f,xx_pd_struct *pd){
    af_view *v=af_parse(f,pd);if(!v)return false;af_release(v);return true;
}
bool xx_aaruformat_handle_base_info(Abstractformat *f,xx_pd_struct *pd){
    xx_aaruformat *a=(xx_aaruformat *)f;af_view *v;
    if(!f||af_stop(pd))return false;
    if(f->base_info_handled&&a->internal)return f->is_valid;
    v=af_parse(f,pd);if(!v){f->base_info_handled=false;f->is_valid=false;return false;}
    af_release((af_view *)a->internal);a->internal=v;a->sectors=v->sectors;
    a->media_size=v->media_size;a->sector_size=v->sector_size;
    f->number_of_archive_records=1U;f->format_size=(int64_t)v->size;
    f->overlay_offset=-1;f->overlay_size=0;f->is_valid=true;f->base_info_handled=true;return true;
}
int64_t xx_aaruformat_get_format_size(Abstractformat *f,xx_pd_struct *pd){return xx_aaruformat_handle_base_info(f,pd)?f->format_size:-1;}
uint64_t xx_aaruformat_get_number_of_archive_records(Abstractformat *f,xx_pd_struct *pd){return xx_aaruformat_handle_base_info(f,pd)?1U:0U;}
static bool af_record(xx_archive_record *r,const af_view *v){
    xx_archive_record_cleanup(r);xx_archive_record_init(r);
    r->header_offset=v->base;r->header_size=AF_HEADER;r->data_offset=-1;r->compressed_size=(int64_t)v->size;
    return xx_archive_record_set_original_name(r,"media.img")&&
        xx_archive_record_set_meta_u64(r,XX_META_ID_UNCOMPRESSED_SIZE,v->media_size)&&
        xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSED_SIZE,v->size)&&
        xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSION_METHOD,0U)&&
        xx_archive_record_set_meta_bool(r,XX_META_ID_IS_FOLDER,false);
}
static void af_cursor_free(void *ptr){af_cursor *c=(af_cursor *)ptr;if(c){af_release(c->view);xx_mem_free(c);}}
xx_archive_record_state *xx_aaruformat_create_archive_records_reading(Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd){
    xx_archive_record_state *s;af_cursor *c;af_view *v;size_t i;
    if(!xx_aaruformat_handle_base_info(f,pd)) {return NULL; } v=(af_view *)((xx_aaruformat *)f)->internal;
    s=(xx_archive_record_state *)xx_mem_alloc(sizeof(*s));c=(af_cursor *)xx_mem_calloc(1U,sizeof(*c));
    if(!s||!c){xx_mem_free(s);xx_mem_free(c);return NULL;}
    ++v->refs;c->view=v;xx_archive_record_state_init(s,f);s->internal_state=c;s->free_internal=af_cursor_free;
    s->total_records=1;
    if(options)for(i=0;i<options->count;++i){
        const xx_meta *m=(const xx_meta *)xx_list_at(options,i);xx_meta copy;
        if(!m) {continue; } xx_meta_init(&copy,m->meta_id);
        if(!xx_var_copy(&copy.var,&m->var)||!xx_list_append(&s->options,&copy)){
            xx_meta_cleanup(&copy);xx_archive_record_state_free(s);return NULL;
        }
    }
    if(!af_record(&s->current_record,v)){xx_archive_record_state_free(s);return NULL;}
    s->has_record=true;s->current_index=0;return s;
}
const xx_archive_record *xx_aaruformat_get_current_archive_record(Abstractformat *f,xx_archive_record_state *s){
    return f&&s&&s->format==f&&s->has_record?&s->current_record:NULL;
}
bool xx_aaruformat_archive_record_move_to_next(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd){
    if(!f||!s||s->format!=f||!s->has_record||af_stop(pd))return false;
    s->has_record=false;xx_archive_record_cleanup(&s->current_record);xx_archive_record_init(&s->current_record);return false;
}
static bool af_limits(Abstractformat *f,const xx_archive_record_state *s,const af_view *v){
    const xx_var *max=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    const xx_var *mem=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t required=sizeof(*v)+v->ddt_length+v->block_count*sizeof(*v->blocks)+sizeof(af_cursor)+AF_COPY;
    return (!max||v->media_size<=xx_var_get_u64(max))&&(!mem||required<=xx_var_get_u64(mem));
}
bool xx_aaruformat_extract_record_to_device(Abstractformat *f,xx_archive_record_state *s,xx_io_device *dst,xx_pd_struct *pd){
    af_cursor *c;af_view *v;uint64_t i,align;uint8_t buf[65536];
    if(!f||!f->device||dst==f->device||!s||s->format!=f||!s->has_record||af_stop(pd)||
       !(c=(af_cursor *)s->internal_state)||!af_limits(f,s,c->view))return false;
    v=c->view;align=UINT64_C(1)<<v->alignment;
    for(i=0;i<v->block_count;++i){const af_block *b=&v->blocks[i];
        if(!af_crc_range(f->device,v,b->offset+AF_BLOCK_HEAD,b->length,b->crc,pd))return false;}
    for(i=0;i<v->sectors&&!af_stop(pd);++i){
        uint64_t encoded=xx_data_get_u64(v->ddt+i*8U, 8, 0, false)&UINT64_C(0x0fffffffffffffff);
        uint64_t ordinal=encoded&((UINT64_C(1)<<v->data_shift)-1U);
        uint64_t address=(encoded>>v->data_shift)*align;const af_block *b=af_find(v,address);
        if(!b||ordinal>=b->length/v->sector_size||
           !af_rel(f->device,v,b->offset+AF_BLOCK_HEAD+ordinal*v->sector_size,buf,v->sector_size,pd))return false;
        if(dst&&!af_write(dst,buf,v->sector_size,pd))return false;
    }
    return i==v->sectors&&!af_stop(pd);
}
static char af_fold(char c){return c>='A'&&c<='Z'?(char)(c+32):c;}
static bool af_equal(const char *a,const char *b){while(*a&&af_fold(*a)==af_fold(*b)){++a;++b;}return *a==*b;}
static xx_io_device *af_stage(const char *dest,char **path){
    size_t i,parent=0;unsigned attempt;char *dir=xx_str_dup(dest);*path=NULL;if(!dir)return NULL;
    for(i=0;dir[i];++i) {if(dir[i]=='/'||dir[i]=='\\')parent=i+1U; } dir[parent]=0;
    for(attempt=0;attempt<128U;++attempt){
        char suffix[44];char *candidate;xx_io_device *d;
        (void)xx_rt_snprintf(suffix,sizeof(suffix),".xx_aaruformat.tmp.%u",attempt);
        candidate=xx_str_concat(dir,suffix);if(!candidate)break;
        if(af_equal(candidate,dest)){xx_str_free(candidate);continue;}
        d=xx_io_file_open(candidate,"wbx");if(d){*path=candidate;xx_str_free(dir);return d;}xx_str_free(candidate);
    }xx_str_free(dir);return NULL;
}
bool xx_aaruformat_unpack_current_archive_record(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd){
    af_cursor *c;const xx_var *option,*ov;const char *base=NULL;char *owned=NULL,*path=NULL,*stage=NULL;
    bool ok=false,overwrite;
    if(!f||!s||s->format!=f||!s->has_record||af_stop(pd)||
       !(c=(af_cursor *)s->internal_state)||!af_limits(f,s,c->view))return false;
    option=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_UNPACK_PATH);
    ov=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_OVERWRITE);overwrite=ov&&xx_var_get_bool(ov);
    if(!option)return xx_aaruformat_extract_record_to_device(f,s,NULL,pd);
    if(option->type==XX_VAR_TYPE_STRING||option->type==XX_VAR_TYPE_STRING_VIEW)base=xx_var_get_str(option);
    else if(option->type==XX_VAR_TYPE_WSTRING||option->type==XX_VAR_TYPE_WSTRING_VIEW){
        owned=xx_str_unicode_to_utf8(xx_var_get_wstr(option));base=owned;
    }
    if(!base)goto done;
    path=*base&&base[strlen(base)-1U]!='/'&&base[strlen(base)-1U]!='\\'?
        xx_str_concat3(base,"/","media.img"):xx_str_concat(base,"media.img");
    if(!path||(!overwrite&&xx_io_file_exists_a(path))||
       !xx_store_create_dirs_a(path,false)||af_stop(pd))goto done;
    {xx_io_device *out=af_stage(path,&stage);if(!out)goto done;
        ok=xx_aaruformat_extract_record_to_device(f,s,out,pd);if(xx_io_close(out)!=0)ok=false;}
    if(ok&&!af_stop(pd))ok=xx_io_file_replace_a(stage,path,overwrite);else ok=false;
done:if(stage){if(!ok)(void)xx_io_file_remove_a(stage);xx_str_free(stage);}
    xx_str_free(path);xx_str_free(owned);return ok;
}
void xx_aaruformat_free_archive_records_reading(Abstractformat *f,xx_archive_record_state *s){(void)f;xx_archive_record_state_free(s);}
