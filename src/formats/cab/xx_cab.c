/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/cab/xx_cab.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzx/xx_lzx.h"
#include "xxfclib/algo/quantum/xx_quantum.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CAB_FLAG_PREV 1U
#define CAB_FLAG_NEXT 2U
#define CAB_FLAG_RESERVE 4U
#define CAB_METHOD_STORE 0U
#define CAB_METHOD_MSZIP 1U
#define CAB_METHOD_QUANTUM 2U
#define CAB_METHOD_LZX 3U

typedef struct cab_folder_s { uint32_t data_offset; uint16_t blocks, type; } cab_folder;
typedef struct cab_file_s { uint32_t size, folder_offset; uint16_t folder, date, time, attrs; char *name; } cab_file;
typedef struct cab_context_s {
    cab_folder *folders; size_t folder_count;
    cab_file *files; size_t file_count, index;
    uint8_t folder_reserve, data_reserve;
    int64_t cabinet_size;
} cab_context;

static uint16_t cab_u16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8U));}
static uint32_t cab_u32(const uint8_t *p){return (uint32_t)cab_u16(p)|((uint32_t)cab_u16(p+2)<<16U);}
static bool cab_read(xx_io_device*d,int64_t o,void*p,size_t n){size_t x=0;if(!d||o<0||o>LONG_MAX||xx_io_seek(d,(long)o,SEEK_SET))return false;while(x<n){ssize_t r=xx_io_read(d,(uint8_t*)p+x,n-x);if(r<=0||(size_t)r>n-x)return false;x+=(size_t)r;}return true;}
static void cab_free(void *p){cab_context*c=(cab_context*)p;size_t i;if(!c)return;for(i=0;i<c->file_count;i++)if(c->files[i].name)xx_str_free(c->files[i].name);if(c->files)xx_mem_free(c->files);if(c->folders)xx_mem_free(c->folders);xx_mem_free(c);}
static bool cab_cstring(Abstractformat*f,int64_t end,int64_t *at,char **value){size_t n=0;uint8_t b;if(!at||*at<0)return false;while(*at+(int64_t)n<end){if(!cab_read(f->device,*at+(int64_t)n,&b,1))return false;if(!b)break;if(b<0x20U)return false;n++;if(n>32767U)return false;}if(*at+(int64_t)n>=end)return false;if(value){*value=(char*)xx_mem_alloc(n+1U);if(!*value)return false;if(n&&!cab_read(f->device,*at,*value,n)){xx_mem_free(*value);*value=NULL;return false;}(*value)[n]=0;}*at+=(int64_t)n+1;return true;}

