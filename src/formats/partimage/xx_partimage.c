/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original bounded implementation of the Partimage 0.6.1 disk layout.
 */
#include "xxfclib/formats/partimage/xx_partimage.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/store/xx_store.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#ifdef PARTIMAGE
#define PI_TYPE XX_FILE_TYPE_PARTIMAGE
#else
#define PI_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define PI_COPY 65536U
#define PI_BITMAP_MAX UINT64_C(67108864)
#define PI_IMAGE_MAX UINT64_C(1125899906842624)
#define PI_HEAD 16384U
#define PI_VOLUME 512U
#define PI_TAIL 16384U
typedef struct pi_view_s {
    int64_t base;
    uint64_t size, part_size, blocks, used, bitmap_size, data_offset, prefix_sum;
    uint32_t block_size;
    uint8_t *bitmap;
    size_t refs;
} pi_view;
typedef struct pi_stream_s {
    xx_io_device *device;
    pi_view *view;
    xx_pd_struct *pd;
    uint64_t pos, sum;
} pi_stream;
typedef struct pi_cursor_s { pi_view *view; bool at_end; } pi_cursor;
static bool pi_stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static uint32_t pi_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
        ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint64_t pi_u64(const uint8_t *p) { return (uint64_t)pi_u32(p) | ((uint64_t)pi_u32(p+4U)<<32); }
