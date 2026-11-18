/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_PP20_SLOT_IMAGE_H
#define XX_PP20_SLOT_IMAGE_H
#define PP20_SLOT 6272U
#define PP20_IMAGE_LIMIT (64U*1024U*1024U)
typedef struct pp20_image_item { size_t start,size; xx_pp20_context context; uint32_t index; bool stored; } pp20_image_item;
typedef struct pp20_image { uint8_t *data; pp20_image_item *items; size_t size,count; uint64_t raw; bool library; } pp20_image;
static void pp20_image_free(void *opaque)
{pp20_image *s=(pp20_image*)opaque;if(s){xx_mem_free(s->data);xx_mem_free(s->items);xx_mem_free(s);}}
static bool pp20_image_decode(const pp20_image *s,size_t index,uint8_t **plain,xx_pd_struct *pd)
{
    const pp20_image_item *m=&s->items[index];size_t unused=SIZE_MAX;
    uint8_t *out=(uint8_t*)xx_mem_alloc((size_t)m->context.uncompressed_size);
    if(!out)return false;
    if(m->stored){xx_rt_memcpy(out,s->data+m->start,m->size);*plain=out;return true;}
    if(!xx_pp20_decrunch(s->data+m->start,8,m->size-4,m->context.offset_widths,m->context.skip_bits,out,
                        (size_t)m->context.uncompressed_size,pd,&unused)||unused||xx_pd_is_stopped(pd)) {
        xx_mem_free(out);return false;
    }
    *plain=out;return true;
}
static pp20_image *pp20_slot_open(Abstractformat *f,xx_pd_struct *pd)
{
    pp20_image *s=NULL;int64_t n,saved;size_t at,i,count=0;bool ok=false;
    if(!f||!f->device||f->base_address<0||xx_pd_is_stopped(pd))return NULL;
    saved=xx_io_tell(f->device);n=xx_io_total_size(f->device)-f->base_address;
    if(saved<0||n<(int64_t)(2U*PP20_SLOT)||n>PP20_IMAGE_LIMIT||n%PP20_SLOT)return NULL;
    s=(pp20_image*)xx_mem_calloc(1,sizeof(*s));if(!s)return NULL;
    s->size=(size_t)n;s->data=(uint8_t*)xx_mem_alloc(s->size);
    if(!s->data||!xx_pp20_read_exact_at(f->device,f->base_address,s->data,s->size)||memcmp(s->data,"PP20",4))goto done;
    for(at=0;at<s->size;at+=PP20_SLOT)if(!memcmp(s->data+at,"PP20",4))++count;
    if(count<2||count>4096)goto done;
    s->items=(pp20_image_item*)xx_mem_calloc(count,sizeof(*s->items));if(!s->items)goto done;
    for(at=0;at<s->size;at+=PP20_SLOT)if(!memcmp(s->data+at,"PP20",4))s->items[s->count++].start=at;
    for(i=0;i<s->count;i++) {
        pp20_image_item *m=&s->items[i];size_t limit=i+1<s->count?s->items[i+1].start:s->size,end=limit;uint8_t *plain=NULL;
        if(xx_pd_is_stopped(pd))goto done;
        while(end>m->start&&!s->data[end-1])--end;
        end=(end+3U)&~(size_t)3U;
        if(end>limit||end<m->start+16U)goto done;
        m->size=end-m->start;
        if(!xx_pp20_parse_buffer(s->data+m->start,m->size,&m->context)||m->context.variant!=XX_PP20_VARIANT_PP20||
           s->raw>UINT64_C(256)*1024*1024-m->context.uncompressed_size||!pp20_image_decode(s,i,&plain,pd))goto done;
        xx_mem_free(plain);s->raw+=m->context.uncompressed_size;
    }
    ok=true;
done:
    (void)xx_io_seek64(f->device,saved,SEEK_SET);
    if(!ok){pp20_image_free(s);s=NULL;}return s;
}
#include "xx_pp20_library.h"
static pp20_image *pp20_image_open(Abstractformat *f,xx_pd_struct *pd)
{pp20_image *s=pp20_slot_open(f,pd);return s?s:pp20_library_open(f,pd);}
static void pp20_image_name(const pp20_image *s,const pp20_image_item *m,char *name,size_t size)
{
    if(!s->library)xx_rt_snprintf(name,size,"block-%08x.bin",(unsigned)m->start);
    else xx_rt_snprintf(name,size,m->stored?"%03u_stored.iff":"%03u.bin",(unsigned)m->index);
}
static bool pp20_image_publish(Abstractformat *f,pp20_image *s)
{
    xx_pp20 *a=(xx_pp20*)f;if(!s)return false;
    a->variant=s->library?XX_PP20_VARIANT_LIBRARY:XX_PP20_VARIANT_IMAGE;a->uncompressed_size=s->raw;a->stream_end=f->base_address+(int64_t)s->size;
    a->skip_bits=0;xx_mem_zero(a->offset_widths,sizeof(a->offset_widths));
    f->format_size=(int64_t)s->size;f->number_of_archive_records=s->count;f->overlay_offset=-1;f->overlay_size=0;
    f->is_archive=true;f->is_crypted=false;f->is_executable=false;f->is_valid=true;f->base_info_handled=true;
    pp20_image_free(s);return true;
}
static bool pp20_image_info(Abstractformat *f,xx_pd_struct *pd){return pp20_image_publish(f,pp20_image_open(f,pd));}
static bool pp20_library_info(Abstractformat *f,xx_pd_struct *pd){return pp20_image_publish(f,pp20_library_open(f,pd));}
static bool pp20_image_record(Abstractformat *f,xx_archive_record_state *state)
{
    pp20_image *s=(pp20_image*)state->internal_state;const pp20_image_item *m;xx_archive_record *r=&state->current_record;char name[40];
    if(!s||state->current_index<0||(uint64_t)state->current_index>=s->count)return false;
    m=&s->items[state->current_index];pp20_image_name(s,m,name,sizeof(name));
    xx_archive_record_cleanup(r);xx_archive_record_init(r);r->header_offset=f->base_address+(int64_t)m->start;r->header_size=m->stored?0:8;
    r->data_offset=r->header_offset+r->header_size;r->compressed_size=(int64_t)m->size-r->header_size;
    state->has_record=xx_archive_record_set_meta_str(r,XX_META_ID_ORIGINAL_NAME,name)&&
        xx_archive_record_set_meta_u64(r,XX_META_ID_UNCOMPRESSED_SIZE,m->context.uncompressed_size)&&
        xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSED_SIZE,(uint64_t)r->compressed_size)&&
        xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSION_METHOD,m->stored?0U:1U)&&
        xx_archive_record_set_meta_bool(r,XX_META_ID_IS_FOLDER,false)&&xx_archive_record_set_meta_bool(r,XX_META_ID_IS_ENCRYPTED,false);
    return state->has_record;
}
static xx_archive_record_state *pp20_image_records(Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd)
{
    pp20_image *s=((xx_pp20*)f)->variant==XX_PP20_VARIANT_LIBRARY?pp20_library_open(f,pd):pp20_image_open(f,pd);xx_archive_record_state *state;if(!s)return NULL;
    state=(xx_archive_record_state*)xx_mem_alloc(sizeof(*state));if(!state){pp20_image_free(s);return NULL;}
    xx_archive_record_state_init(state,f);state->internal_state=s;state->free_internal=pp20_image_free;state->current_index=0;state->total_records=s->count;
    if(!xx_pp20_copy_options(&state->options,options)||!pp20_image_record(f,state)){xx_archive_record_state_free(state);return NULL;}
    return state;
}
static bool pp20_image_unpack(Abstractformat *f,xx_archive_record_state *state,xx_pd_struct *pd)
{
    pp20_image *s=(pp20_image*)state->internal_state;const xx_var *option=xx_pp20_find_option(&state->options,XX_META_ID_OPT_UNPACK_PATH);
    const char *base=NULL;char *owned=NULL,*path=NULL,name[40];uint8_t *plain=NULL;xx_io_device *out=NULL;bool ok=false,created=false;
    (void)f;
    if(!s||state->current_index<0||(uint64_t)state->current_index>=s->count||!pp20_image_decode(s,(size_t)state->current_index,&plain,pd))return false;
    if(!option){xx_mem_free(plain);return true;}
    if(option->type==XX_VAR_TYPE_STRING||option->type==XX_VAR_TYPE_STRING_VIEW)base=xx_var_get_str(option);
    else if(option->type==XX_VAR_TYPE_WSTRING||option->type==XX_VAR_TYPE_WSTRING_VIEW){owned=xx_str_unicode_to_utf8(xx_var_get_wstr(option));base=owned;}
    if(!base)goto done;
    pp20_image_name(s,&s->items[state->current_index],name,sizeof(name));path=base[0]?xx_str_concat3(base,"/",name):xx_str_dup(name);
    if(!path||!xx_store_create_dirs_a(path,false))goto done;
    out=xx_io_file_open(path,"wb");created=out!=NULL;
    ok=out&&xx_pp20_write_all(out,plain,(size_t)s->items[state->current_index].context.uncompressed_size,pd);
done:
    if(out&&xx_io_close(out))ok=false;
    if(!ok&&created)xx_rt_remove(path);
    xx_str_free(path);xx_str_free(owned);xx_mem_free(plain);return ok;
}
#endif