static bool cab_parse(Abstractformat*f,cab_context **out){
    uint8_t h[36];uint32_t cabinet,files_at;uint16_t flags,nfolders,nfiles;int64_t at,end,total;cab_context*c;size_t i;
    if(!f||!f->device||!out||f->base_address<0)return false;total=xx_io_total_size(f->device);if(total-f->base_address<36||!cab_read(f->device,f->base_address,h,36)||xx_rt_memcmp(h,"MSCF",4))return false;
    cabinet=cab_u32(h+8);files_at=cab_u32(h+16);nfolders=cab_u16(h+26);nfiles=cab_u16(h+28);flags=cab_u16(h+30);if(cabinet<36U||cabinet>(uint64_t)(total-f->base_address)||!nfolders||!nfiles||files_at>=cabinet)return false;end=f->base_address+cabinet;at=f->base_address+36;
    c=(cab_context*)xx_mem_calloc(1,sizeof(*c));if(!c)return false;c->cabinet_size=cabinet;c->folder_count=nfolders;c->file_count=nfiles;c->folders=(cab_folder*)xx_mem_calloc(nfolders,sizeof(*c->folders));c->files=(cab_file*)xx_mem_calloc(nfiles,sizeof(*c->files));if(!c->folders||!c->files)goto fail;
    if(flags&CAB_FLAG_RESERVE){uint8_t r[4];uint16_t hr;if(!cab_read(f->device,at,r,4))goto fail;hr=cab_u16(r);c->folder_reserve=r[2];c->data_reserve=r[3];at+=4;if(at>end||hr>(uint64_t)(end-at))goto fail;at+=hr;}
    if(flags&CAB_FLAG_PREV){if(!cab_cstring(f,end,&at,NULL)||!cab_cstring(f,end,&at,NULL))goto fail;}
    if(flags&CAB_FLAG_NEXT){if(!cab_cstring(f,end,&at,NULL)||!cab_cstring(f,end,&at,NULL))goto fail;}
    for(i=0;i<nfolders;i++){uint8_t b[8];if(at>end||8U+c->folder_reserve>(uint64_t)(end-at)||!cab_read(f->device,at,b,8))goto fail;c->folders[i].data_offset=cab_u32(b);c->folders[i].blocks=cab_u16(b+4);c->folders[i].type=cab_u16(b+6);if(c->folders[i].data_offset>=cabinet||!c->folders[i].blocks)goto fail;at+=8+c->folder_reserve;}
    at=f->base_address+files_at;
    for(i=0;i<nfiles;i++){uint8_t b[16];char*name=NULL;if(at>end||end-at<16||!cab_read(f->device,at,b,16))goto fail;c->files[i].size=cab_u32(b);c->files[i].folder_offset=cab_u32(b+4);c->files[i].folder=cab_u16(b+8);c->files[i].date=cab_u16(b+10);c->files[i].time=cab_u16(b+12);c->files[i].attrs=cab_u16(b+14);at+=16;if(c->files[i].folder>=nfolders||!cab_cstring(f,end,&at,&name))goto fail;c->files[i].name=name;}
    for(i=0;i<nfolders;i++){int64_t p=f->base_address+c->folders[i].data_offset;uint16_t j;uint64_t unpacked=0;for(j=0;j<c->folders[i].blocks;j++){uint8_t b[8];uint16_t packed,plain;if(p>end||8U+c->data_reserve>(uint64_t)(end-p)||!cab_read(f->device,p,b,8))goto fail;packed=cab_u16(b+4);plain=cab_u16(b+6);p+=8+c->data_reserve;if(p>end||packed>(uint64_t)(end-p)||!plain)goto fail;p+=packed;unpacked+=plain;if(unpacked>UINT32_MAX)goto fail;}}
    *out=c;return true;fail:cab_free(c);return false;
}

static bool cab_opts(xx_list_s*d,const xx_list_s*s){size_t i;if(!s)return true;for(i=0;i<s->count;i++){const xx_meta*m=(const xx_meta*)xx_list_at((const xx_list_t*)s,i);xx_meta c;if(!m)continue;xx_meta_init(&c,m->meta_id);if(!xx_var_copy(&c.var,&m->var)||!xx_list_append(d,&c)){xx_meta_cleanup(&c);return false;}}return true;}
static const xx_var*cab_opt(const xx_list_s*l,uint32_t id){size_t i;if(!l)return NULL;for(i=0;i<l->count;i++){const xx_meta*m=(const xx_meta*)xx_list_at((const xx_list_t*)l,i);if(m&&m->meta_id==id)return &m->var;}return NULL;}
static bool cab_safe(const char*n){const char*s=n,*p;if(!n||!n[0]||n[0]=='/'||n[0]=='\\'||n[1]==':')return false;for(p=n;;p++){unsigned char ch=(unsigned char)*p;if(ch==':'||ch=='<'||ch=='>'||ch=='"'||ch=='|'||ch=='?'||ch=='*'||(ch&&ch<32))return false;if(ch=='/'||ch=='\\'||!ch){size_t z=(size_t)(p-s);if(!z||(z==1&&s[0]=='.')||(z==2&&s[0]=='.'&&s[1]=='.'))return false;if(!ch)return true;s=p+1;}}}
static bool cab_record(xx_archive_record*r,const cab_context*c,size_t i){const cab_file*x=&c->files[i];uint16_t method=c->folders[x->folder].type&15U;xx_archive_record_cleanup(r);xx_archive_record_init(r);r->data_offset=-1;r->compressed_size=-1;return xx_archive_record_set_original_name(r,x->name)&&xx_archive_record_set_meta_u64(r,XX_META_ID_UNCOMPRESSED_SIZE,x->size)&&xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSION_METHOD,method)&&xx_archive_record_set_meta_u64(r,XX_META_ID_ATTRIBUTES,x->attrs)&&xx_archive_record_set_meta_u64(r,XX_META_ID_TIMESTAMP,((uint32_t)x->date<<16U)|x->time)&&xx_archive_record_set_meta_bool(r,XX_META_ID_IS_FOLDER,false)&&xx_archive_record_set_meta_bool(r,XX_META_ID_IS_ENCRYPTED,false);}