static uint32_t pi_crc(uint32_t crc,const uint8_t *p,size_t n) {
    /* Partimage keeps the unfinalized register between 64 KiB chunks. */
    return ~xx_crc32_calc(~crc, p, n);
}
static uint32_t pi_signed_sum(const uint8_t *p,size_t n) {
    uint32_t sum=0;size_t i;
    for(i=0;i<n;++i)sum+=(uint32_t)(int32_t)(int8_t)p[i];
    return sum;
}
static bool pi_read(xx_io_device *d,int64_t offset,void *out,size_t n,xx_pd_struct *pd) {
    int64_t saved;size_t done=0;bool ok=false;
    if(!d||offset<0||(!out&&n)||pi_stop(pd))return false;
    saved=xx_io_tell(d);if(saved<0)return false;
    if(xx_io_seek64(d,offset,SEEK_SET)==0) {
        while(done<n&&!pi_stop(pd)) {
            ssize_t got=xx_io_read(d,(uint8_t *)out+done,n-done);
            if(got<=0||(size_t)got>n-done||pi_stop(pd))break;
            done+=(size_t)got;
        }
        ok=done==n&&!pi_stop(pd);
    }
    if(xx_io_seek64(d,saved,SEEK_SET)!=0)ok=false;
    return ok;
}
static bool pi_take(pi_stream *s,void *out,size_t n,bool checksum) {
    size_t i;
    if(pi_stop(s->pd)||s->pos>s->view->size||n>s->view->size-s->pos||
       s->pos>(uint64_t)(INT64_MAX-s->view->base)||
       n>(uint64_t)(INT64_MAX-s->view->base)-s->pos||
       !pi_read(s->device,s->view->base+(int64_t)s->pos,out,n,s->pd))return false;
    if(checksum){const uint8_t *p=(const uint8_t *)out;for(i=0;i<n;++i)s->sum+=p[i];}
    s->pos+=n;return true;
}
static bool pi_magic(pi_stream *s,const char *magic) {
    uint8_t b[32];size_t n=strlen(magic);
    return n<=sizeof(b)&&pi_take(s,b,n,true)&&memcmp(b,magic,n)==0;
}
static bool pi_header(pi_stream *s,uint8_t *out,size_t n) {
    uint8_t sum[4];
    return pi_take(s,out,n,true)&&pi_take(s,sum,sizeof(sum),true)&&
        pi_u32(sum)==pi_signed_sum(out,n);
}
static bool pi_cstring(const uint8_t *p,size_t n,const char *expected) {
    size_t len=strlen(expected);
    return len<n&&memcmp(p,expected,len)==0&&p[len]==0;
}
static bool pi_zero(const uint8_t *p,size_t n) {size_t i;for(i=0;i<n;++i)if(p[i])return false;return true;}
static bool pi_write(xx_io_device *d,const uint8_t *p,size_t n,xx_pd_struct *pd) {
    size_t done=0;
    while(done<n&&!pi_stop(pd)) {
        ssize_t got=xx_io_write(d,p+done,n-done);
        if(got<=0||(size_t)got>n-done||pi_stop(pd))return false;
        done+=(size_t)got;
    }
    return done==n&&!pi_stop(pd);
}
static bool pi_bit(const pi_view *v,uint64_t i) {
    return (v->bitmap[i>>3]&(uint8_t)(1U<<(unsigned)(i&7U)))!=0;
}
static bool pi_data(pi_stream *s,xx_io_device *dst) {
    pi_view *v=s->view;uint8_t buf[PI_COPY],check[16];
    uint64_t block=0,used=0,written=0;uint32_t crc=UINT32_MAX,filled=0;
    uint32_t max_run=262144U/v->block_size;
    if(!max_run)return false;
    while(block<v->blocks&&!pi_stop(s->pd)) {
        bool present=pi_bit(v,block);uint64_t run=1U,start=block,remaining;
        while(run<max_run&&block+run<v->blocks&&pi_bit(v,block+run)==present)++run;
        remaining=run*v->block_size;
        if(present) {
            used+=run;
            while(remaining&&!pi_stop(s->pd)) {
                size_t part=(size_t)(remaining<PI_COPY?remaining:PI_COPY);
                if(part>PI_COPY-filled)part=PI_COPY-filled;
                if(!pi_take(s,buf,part,true))return false;
                crc=pi_crc(crc,buf,part);filled+=(uint32_t)part;
                if(dst&&!pi_write(dst,buf,part,s->pd))return false;
                remaining-=part;written+=part;
                if(filled==PI_COPY) {
                    if(!pi_take(s,check,sizeof(check),true)||memcmp(check,"CHK\0",4U)||
                       pi_u32(check+4U)!=(crc^UINT32_MAX)||pi_u64(check+8U)!=start)return false;
                    crc=UINT32_MAX;filled=0;
                }
            }
        } else {
            if(dst) {
                memset(buf,0,sizeof(buf));
                while(remaining&&!pi_stop(s->pd)) {
                    size_t part=(size_t)(remaining<PI_COPY?remaining:PI_COPY);
                    if(!pi_write(dst,buf,part,s->pd))return false;
                    remaining-=part;written+=part;
                }
            } else written+=remaining;
        }
        block+=run;
    }
    return !pi_stop(s->pd)&&used==v->used&&written==v->part_size;
}
static bool pi_tail(pi_stream *s) {
    uint8_t tail[PI_TAIL];uint64_t expected;
    if(!pi_magic(s,"MAGIC-BEGIN-TAIL"))return false;
    expected=s->sum;
    if(!pi_take(s,tail,sizeof(tail),false)||s->pos!=s->view->size)return false;
    return pi_u64(tail)==expected&&pi_u32(tail+8U)==0U&&pi_zero(tail+12U,sizeof(tail)-12U);
}
static void pi_release(pi_view *v) {
    if(!v||--v->refs)return;
    xx_mem_free(v->bitmap);xx_mem_free(v);
}
static pi_view *pi_parse(Abstractformat *f,xx_pd_struct *pd) {
    pi_view *v;pi_stream s;uint8_t volume[PI_VOLUME],main[PI_HEAD],local[PI_HEAD],info[PI_HEAD],length[4];
    int64_t total;uint64_t i,count=0,bitmap_min;
    if(!f||!f->device||f->base_address<0||pi_stop(pd))return NULL;
    total=xx_io_total_size(f->device);
    if(total<0||total<f->base_address||(uint64_t)(total-f->base_address)>PI_IMAGE_MAX)return NULL;
    v=(pi_view *)xx_mem_calloc(1U,sizeof(*v));if(!v)return NULL;
    v->base=f->base_address;v->size=(uint64_t)(total-f->base_address);v->refs=1U;
    memset(&s,0,sizeof(s));s.device=f->device;s.view=v;s.pd=pd;
    if(!pi_take(&s,volume,sizeof(volume),false)||
       !pi_cstring(volume,32U,"PaRtImAgE-VoLuMe")||
       !pi_cstring(volume+32U,64U,"0.6.1")||pi_u32(volume+96U)!=0U)goto fail;
    if(!pi_header(&s,main,sizeof(main))||!memchr(main,0,512U)||
       !pi_cstring(main+9728U,64U,"0.6.1")||
       pi_u32(main+9540U)!=0U||pi_u32(main+9544U)!=0U||
       pi_u32(main+9792U)!=0U||pi_u32(main+9800U)!=0U||
       !pi_zero(main+9820U,40U))goto fail;
    v->part_size=pi_u64(main+9592U);
    if(!v->part_size||v->part_size>PI_IMAGE_MAX||!pi_magic(&s,"MAGIC-BEGIN-MBRBACKUP"))goto fail;
    for(i=0;i<10U;++i) {
        char marker[32];(void)xx_rt_snprintf(marker,sizeof(marker),"MAGIC-BEGIN-EXT%03u",(unsigned)i);
        if(!pi_magic(&s,marker)||!pi_take(&s,length,sizeof(length),true)||pi_u32(length))goto fail;
    }
    if(!pi_magic(&s,"MAGIC-BEGIN-LOCALHEADER")||!pi_header(&s,local,sizeof(local)))goto fail;
    if(pi_u64(local)>UINT32_MAX||pi_u64(local)<512U||
       (pi_u64(local)&(pi_u64(local)-1U))||pi_u64(local)>65536U)goto fail;
    v->block_size=(uint32_t)pi_u64(local);
    v->used=pi_u64(local+8U);v->blocks=pi_u64(local+16U);v->bitmap_size=pi_u64(local+24U);
    if(!v->blocks||v->blocks>UINT64_MAX/v->block_size||
       v->blocks*v->block_size!=v->part_size||v->used>v->blocks||
       pi_u64(local+32U)!=0U||v->blocks>UINT64_MAX-7U)goto fail;
    bitmap_min=(v->blocks+7U)/8U;
    if(v->bitmap_size<bitmap_min||v->bitmap_size>bitmap_min+16U||
       v->bitmap_size>PI_BITMAP_MAX||!pi_magic(&s,"MAGIC-BEGIN-BITMAP"))goto fail;
    v->bitmap=(uint8_t *)xx_mem_alloc((size_t)v->bitmap_size);
    if(!v->bitmap||!pi_take(&s,v->bitmap,(size_t)v->bitmap_size,true))goto fail;
    if(!pi_zero(v->bitmap+(size_t)bitmap_min,(size_t)(v->bitmap_size-bitmap_min)))goto fail;
    if((v->blocks&7U)&&((v->bitmap[bitmap_min-1U]>>(unsigned)(v->blocks&7U))!=0U))goto fail;
    for(i=0;i<v->blocks;++i){if(pi_stop(pd))goto fail;if(pi_bit(v,i))++count;}
    if(count!=v->used||!pi_magic(&s,"MAGIC-BEGIN-INFO")||
       !pi_header(&s,info,sizeof(info))||!pi_magic(&s,"MAGIC-BEGIN-DATABLOCKS"))goto fail;
    v->data_offset=s.pos;v->prefix_sum=s.sum;
    if(!pi_data(&s,NULL)||!pi_tail(&s))goto fail;
    return v;
fail:pi_release(v);return NULL;
}
static void pi_destroy_format(Abstractformat *f) {xx_partimage_destroy((xx_partimage *)f);}
void xx_partimage_init(xx_partimage *v,xx_io_device *d,int64_t offset) {
    if(!v) {return; } xx_mem_zero(v,sizeof(*v));xx_format_init(&v->format,d,offset);
    v->format.file_type=PI_TYPE;v->format.format_type=XX_TYPE_ARCHIVE;v->format.is_archive=true;
    xx_format_set_mime_type(&v->format,"application/x-partimage");
    xx_format_set_extension(&v->format,"partimg");
    v->format.check_is_valid=xx_partimage_check_is_valid;
    v->format.handle_base_info=xx_partimage_handle_base_info;
    v->format.get_format_size=xx_partimage_get_format_size;
    v->format.get_number_of_archive_records=xx_partimage_get_number_of_archive_records;
    v->format.create_archive_records_reading=xx_partimage_create_archive_records_reading;
    v->format.get_current_archive_record=xx_partimage_get_current_archive_record;
    v->format.archive_record_move_to_next=xx_partimage_archive_record_move_to_next;
    v->format.unpack_current_archive_record=xx_partimage_unpack_current_archive_record;
    v->format.free_archive_records_reading=xx_partimage_free_archive_records_reading;
    v->format.destroy=pi_destroy_format;
}
xx_partimage *xx_partimage_create(xx_io_device *d,int64_t offset) {
    xx_partimage *v=(xx_partimage *)xx_mem_alloc(sizeof(*v));if(v)xx_partimage_init(v,d,offset);return v;
}
void xx_partimage_destroy(xx_partimage *v) {
    if(!v) {return; } pi_release((pi_view *)v->internal);v->internal=NULL;
    xx_format_cleanup_extra_parameters(&v->format);
}
void xx_partimage_free(xx_partimage *v){if(v){xx_partimage_destroy(v);xx_mem_free(v);}}
bool xx_partimage_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {
    pi_view *v=pi_parse(f,pd);if(!v)return false;pi_release(v);return true;
}
bool xx_partimage_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {
    xx_partimage *p=(xx_partimage *)f;pi_view *v;
    if(!f||pi_stop(pd))return false;
    if(f->base_info_handled&&p->internal)return f->is_valid;
    v=pi_parse(f,pd);if(!v){f->base_info_handled=false;f->is_valid=false;return false;}
    pi_release((pi_view *)p->internal);p->internal=v;p->partition_size=v->part_size;
    p->used_blocks=v->used;p->block_size=v->block_size;
    f->number_of_archive_records=1U;f->format_size=(int64_t)v->size;
    f->overlay_offset=-1;f->overlay_size=0;f->is_valid=true;f->base_info_handled=true;return true;
}
int64_t xx_partimage_get_format_size(Abstractformat *f,xx_pd_struct *pd){return xx_partimage_handle_base_info(f,pd)?f->format_size:-1;}
uint64_t xx_partimage_get_number_of_archive_records(Abstractformat *f,xx_pd_struct *pd){return xx_partimage_handle_base_info(f,pd)?1U:0U;}
static bool pi_record(xx_archive_record *r,const pi_view *v){
    xx_archive_record_cleanup(r);xx_archive_record_init(r);
    r->header_offset=v->base;r->header_size=PI_VOLUME;r->data_offset=v->base+(int64_t)v->data_offset;
    r->compressed_size=(int64_t)v->size;
    return xx_archive_record_set_original_name(r,"partition.img")&&
        xx_archive_record_set_meta_u64(r,XX_META_ID_UNCOMPRESSED_SIZE,v->part_size)&&
        xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSED_SIZE,v->size)&&
        xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSION_METHOD,0U)&&
        xx_archive_record_set_meta_bool(r,XX_META_ID_IS_FOLDER,false);
}
static void pi_cursor_free(void *ptr){pi_cursor *c=(pi_cursor *)ptr;if(c){pi_release(c->view);xx_mem_free(c);}}
xx_archive_record_state *xx_partimage_create_archive_records_reading(Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd){
    xx_archive_record_state *s;pi_cursor *c;pi_view *v;size_t i;
    if(!xx_partimage_handle_base_info(f,pd)) {return NULL; } v=(pi_view *)((xx_partimage *)f)->internal;
    s=(xx_archive_record_state *)xx_mem_alloc(sizeof(*s));c=(pi_cursor *)xx_mem_calloc(1U,sizeof(*c));
    if(!s||!c){xx_mem_free(s);xx_mem_free(c);return NULL;}
    ++v->refs;c->view=v;xx_archive_record_state_init(s,f);s->internal_state=c;s->free_internal=pi_cursor_free;
    s->total_records=1;
    if(options)for(i=0;i<options->count;++i){
        const xx_meta *m=(const xx_meta *)xx_list_at(options,i);xx_meta copy;
        if(!m) {continue; } xx_meta_init(&copy,m->meta_id);
        if(!xx_var_copy(&copy.var,&m->var)||!xx_list_append(&s->options,&copy)){
            xx_meta_cleanup(&copy);xx_archive_record_state_free(s);return NULL;
        }
    }
    if(!pi_record(&s->current_record,v)){xx_archive_record_state_free(s);return NULL;}
    s->has_record=true;s->current_index=0;return s;
}
const xx_archive_record *xx_partimage_get_current_archive_record(Abstractformat *f,xx_archive_record_state *s){
    return f&&s&&s->format==f&&s->has_record?&s->current_record:NULL;
}
bool xx_partimage_archive_record_move_to_next(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd){
    if(!f||!s||s->format!=f||!s->has_record||pi_stop(pd))return false;
    s->has_record=false;xx_archive_record_cleanup(&s->current_record);xx_archive_record_init(&s->current_record);return false;
}
static bool pi_limits(Abstractformat *f,const xx_archive_record_state *s,const pi_view *v){
    const xx_var *max=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    const xx_var *mem=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t required=sizeof(*v)+v->bitmap_size+sizeof(pi_cursor)+PI_COPY;
    return (!max||v->part_size<=xx_var_get_u64(max))&&(!mem||required<=xx_var_get_u64(mem));
}
bool xx_partimage_extract_record_to_device(Abstractformat *f,xx_archive_record_state *s,xx_io_device *dst,xx_pd_struct *pd){
    pi_cursor *c;pi_stream stream;
    if(!f||!f->device||dst==f->device||!s||s->format!=f||!s->has_record||pi_stop(pd)||
       !(c=(pi_cursor *)s->internal_state)||!pi_limits(f,s,c->view))return false;
    memset(&stream,0,sizeof(stream));stream.device=f->device;stream.view=c->view;
    stream.pd=pd;stream.pos=c->view->data_offset;stream.sum=c->view->prefix_sum;
    return pi_data(&stream,dst)&&pi_tail(&stream);
}
static char pi_fold(char c){return c>='A'&&c<='Z'?(char)(c+32):c;}
static bool pi_equal(const char *a,const char *b){while(*a&&pi_fold(*a)==pi_fold(*b)){++a;++b;}return *a==*b;}
static xx_io_device *pi_stage(const char *dest,char **path){
    size_t i,parent=0;unsigned attempt;char *dir=xx_str_dup(dest);*path=NULL;if(!dir)return NULL;
    for(i=0;dir[i];++i) {if(dir[i]=='/'||dir[i]=='\\')parent=i+1U; } dir[parent]=0;
    for(attempt=0;attempt<128U;++attempt){
        char suffix[48];char *candidate;xx_io_device *d;
        (void)xx_rt_snprintf(suffix,sizeof(suffix),".xx_partimage.tmp.%u",attempt);
        candidate=xx_str_concat(dir,suffix);if(!candidate)break;
        if(pi_equal(candidate,dest)){xx_str_free(candidate);continue;}
        d=xx_io_file_open(candidate,"wbx");if(d){*path=candidate;xx_str_free(dir);return d;}
        xx_str_free(candidate);
    }xx_str_free(dir);return NULL;
}
bool xx_partimage_unpack_current_archive_record(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd){
    pi_cursor *c;const xx_var *option,*ov;const char *base=NULL;char *owned=NULL,*path=NULL,*stage=NULL;
    bool ok=false,overwrite;
    if(!f||!s||s->format!=f||!s->has_record||pi_stop(pd)||
       !(c=(pi_cursor *)s->internal_state)||!pi_limits(f,s,c->view))return false;
    option=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_UNPACK_PATH);
    ov=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_OVERWRITE);
    overwrite=ov&&xx_var_get_bool(ov);
    if(!option)return xx_partimage_extract_record_to_device(f,s,NULL,pd);
    if(option->type==XX_VAR_TYPE_STRING||option->type==XX_VAR_TYPE_STRING_VIEW)base=xx_var_get_str(option);
    else if(option->type==XX_VAR_TYPE_WSTRING||option->type==XX_VAR_TYPE_WSTRING_VIEW){
        owned=xx_str_unicode_to_utf8(xx_var_get_wstr(option));base=owned;
    }
    if(!base)goto done;
    path=*base&&base[strlen(base)-1U]!='/'&&base[strlen(base)-1U]!='\\'?
        xx_str_concat3(base,"/","partition.img"):xx_str_concat(base,"partition.img");
    if(!path||(!overwrite&&xx_io_file_exists_a(path))||
       !xx_store_create_dirs_a(path,false)||pi_stop(pd))goto done;
    {xx_io_device *out=pi_stage(path,&stage);if(!out)goto done;
        ok=xx_partimage_extract_record_to_device(f,s,out,pd);if(xx_io_close(out)!=0)ok=false;}
    if(ok&&!pi_stop(pd))ok=xx_io_file_replace_a(stage,path,overwrite);else ok=false;
done:if(stage){if(!ok)(void)xx_io_file_remove_a(stage);xx_str_free(stage);}
    xx_str_free(path);xx_str_free(owned);return ok;
}
void xx_partimage_free_archive_records_reading(Abstractformat *f,xx_archive_record_state *s){(void)f;xx_archive_record_state_free(s);}
