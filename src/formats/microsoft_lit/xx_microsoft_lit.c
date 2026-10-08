/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Native reader with bounded RAM pipe transport. */
/* Request POSIX before any header; a strict -std=c11 hides it otherwise. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#include "xxfclib/formats/microsoft_lit/xx_microsoft_lit.h"
#include "../ue2_indexed.h"
#include "xxfclib/data/xx_data.h"
typedef struct ac_blob { uint8_t *p;uint32_t n;uint64_t used,limit;xx_pd_struct *pd; } ac_blob;
static bool ac_error(ac_blob *b,const char *why) { xx_pd_set_error(b->pd,1,why);return false; }
#include "../xx_archive_codec_pipe.h"
#define LIT_TYPE XX_FILE_TYPE_MICROSOFT_LIT
#define LIT_MAX_BLOB (64U*1024U*1024U)
#define LIT_DEFAULT_MEMORY (UINT64_C(256)*1024*1024)

static bool lit_valid(Abstractformat *f,xx_pd_struct *pd) {
    uint8_t h[40],table[80];uint32_t header,secondary,i;uint64_t start,total;
    int64_t size=f&&f->device?xx_io_total_size(f->device):-1;
    if(pd&&xx_pd_is_stopped(pd))return false;
    if(!f||f->base_address<0||!ue2_read(f,f->base_address,h,40)||xx_rt_memcmp(h,"ITOLITLS",8)||xx_data_get_u32(h+8, 4, 0, false)!=1||xx_data_get_u32(h+16, 4, 0, false)!=5)return false;
    header=xx_data_get_u32(h+12, 4, 0, false);secondary=xx_data_get_u32(h+20, 4, 0, false);
    if(header<40||header>65536||secondary<232||secondary>65536||!ue2_range(size,f->base_address+(int64_t)header,80+(int64_t)secondary))return false;
    if(!ue2_read(f,f->base_address+header,table,80))return false;
    start=(uint64_t)header+80+secondary;total=(uint64_t)(size-f->base_address);
    for(i=0;i<5;++i){uint64_t at=xx_data_get_u64(table+i*16, 8, 0, false),n=xx_data_get_u64(table+i*16+8, 8, 0, false);
        if(at<start||at>total||n>total-at||at>UINT32_MAX||n>UINT32_MAX)return false;
        if((i==0&&n!=24)||((i==1||i==2)&&n<32)||((i==3||i==4)&&n!=16))return false;}
    return true;
}
static bool lit_info(Abstractformat *f,xx_pd_struct *pd) {
    if(!lit_valid(f,pd)) {return false; } f->format_size=xx_io_total_size(f->device)-f->base_address;
    f->is_valid=true;f->base_info_handled=true;return true;
}
static const xx_archive_record *lit_current(Abstractformat *f,xx_archive_record_state *state) {
    const xx_archive_record *r=ue2_current(f,state);
    if(r&&!xx_archive_record_set_meta_bool(&state->current_record,XX_META_ID_IS_ENCRYPTED,((xx_microsoft_lit *)f)->drm_level!=0))return NULL;
    return r;
}
static char *lit_helper_path(void) {
#ifdef _WIN32
    wchar_t path[32768];DWORD n=GetModuleFileNameW(NULL,path,32768);
    if(!n||n>=32768) {return NULL; } while(n&&path[n-1]!=L'/'&&path[n-1]!=L'\\')--n;
    if(!n||n+32>=32768) {return NULL; } xx_rt_memcpy(path+n,L"xfu_convertlit_helper.exe",sizeof(L"xfu_convertlit_helper.exe"));return xx_str_unicode_to_utf8(path);
#else
    char path[4096];ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-1);
    if(n<=0||n>=(ssize_t)sizeof(path)-32) {return NULL; } while(n&&path[n-1]!='/')--n;
    if(!n) {return NULL; } xx_rt_memcpy(path+n,"xfu_convertlit_helper",sizeof("xfu_convertlit_helper"));return xx_str_dup(path);
#endif
}
static bool lit_same_name(const char *a,const char *b) {
    while(*a&&*b){unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;if(xx_rt_ascii_tolower(x)!=xx_rt_ascii_tolower(y))return false;}return *a==*b;
}
static bool lit_index_blob(xx_microsoft_lit *a,uint8_t *data,size_t size) {
    ue2_index *index=NULL;uint32_t count,drm,i;size_t at=16;
    if(size<16||xx_rt_memcmp(data,"LITP",4)||xx_data_get_u32(data+12, 4, 0, false))return false;
    drm=xx_data_get_u32(data+4, 4, 0, false);count=xx_data_get_u32(data+8, 4, 0, false);
    if((drm!=0&&drm!=1&&drm!=3)||!count||count>65535U)return false;
    index=xx_mem_calloc(1,sizeof(*index));if(!index)return false;
    for(i=0;i<count;++i){uint32_t n;uint64_t bytes;char *name;size_t j;
        if(at>size||size-at<12) {goto failed; } n=xx_data_get_u32(data+at, 4, 0, false);bytes=xx_data_get_u64(data+at+4, 8, 0, false);at+=12;
        if(!n||n>4096||n>size-at||xx_rt_memchr(data+at,0,n))goto failed;
        name=xx_mem_alloc((size_t)n+1);if(!name)goto failed;xx_rt_memcpy(name,data+at,n);name[n]=0;at+=n;
        if(!ue2_safe_name(name)||bytes>size-at){xx_mem_free(name);goto failed;}
        for(j=0;j<index->count;++j)if(lit_same_name(name,index->members[j].name)){xx_mem_free(name);goto failed;}
        if(!ue2_add(index,name,(int64_t)at,(int64_t)bytes,0)){xx_mem_free(name);goto failed;}xx_mem_free(name);at+=(size_t)bytes;
    }
    if(at!=size)goto failed;
    index->size=a->format.format_size;
    ue2_index_free(a->index);xx_mem_free(a->decoded);a->index=index;a->decoded=data;a->decoded_size=size;a->drm_level=drm;
    a->format.number_of_archive_records=count;return true;
failed:
    ue2_index_free(index);return false;
}
static uint64_t lit_memory_option(const xx_list_s *options) {
    size_t i;uint64_t limit=LIT_DEFAULT_MEMORY;
    for(i=0;options&&i<options->count;++i){const xx_meta *m=(const xx_meta *)xx_list_at((const xx_list_t *)options,i);
        if(m&&m->meta_id==XX_META_ID_OPT_MEMORY_LIMIT)limit=xx_var_get_u64(&m->var);}
    return limit>LIT_DEFAULT_MEMORY?LIT_DEFAULT_MEMORY:limit;
}
static bool lit_decode(Abstractformat *f,uint64_t limit,xx_pd_struct *pd) {
    xx_microsoft_lit *a=(xx_microsoft_lit *)f;af_process process;uint8_t h[32],chunk[65536],*output=NULL;
    char *helper=NULL;uint64_t input,total=0,received=0,worker;bool ok=false,announced=false;unsigned rejected=1;int level=-1;
    if(a->index&&a->decode_memory_limit<=limit)return !(pd&&xx_pd_is_stopped(pd));
    if(!f->base_info_handled&&!xx_format_handle_base_info(f,pd))return false;
    input=(uint64_t)f->format_size;worker=limit/2;
    if(input>LIT_MAX_BLOB||worker<65536||input>limit-worker){xx_pd_set_error(pd,1,"LIT input or decoder exceeds archive memory limit");return false;}
    ue2_index_free(a->index);a->index=NULL;xx_mem_free(a->decoded);a->decoded=NULL;a->decoded_size=0;
    f->number_of_archive_records=0;a->decode_memory_limit=0;
    xx_mem_zero(&process,sizeof(process));process.start=af_clock();process.timeout=60000;process.pd=pd;process.status=AF_FORMAT;
#ifndef _WIN32
    process.input=process.output=-1;
#endif
    helper=lit_helper_path();if(!helper||!af_start(&process,helper,worker)){process.status=AF_UNAVAILABLE;goto done;}
    xx_rt_memset(h,0,sizeof(h));xx_rt_memcpy(h,"LTC1",4);xx_data_set_u64(h+8, 8, 0, input, false);xx_data_set_u64(h+24, 8, 0, worker, false);
    if(!af_output(&process,h,32))goto done;
    level=xx_pd_enter_level(pd,input,"Decoding Microsoft Reader book");
    for(;;){unsigned type;if(!af_input(&process,h,4))goto done;type=xx_data_get_u32(h, 4, 0, false);
        if(type==1){uint64_t at;uint32_t n;if(announced||!af_input(&process,h,12))goto done;at=xx_data_get_u64(h, 8, 0, false);n=xx_data_get_u32(h+8, 4, 0, false);
            if(!n||n>65536||at>input||n>input-at||!ue2_read(f,f->base_address+(int64_t)at,chunk,n))goto done;
            xx_data_set_u32(h, 4, 0, n, false);if(!af_output(&process,h,4)||!af_output(&process,chunk,n))goto done;xx_pd_set_current(pd,level,at+n);
        }else if(type==2){if(announced||!af_input(&process,h,8))goto done;total=xx_data_get_u64(h, 8, 0, false);
            if(total<16||total>LIT_MAX_BLOB||total>limit-worker-input) {goto done; } output=xx_mem_alloc((size_t)total);if(!output)goto done;announced=true;
        }else if(type==3){uint32_t n;if(!announced||!af_input(&process,h,4))goto done;n=xx_data_get_u32(h, 4, 0, false);
            if(!n||n>65536||n>total-received||!af_input(&process,output+(size_t)received,n)) {goto done; } received+=n;
        }else if(type==4){if(!af_input(&process,h,12))goto done;rejected=xx_data_get_u32(h, 4, 0, false);
            if(rejected||!announced||xx_data_get_u64(h+4, 8, 0, false)!=total||received!=total||!af_live(&process))goto done;
            ok=lit_index_blob(a,output,(size_t)total);if(ok){output=NULL;a->decode_memory_limit=limit;}break;
        }else goto done;
    }
done:
    if(level>=0) {xx_pd_leave_level(pd,level); } af_close(&process);xx_str_free(helper);xx_mem_free(output);
    if(!ok&&(!pd||!xx_pd_is_stopped(pd)))xx_pd_set_error(pd,1,process.status==AF_UNAVAILABLE?"ConvertLIT decoder helper unavailable":process.status==AF_TIMEOUT?"ConvertLIT decoding timed out":rejected==2?"LIT owner-key DRM5 requires a personal key and is unsupported":rejected==3?"LIT decoder exceeds archive memory limit":"LIT book is damaged, unsupported or exceeds archive memory limit");
    return ok;
}
static uint64_t lit_count(Abstractformat *f,xx_pd_struct *pd) { return ((xx_microsoft_lit *)f)->index||lit_decode(f,LIT_DEFAULT_MEMORY,pd)?f->number_of_archive_records:0; }
static xx_archive_record_state *lit_records(Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd) {
    return lit_decode(f,lit_memory_option(options),pd)?ue2_records(f,options,pd):NULL;
}
static bool lit_unpack(Abstractformat *f,xx_archive_record_state *state,xx_pd_struct *pd) {
    xx_microsoft_lit *a=(xx_microsoft_lit *)f;const xx_archive_record *r=ue2_current(f,state);xx_io_device *memory;
    const xx_var *option=NULL;const char *base=NULL;char *owned=NULL,*path=NULL;bool result=false;size_t i;int level;
    if(!r||!a->decoded||(pd&&xx_pd_is_stopped(pd))||!ue2_range((int64_t)a->decoded_size,r->data_offset,r->compressed_size))return false;
    for(i=0;i<state->options.count;++i){const xx_meta *m=(const xx_meta *)xx_list_at((const xx_list_t *)&state->options,i);if(m&&m->meta_id==XX_META_ID_OPT_UNPACK_PATH){option=&m->var;break;}}
    if(!option){int64_t n=0;level=xx_pd_enter_level(pd,(uint64_t)r->compressed_size,"Testing Microsoft Reader member");
        while(n<r->compressed_size){if(pd&&xx_pd_is_stopped(pd))break;n+=r->compressed_size-n>65536?65536:r->compressed_size-n;xx_pd_set_current(pd,level,(uint64_t)n);}xx_pd_leave_level(pd,level);return !(pd&&xx_pd_is_stopped(pd));}
    if(option->type==XX_VAR_TYPE_STRING||option->type==XX_VAR_TYPE_STRING_VIEW)base=xx_var_get_str(option);
    else if(option->type==XX_VAR_TYPE_WSTRING||option->type==XX_VAR_TYPE_WSTRING_VIEW){owned=xx_str_unicode_to_utf8(xx_var_get_wstr(option));base=owned;}
    if(base&&ue2_safe_name(xx_archive_record_get_original_name(r)))path=xx_str_concat3(base,"/",xx_archive_record_get_original_name(r));
    memory=xx_io_mem_open_ro(a->decoded,a->decoded_size);
    if(path&&memory&&xx_store_create_dirs_a(path,false))result=xx_store_unpack_device_to_file(memory,r->data_offset,r->compressed_size,path,pd);
    if(memory) {xx_io_close(memory); } xx_str_free(path);xx_str_free(owned);return result;
}
static void lit_destroy(Abstractformat *f) { xx_microsoft_lit *a=(xx_microsoft_lit *)f;ue2_destroy_format(f);xx_mem_free(a->decoded);a->decoded=NULL;a->decoded_size=0;a->decode_memory_limit=0; }
void xx_microsoft_lit_init(xx_microsoft_lit *a,xx_io_device *device,int64_t base) {
    if(!a) {return; } xx_mem_zero(a,sizeof(*a));ue2_init_format(&a->format,device,base,LIT_TYPE,"lit","application/x-ms-reader");
    a->format.check_is_valid=lit_valid;a->format.handle_base_info=lit_info;a->format.get_number_of_archive_records=lit_count;
    a->format.get_current_archive_record=lit_current;
    a->format.create_archive_records_reading=lit_records;a->format.unpack_current_archive_record=lit_unpack;a->format.destroy=lit_destroy;
}
xx_microsoft_lit *xx_microsoft_lit_create(xx_io_device *device,int64_t base) { xx_microsoft_lit *a=xx_mem_alloc(sizeof(*a));if(a)xx_microsoft_lit_init(a,device,base);return a; }
void xx_microsoft_lit_destroy(xx_microsoft_lit *a) { if(a)lit_destroy(&a->format); }
void xx_microsoft_lit_free(xx_microsoft_lit *a) { if(a){xx_microsoft_lit_destroy(a);xx_mem_free(a);} }