static bool cab_decode_folder_advanced(Abstractformat*f,const cab_context*c,
                                       const cab_folder*folder,uint16_t method,
                                       uint8_t **out,size_t *out_size){
    uint8_t **blocks=NULL,*result=NULL;
    size_t *packed_sizes=NULL,*plain_sizes=NULL,total=0,wrote=0,i;
    int64_t p=f->base_address+folder->data_offset;
    bool ok=false;
    unsigned window_bits=(folder->type>>8U)&31U;
    blocks=(uint8_t**)xx_mem_calloc(folder->blocks,sizeof(*blocks));
    packed_sizes=(size_t*)xx_mem_calloc(folder->blocks,sizeof(*packed_sizes));
    plain_sizes=(size_t*)xx_mem_calloc(folder->blocks,sizeof(*plain_sizes));
    if(!blocks||!packed_sizes||!plain_sizes)goto done;
    for(i=0;i<folder->blocks;i++){
        uint8_t h[8];uint16_t packed,plain;
        if(!cab_read(f->device,p,h,8))goto done;
        packed=cab_u16(h+4);plain=cab_u16(h+6);p+=8+c->data_reserve;
        if(!packed||!plain||plain>SIZE_MAX-total)goto done;
        blocks[i]=(uint8_t*)xx_mem_alloc(packed);
        if(!blocks[i]||!cab_read(f->device,p,blocks[i],packed))goto done;
        p+=packed;packed_sizes[i]=packed;plain_sizes[i]=plain;total+=plain;
    }
    result=(uint8_t*)xx_mem_alloc(total);
    if(!result)goto done;
    if(method==CAB_METHOD_LZX)
        ok=xx_lzx_cab_decode((const uint8_t*const*)blocks,packed_sizes,
                             plain_sizes,folder->blocks,window_bits,
                             result,total,&wrote);
    else
        ok=xx_quantum_cab_decode((const uint8_t*const*)blocks,packed_sizes,
                                 plain_sizes,folder->blocks,window_bits,
                                 result,total,&wrote);
    if(!ok||wrote!=total){ok=false;goto done;}
    *out=result;*out_size=total;result=NULL;
done:
    if(result)xx_mem_free(result);
    if(blocks){for(i=0;i<folder->blocks;i++)if(blocks[i])xx_mem_free(blocks[i]);xx_mem_free(blocks);}
    if(packed_sizes)xx_mem_free(packed_sizes);
    if(plain_sizes)xx_mem_free(plain_sizes);
    return ok;
}

static bool cab_decode_folder(Abstractformat*f,const cab_context*c,uint16_t fi,uint8_t **out,size_t *out_size){
    const cab_folder*folder=&c->folders[fi];uint16_t method=folder->type&15U,j;
    int64_t p=f->base_address+folder->data_offset;size_t cap=0,used=0;uint8_t*result=NULL;
    if(method==CAB_METHOD_LZX||method==CAB_METHOD_QUANTUM)
        return cab_decode_folder_advanced(f,c,folder,method,out,out_size);
    if(method!=CAB_METHOD_STORE&&method!=CAB_METHOD_MSZIP)return false;
    for(j=0;j<folder->blocks;j++){
        uint8_t h[8];uint16_t packed,plain;uint8_t*in=NULL,*grown;size_t wrote=0;
        if(!cab_read(f->device,p,h,8))goto fail;packed=cab_u16(h+4);plain=cab_u16(h+6);p+=8+c->data_reserve;
        if(!packed||!plain)goto fail;in=(uint8_t*)xx_mem_alloc(packed);
        if(!in||!cab_read(f->device,p,in,packed)){if(in)xx_mem_free(in);goto fail;}p+=packed;
        if(used>SIZE_MAX-plain){xx_mem_free(in);goto fail;}
        if(used+plain>cap){grown=(uint8_t*)xx_mem_realloc(result,used+plain);if(!grown){xx_mem_free(in);goto fail;}result=grown;cap=used+plain;}
        if(method==CAB_METHOD_STORE){if(packed!=plain){xx_mem_free(in);goto fail;}xx_mem_copy(result+used,in,plain);wrote=plain;}
        else if(packed<2||in[0]!='C'||in[1]!='K'||!xx_deflate_decompress_memory(in+2,packed-2,result+used,plain,&wrote,false)||wrote!=plain){xx_mem_free(in);goto fail;}
        xx_mem_free(in);used+=plain;
    }
    *out=result;*out_size=used;return true;
fail:if(result)xx_mem_free(result);return false;
}

void xx_cab_init(xx_cab*a,xx_io_device*d,int64_t b){if(!a)return;xx_mem_zero(a,sizeof(*a));xx_format_init(&a->format,d,b);a->format.file_type=XX_FILE_TYPE_CAB;a->format.format_type=XX_TYPE_ARCHIVE;a->format.is_archive=true;xx_format_set_extension(&a->format,"cab");a->format.check_is_valid=xx_cab_check_is_valid;a->format.handle_base_info=xx_cab_handle_base_info;a->format.get_format_size=xx_cab_get_format_size;a->format.get_number_of_archive_records=xx_cab_get_number_of_archive_records;a->format.create_archive_records_reading=xx_cab_create_archive_records_reading;a->format.get_current_archive_record=xx_cab_get_current_archive_record;a->format.unpack_current_archive_record=xx_cab_unpack_current_archive_record;a->format.archive_record_move_to_next=xx_cab_archive_record_move_to_next;a->format.free_archive_records_reading=xx_cab_free_archive_records_reading;a->archive_end=-1;}
xx_cab*xx_cab_create(xx_io_device*d,int64_t b){xx_cab*a=(xx_cab*)xx_mem_alloc(sizeof(*a));if(a)xx_cab_init(a,d,b);return a;}void xx_cab_destroy(xx_cab*a){if(a)xx_format_cleanup_extra_parameters(&a->format);}void xx_cab_free(xx_cab*a){if(a){xx_cab_destroy(a);xx_mem_free(a);}}
bool xx_cab_check_is_valid(Abstractformat*f,xx_pd_struct*p){cab_context*c;(void)p;if(!cab_parse(f,&c))return false;cab_free(c);return true;}
bool xx_cab_handle_base_info(Abstractformat*f,xx_pd_struct*p){cab_context*c;xx_cab*a;(void)p;if(!f||!cab_parse(f,&c))return false;a=(xx_cab*)f;a->number_of_records=c->file_count;a->archive_end=f->base_address+c->cabinet_size;f->number_of_archive_records=c->file_count;f->format_size=c->cabinet_size;f->is_valid=true;f->base_info_handled=true;cab_free(c);return true;}
int64_t xx_cab_get_format_size(Abstractformat*f,xx_pd_struct*p){return f&&(f->base_info_handled||xx_cab_handle_base_info(f,p))?f->format_size:-1;}
uint64_t xx_cab_get_number_of_archive_records(Abstractformat*f,xx_pd_struct*p){return f&&(f->base_info_handled||xx_cab_handle_base_info(f,p))?((xx_cab*)f)->number_of_records:0;}
xx_archive_record_state*xx_cab_create_archive_records_reading(Abstractformat*f,const xx_list_s*o,xx_pd_struct*p){cab_context*c;xx_archive_record_state*s;(void)p;if(!cab_parse(f,&c))return NULL;s=(xx_archive_record_state*)xx_mem_alloc(sizeof(*s));if(!s){cab_free(c);return NULL;}xx_archive_record_state_init(s,f);s->internal_state=c;s->free_internal=cab_free;s->total_records=c->file_count;if(!cab_opts(&s->options,o)||(c->file_count&&!cab_record(&s->current_record,c,0))){xx_archive_record_state_free(s);return NULL;}s->has_record=c->file_count!=0;return s;}
const xx_archive_record*xx_cab_get_current_archive_record(Abstractformat*f,xx_archive_record_state*s){return f&&s&&s->format==f&&s->has_record?&s->current_record:NULL;}
bool xx_cab_archive_record_move_to_next(Abstractformat*f,xx_archive_record_state*s,xx_pd_struct*p){cab_context*c;(void)p;if(!f||!s||s->format!=f||!(c=(cab_context*)s->internal_state)||++c->index>=c->file_count){if(s)s->has_record=false;return false;}s->current_index++;s->has_record=cab_record(&s->current_record,c,c->index);return s->has_record;}
bool xx_cab_unpack_current_archive_record(Abstractformat*f,xx_archive_record_state*s,xx_pd_struct*p){cab_context*c;cab_file*x;const xx_var*o;uint8_t*folder=NULL;size_t folder_size=0;const char*base=NULL;char*owned=NULL,*path=NULL;bool ok=false;if(!f||!s||s->format!=f||!s->has_record||!(c=(cab_context*)s->internal_state)||c->index>=c->file_count||(p&&xx_pd_is_stopped(p)))return false;x=&c->files[c->index];if(!cab_safe(x->name)||!cab_decode_folder(f,c,x->folder,&folder,&folder_size)||x->folder_offset>folder_size||x->size>folder_size-x->folder_offset)goto done;o=cab_opt(&s->options,XX_META_ID_OPT_UNPACK_PATH);if(!o){ok=true;goto done;}if(o->type==XX_VAR_TYPE_STRING||o->type==XX_VAR_TYPE_STRING_VIEW)base=xx_var_get_str(o);else if(o->type==XX_VAR_TYPE_WSTRING||o->type==XX_VAR_TYPE_WSTRING_VIEW){owned=xx_str_unicode_to_utf8(xx_var_get_wstr(o));base=owned;}if(!base)goto done;path=(base[0]&&base[xx_str_len(base)-1]!='/'&&base[xx_str_len(base)-1]!='\\')?xx_str_concat3(base,"/",x->name):xx_str_concat(base,x->name);if(!path||!xx_store_create_dirs_a(path,false))goto done;{xx_io_device*d=xx_io_file_open(path,"wb");size_t at=0;if(!d)goto done;ok=true;while(at<x->size){ssize_t n=xx_io_write(d,folder+x->folder_offset+at,x->size-at);if(n<=0||(size_t)n>x->size-at){ok=false;break;}at+=(size_t)n;}xx_io_close(d);}done:if(!ok&&path)xx_rt_remove(path);if(folder)xx_mem_free(folder);if(path)xx_str_free(path);if(owned)xx_str_free(owned);return ok;}
void xx_cab_free_archive_records_reading(Abstractformat*f,xx_archive_record_state*s){(void)f;xx_archive_record_state_free(s);}
