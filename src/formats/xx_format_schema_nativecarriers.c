/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Delegation of authenticated physical SFX payload metadata. The executable
 * stub remains raw; this bridge never assigns file offsets to decoded bytes.
 * Current scope: docs/data_struct_nativecarriers_later_scope.txt and
 * docs/data_struct_nativecarriers_resources_scope.txt.
 * Reader factory references are omitted from focused emulator profiles.
 */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"

#if SX_ALIAS_READER_FACTORY
#include "xxfclib/formats/archive/sfx_ace/xx_sfx_ace.h"
#include "xxfclib/formats/archive/sfx_lha/xx_sfx_lha.h"
#include "xxfclib/formats/archive/sfx_7zip/xx_sfx_7zip.h"
#include "xxfclib/formats/archive/sfx_cab/xx_sfx_cab.h"
#include "xxfclib/formats/archive/sfx_zipcentral/xx_sfx_zipcentral.h"
#include "xxfclib/formats/archive/sfx_gxl/xx_sfx_gxl.h"
#include "xxfclib/formats/archive/sfx_rta/xx_sfx_rta.h"
#include "xxfclib/formats/archive/sfx_sqz/xx_sfx_sqz.h"
#include "xxfclib/formats/archive/sfx_chz/xx_sfx_chz.h"
#include "xxfclib/formats/archive/sfx_swag/xx_sfx_swag.h"
#include "xxfclib/formats/installer/sfx_rtpatch/xx_sfx_rtpatch.h"
#include "xxfclib/formats/installer/rtpatch/xx_rtpatch.h"
#define SC_ID_BASE UINT32_C(0x60120000)
#define SC_MAX_ITEMS 100000U
static xx_file_type_t sc_canonical(xx_file_type_t type)
{
    switch(type){
    case XX_FILE_TYPE_MSKN1:case XX_FILE_TYPE_MSKN2:case XX_FILE_TYPE_MSKN3:return XX_FILE_TYPE_ZLIB;
    case XX_FILE_TYPE_ZBEOS:return XX_FILE_TYPE_GZ;
    case XX_FILE_TYPE_SFX_GZIP:return XX_FILE_TYPE_GZ;
    case XX_FILE_TYPE_SFX_BZIP2:return XX_FILE_TYPE_BZ2;
    case XX_FILE_TYPE_SFX_TAR:return XX_FILE_TYPE_TAR;
    case XX_FILE_TYPE_SFX_CAB:return XX_FILE_TYPE_CAB;
    case XX_FILE_TYPE_SFX_7ZIP:return XX_FILE_TYPE_7ZIP;
    case XX_FILE_TYPE_SFX_ZIPCENTRAL:return XX_FILE_TYPE_ZIP;
    case XX_FILE_TYPE_SFX_ARJ:return XX_FILE_TYPE_ARJ;
    case XX_FILE_TYPE_SFX_ACE:return XX_FILE_TYPE_ACE;
    case XX_FILE_TYPE_SFX_HA:return XX_FILE_TYPE_HA;
    case XX_FILE_TYPE_SFX_ALZ:return XX_FILE_TYPE_ALZ;
    case XX_FILE_TYPE_SFX_ZOO:return XX_FILE_TYPE_ZOO;
    case XX_FILE_TYPE_SFX_CHM:return XX_FILE_TYPE_CHM;
    case XX_FILE_TYPE_SFX_LHA:case XX_FILE_TYPE_GEMDOS_LHA:return XX_FILE_TYPE_LHA;
    case XX_FILE_TYPE_SFX_ARC:return XX_FILE_TYPE_SEAARC;
    case XX_FILE_TYPE_SFX_BSN:return XX_FILE_TYPE_BSN;
    case XX_FILE_TYPE_SFX_ARQ:return XX_FILE_TYPE_ARQ;
    case XX_FILE_TYPE_SFX_GXL:return XX_FILE_TYPE_GXL;
    case XX_FILE_TYPE_SFX_ASYMETRIX:return XX_FILE_TYPE_ASYMETRIX;
    case XX_FILE_TYPE_SFX_RTA:return XX_FILE_TYPE_RTA;
    case XX_FILE_TYPE_SFX_KWAJ:return XX_FILE_TYPE_KWAJ;
    case XX_FILE_TYPE_SFX_SQZ:return XX_FILE_TYPE_SQZ;
    case XX_FILE_TYPE_SFX_CHZ:return XX_FILE_TYPE_CHZ;
    case XX_FILE_TYPE_SFX_SWAG:return XX_FILE_TYPE_SWAG;
    case XX_FILE_TYPE_SFX_ZPAK:return XX_FILE_TYPE_ZPAK;
    case XX_FILE_TYPE_SFX_DISKEXPRESS:return XX_FILE_TYPE_DISKEXPRESS;
    case XX_FILE_TYPE_SFX_MPQ:return XX_FILE_TYPE_MPQ;
    case XX_FILE_TYPE_SFX_SQX:return XX_FILE_TYPE_SQX;
    case XX_FILE_TYPE_SFX_AIN:return XX_FILE_TYPE_AIN;
    case XX_FILE_TYPE_SFX_HAP:return XX_FILE_TYPE_HAP;
    case XX_FILE_TYPE_SFX_LZX:return XX_FILE_TYPE_AMIGALZX;
    case XX_FILE_TYPE_JAVA_JMOD:return XX_FILE_TYPE_ZIP;
    case XX_FILE_TYPE_DIE_MUSIC_MULLER_RENOISE_XRNS:return XX_FILE_TYPE_ZIP;
    case XX_FILE_TYPE_DIE_MUSIC_MULLER_XRNI:return XX_FILE_TYPE_ZIP;
    case XX_FILE_TYPE_DIE_MUSIC_MULLER_RENOISE_XRNT_PK:return XX_FILE_TYPE_ZIP;
    case XX_FILE_TYPE_DIE_MUSIC_SVARTRACKER:return XX_FILE_TYPE_ZIP;

    case XX_FILE_TYPE_SFX_RTPATCH:return XX_FILE_TYPE_RTPATCH;
    default:return XX_FILE_TYPE_UNKNOWN;
    }
}
static bool sx_nativecarriers_supports(xx_file_type_t type)
{
    switch(type){case XX_FILE_TYPE_MSKN1:case XX_FILE_TYPE_MSKN2:case XX_FILE_TYPE_MSKN3:case XX_FILE_TYPE_ZBEOS:case XX_FILE_TYPE_SFX_GZIP:case XX_FILE_TYPE_SFX_BZIP2:case XX_FILE_TYPE_SFX_TAR:case XX_FILE_TYPE_SFX_CAB:case XX_FILE_TYPE_SFX_7ZIP:case XX_FILE_TYPE_SFX_ZIPCENTRAL:
    case XX_FILE_TYPE_SFX_ARJ:case XX_FILE_TYPE_SFX_ACE:case XX_FILE_TYPE_SFX_HA:case XX_FILE_TYPE_SFX_ALZ:case XX_FILE_TYPE_SFX_ZOO:case XX_FILE_TYPE_SFX_CHM:case XX_FILE_TYPE_SFX_LHA:case XX_FILE_TYPE_GEMDOS_LHA:case XX_FILE_TYPE_SFX_ARC:case XX_FILE_TYPE_SFX_BSN:case XX_FILE_TYPE_SFX_ARQ:case XX_FILE_TYPE_SFX_GXL:case XX_FILE_TYPE_SFX_ASYMETRIX:case XX_FILE_TYPE_SFX_RTA:case XX_FILE_TYPE_SFX_KWAJ:case XX_FILE_TYPE_SFX_SQZ:case XX_FILE_TYPE_SFX_CHZ:case XX_FILE_TYPE_SFX_SWAG:case XX_FILE_TYPE_SFX_ZPAK:case XX_FILE_TYPE_SFX_DISKEXPRESS:case XX_FILE_TYPE_SFX_MPQ:case XX_FILE_TYPE_SFX_SQX:case XX_FILE_TYPE_SFX_AIN:case XX_FILE_TYPE_SFX_HAP:case XX_FILE_TYPE_SFX_LZX:case XX_FILE_TYPE_JAVA_JMOD:case XX_FILE_TYPE_SFX_RTPATCH:case XX_FILE_TYPE_DIE_MUSIC_MULLER_RENOISE_XRNS:case XX_FILE_TYPE_DIE_MUSIC_MULLER_XRNI:case XX_FILE_TYPE_DIE_MUSIC_MULLER_RENOISE_XRNT_PK:case XX_FILE_TYPE_DIE_MUSIC_SVARTRACKER:return true;
    default:return false;}
}
static uint32_t sc_native_prefix(xx_file_type_t type)
{
    switch(type){case XX_FILE_TYPE_MSKN1:case XX_FILE_TYPE_MSKN2:case XX_FILE_TYPE_MSKN3:return UINT32_C(0x60070000);
    case XX_FILE_TYPE_SFX_CAB:return UINT32_C(0x60040000);
    case XX_FILE_TYPE_SFX_ARJ:case XX_FILE_TYPE_SFX_ACE:case XX_FILE_TYPE_SFX_HA:case XX_FILE_TYPE_SFX_ALZ:case XX_FILE_TYPE_SFX_ZOO:case XX_FILE_TYPE_SFX_CHM:case XX_FILE_TYPE_SFX_LHA:case XX_FILE_TYPE_GEMDOS_LHA:return UINT32_C(0x60070000);
    case XX_FILE_TYPE_SFX_ARC:    case XX_FILE_TYPE_SFX_BSN:    case XX_FILE_TYPE_SFX_ARQ:    case XX_FILE_TYPE_SFX_GXL:    case XX_FILE_TYPE_SFX_ASYMETRIX:    case XX_FILE_TYPE_SFX_RTA:    case XX_FILE_TYPE_SFX_KWAJ:    case XX_FILE_TYPE_SFX_SQZ:    case XX_FILE_TYPE_SFX_CHZ:    case XX_FILE_TYPE_SFX_SWAG:    case XX_FILE_TYPE_SFX_ZPAK:    case XX_FILE_TYPE_SFX_DISKEXPRESS:return UINT32_C(0x60170000);
    case XX_FILE_TYPE_SFX_MPQ:return UINT32_C(0x601c0000);
    case XX_FILE_TYPE_SFX_SQX:return UINT32_C(0x60170000);
    case XX_FILE_TYPE_SFX_AIN:return UINT32_C(0x60170000);
    case XX_FILE_TYPE_SFX_HAP:return UINT32_C(0x60170000);
    case XX_FILE_TYPE_SFX_LZX:return UINT32_C(0x60170000);
    case XX_FILE_TYPE_JAVA_JMOD:return UINT32_C(0x0);
    case XX_FILE_TYPE_SFX_RTPATCH:return UINT32_C(0x601c0000);
    default:return 0;}
}
static uint32_t sc_native_id(xx_file_type_t type,uint32_t id)
{
    if((id&UINT32_C(0xffff0000))!=SC_ID_BASE)return 0;
    return sc_native_prefix(type)|(id&UINT32_C(0xffff));
}
typedef struct sc_view {xx_io_device io;xx_io_device *source;int64_t begin,end,position;} sc_view;
static ssize_t sc_view_read(xx_io_device *device,void *buffer,size_t size)
{
    sc_view *v=device?device->priv:NULL;size_t prefix=0;
    if(!v||(!buffer&&size)||v->position<0||v->position>v->end)return -1;
    if((uint64_t)size>(uint64_t)(v->end-v->position))size=(size_t)(v->end-v->position);
    if(!size)return 0;
    /* Canonical scanners sometimes buffer a prefix ending in the payload.
     * Preserve absolute coordinates while masking unrelated carrier bytes. */
    if(v->position<v->begin){uint64_t n=(uint64_t)(v->begin-v->position);prefix=n<size?(size_t)n:size;xx_mem_zero(buffer,prefix);}
    if(size>prefix&&!xx_io_read_at(v->source,v->position+(int64_t)prefix,(uint8_t *)buffer+prefix,size-prefix))return -1;
    v->position+=(int64_t)size;return (ssize_t)size;
}
static int sc_view_seek64(xx_io_device *device,int64_t off,int whence)
{
    sc_view *v=device?device->priv:NULL;int64_t base,p;
    if(!v)return -1;
    if(whence==SEEK_SET)base=0;else if(whence==SEEK_CUR)base=v->position;else if(whence==SEEK_END)base=v->end;else return -1;
    if((off>0&&base>INT64_MAX-off)||(off<0&&off< -base))return -1;
    p=base+off;if(p<0||p>v->end)return -1;v->position=p;return 0;
}
static int sc_view_seek(xx_io_device *d,long off,int whence){return sc_view_seek64(d,(int64_t)off,whence);}
static int64_t sc_view_tell(xx_io_device *d){sc_view *v=d?d->priv:NULL;return v?v->position:-1;}
static int64_t sc_view_size(xx_io_device *d){sc_view *v=d?d->priv:NULL;return v?v->end:-1;}
typedef struct sc_reader {sc_view view;Abstractformat *reader;int64_t begin,end;} sc_reader;
static void sc_close(sc_reader *r)
{
    if(!r)return;if(r->reader)xx_format_free_reader(r->reader);xx_mem_free(r);
}
/* Locate only the span authenticated by the outer reader. Most wrappers expose
 * one physical payload record. 7ZIP and ZIPCENTRAL expose archive members and
 * retain a canonical reader instead; their proven coordinate cases are explicit. */
static bool sc_payload(Abstractformat *f,int64_t *begin,int64_t *size,xx_pd_struct *pd)
{
    int64_t total,available;
    if(!f||!f->device||!f->handle_base_info||!f->create_archive_records_reading||f->base_address<0||xx_pd_is_stopped(pd))return false;
    total=xx_io_total_size(f->device);if(total<f->base_address)return false;available=total-f->base_address;
    if((!f->base_info_handled&&!xx_format_handle_base_info(f,pd))||!f->is_valid||f->format_size<=0||f->format_size>available)return false;
    Abstractformat *validated_inner=NULL;
    switch(f->file_type){
    case XX_FILE_TYPE_SFX_RTPATCH:{xx_rtpatch *p=((xx_sfx_rtpatch *)f)->inner;validated_inner=p?&p->format:NULL;break;}
    case XX_FILE_TYPE_SFX_GXL:{xx_gxl *p=((xx_sfx_gxl *)f)->inner;validated_inner=p?&p->format:NULL;break;}
    case XX_FILE_TYPE_SFX_RTA:{xx_rta *p=((xx_sfx_rta *)f)->inner;validated_inner=p?&p->format:NULL;break;}
    case XX_FILE_TYPE_SFX_SQZ:{xx_sqz *p=((xx_sfx_sqz *)f)->inner;validated_inner=p?&p->format:NULL;break;}
    case XX_FILE_TYPE_SFX_CHZ:validated_inner=((xx_sfx_chz *)f)->inner;break;
    case XX_FILE_TYPE_SFX_SWAG:validated_inner=((xx_sfx_swag *)f)->inner;break;
    default:break;}
    if(f->file_type==XX_FILE_TYPE_MSKN1||f->file_type==XX_FILE_TYPE_MSKN2||f->file_type==XX_FILE_TYPE_MSKN3){
        /* Native MSKN validation inflates exactly the whole remainder at this
         * fixed signature boundary. Decoded theme/BMP records are not spans. */
        int64_t prefix=f->file_type==XX_FILE_TYPE_MSKN1?4:f->file_type==XX_FILE_TYPE_MSKN2?9:7;
        if(f->format_size<=prefix)return false;
        *begin=f->base_address+prefix;*size=f->format_size-prefix;
    }else if(f->file_type==XX_FILE_TYPE_ZBEOS){
        /* The native one-member stream measures deflate consumption and both
         * gzip trailer words. Its memory-backed record has no physical offset,
         * but packed_size includes the authenticated10+8-byte gzip framing. */
        if(f->format_size<=131072)return false;
        xx_archive_record_state *s=xx_format_create_archive_records_reading(f,NULL,pd);bool ok=false;
        if(s){const xx_archive_record *p=xx_format_get_current_archive_record(f,s);
            if(p&&p->compressed_size>=18){*begin=f->base_address+131072;*size=p->compressed_size;ok=!xx_format_archive_record_move_to_next(f,s,pd)&&!xx_pd_is_stopped(pd);}
            xx_format_free_archive_records_reading(f,s);
        }
        if(!ok)return false;
    }else if(validated_inner){
        if(!validated_inner->is_valid||validated_inner->device!=f->device)return false;
        *begin=validated_inner->base_address;*size=validated_inner->format_size;
    }else if(f->file_type==XX_FILE_TYPE_SFX_LHA){
        xx_sfx_lha *outer=(xx_sfx_lha *)f;
        if(!outer->inner_ready||outer->incomplete_tail||!outer->inner.format.is_valid||outer->payload_offset<0||outer->payload_offset>f->format_size)return false;
        *begin=f->base_address+outer->payload_offset;*size=outer->inner.format.format_size;
    }else if(f->file_type==XX_FILE_TYPE_SFX_ACE){
        if(!xx_sfx_ace_get_payload_span(f,begin,size,pd))return false;
    }else if(f->file_type==XX_FILE_TYPE_SFX_CAB){
        xx_sfx_cab *outer=(xx_sfx_cab *)f;Abstractformat *inner=&outer->inner.format;
        if(!outer->inner_ready||!inner->is_valid||inner->device!=f->device)return false;
        *begin=inner->base_address;*size=inner->format_size;
    }else if(f->file_type==XX_FILE_TYPE_SFX_7ZIP){
        Abstractformat *nested=((xx_sfx_7zip *)f)->nested_7zip;
        if(!nested||nested->device!=f->device||!nested->is_valid)return false;
        *begin=nested->base_address;*size=nested->format_size;
    }else if(f->file_type==XX_FILE_TYPE_SFX_ZIPCENTRAL){
        xx_sfx_zipcentral *outer=(xx_sfx_zipcentral *)f;Abstractformat *inner=&outer->inner.format;
        if(!outer->inner_ready){
            /* The validated absolute-offset carrier fallback exposes the whole
             * physical file. Revalidate ZIP with its public leading-data mode. */
            if(f->base_address!=0)return false;
            goto archive_payload;
        }
        if(!inner->is_valid)return false;
        *size=inner->format_size;
        if(outer->relative_view){
            /* Native relative_ensure accepts only a payload ending at outer end. */
            if(inner->base_address!=0||*size<=0||*size>f->format_size)return false;
            *begin=f->base_address+f->format_size-*size;
        }else{
            /* Native absolute_ensure is explicitly restricted to base zero. */
            if(f->base_address!=0||inner->device!=f->device||inner->base_address<0||inner->base_address>=*size)return false;
            *begin=inner->base_address;*size-=*begin;
        }
    }else{
archive_payload:;
        xx_archive_record_state *s=xx_format_create_archive_records_reading(f,NULL,pd);bool ok=false;
        if(s){const xx_archive_record *p=xx_format_get_current_archive_record(f,s);
            if(p){*begin=p->data_offset;*size=p->compressed_size;ok=!xx_format_archive_record_move_to_next(f,s,pd)&&!xx_pd_is_stopped(pd);}
            xx_format_free_archive_records_reading(f,s);
        }
        if(!ok)return false;
    }
    return *begin>=f->base_address&&*begin<=f->base_address+f->format_size&&*size>0&&*size<=f->base_address+f->format_size-*begin;
}
static sc_reader *sc_open(Abstractformat *f,xx_pd_struct *pd)
{
    int64_t begin,size;sc_reader *r;
    if(!sx_nativecarriers_supports(f?f->file_type:XX_FILE_TYPE_UNKNOWN)||!sc_payload(f,&begin,&size,pd))return NULL;
    r=xx_mem_alloc(sizeof(*r));if(!r)return NULL;xx_mem_zero(r,sizeof(*r));r->begin=begin;r->end=begin+size;
    r->view.source=f->device;r->view.begin=begin;r->view.end=r->end;r->view.position=begin;
    r->view.io.priv=&r->view;r->view.io.read=sc_view_read;r->view.io.seek=sc_view_seek;r->view.io.seek64=sc_view_seek64;r->view.io.tell=sc_view_tell;r->view.io.total_size=sc_view_size;
    /* Base zero avoids the factory's coordinate-remapping slice path. */
    r->reader=xx_format_create_reader(sc_canonical(f->file_type),&r->view.io,0);
    if(!r->reader){sc_close(r);return NULL;}xx_format_set_base_address(r->reader,begin);
    if(f->file_type==XX_FILE_TYPE_SFX_ZIPCENTRAL&&begin==0)((xx_zip *)r->reader)->allow_leading_data=true;
    if(!xx_format_handle_base_info(r->reader,pd)||!r->reader->is_valid||(r->reader->format_size!=size&&!((sc_canonical(f->file_type)==XX_FILE_TYPE_ZIP&&r->reader->format_size==r->end)||(f->file_type==XX_FILE_TYPE_SFX_SQX&&size>=7&&r->reader->format_size==size-7)))||!xx_format_has_data_structs(r->reader)){sc_close(r);return NULL;}
    return r;
}
static const char *sc_id_name(Abstractformat *f,uint32_t id)
{
    Abstractformat *r;const char *name;uint32_t native;
    if(id==XX_DATA_STRUCT_ID_RAW_DATA)return "RAW_DATA";
    if(!f||!(native=sc_native_id(f->file_type,id)))return "UNKNOWN";
    r=xx_format_create_reader(sc_canonical(f->file_type),f->device,0);if(!r)return "UNKNOWN";
    name=xx_format_data_struct_id_to_string(r,native);xx_format_free_reader(r);return name;
}
static uint32_t sc_name_id(Abstractformat *f,const char *name)
{
    Abstractformat *r;uint32_t id;
    if(!f||!name)return 0;if(!xx_rt_strcmp(name,"RAW_DATA"))return XX_DATA_STRUCT_ID_RAW_DATA;
    r=xx_format_create_reader(sc_canonical(f->file_type),f->device,0);if(!r)return 0;
    id=xx_format_data_struct_string_to_id(r,name);xx_format_free_reader(r);
    return id&&(id&UINT32_C(0xffff0000))==sc_native_prefix(f->file_type)?SC_ID_BASE|(id&UINT32_C(0xffff)):0;
}
typedef struct sc_struct_context {xx_data_struct *items;size_t count,capacity;} sc_struct_context;
static void sc_struct_free(void *p){sc_struct_context *c=p;if(c){xx_mem_free(c->items);xx_mem_free(c);}}
static bool sc_push(sc_struct_context *c,const xx_data_struct *d)
{
    if(c->count>=SC_MAX_ITEMS)return false;
    if(c->count==c->capacity){size_t n=c->capacity?c->capacity*2:32;if(n>SC_MAX_ITEMS)n=SC_MAX_ITEMS;void *p=xx_mem_realloc(c->items,n*sizeof(*c->items));if(!p)return false;c->items=p;c->capacity=n;}
    c->items[c->count++]=*d;return true;
}
static bool sc_gap(sc_struct_context *c,int64_t at,int64_t end)
{
    xx_data_struct d={XX_DATA_STRUCT_ID_RAW_DATA,at,-1,end-at,end-at,1,XX_DATA_STRUCT_TYPE_RAW_DATA};return at==end||(at<end&&sc_push(c,&d));
}
static bool sc_struct_next(Abstractformat *f,xx_data_struct_state *s,xx_pd_struct *pd)
{
    sc_struct_context *c=s?s->internal_state:NULL;(void)f;if(!c||xx_pd_is_stopped(pd)||s->current_index<0||(uint64_t)s->current_index+1>=c->count){if(s)s->has_struct=false;return false;}
    s->current_index++;s->current_struct=c->items[s->current_index];s->has_struct=true;return true;
}
static xx_data_struct_state *sc_structs(Abstractformat *f,xx_pd_struct *pd)
{
    sc_reader *r=sc_open(f,pd);xx_data_struct_state *inner=NULL,*s=NULL;sc_struct_context *c=NULL;int64_t end;size_t input_count=0;bool ok=false;
    if(!r)return NULL;c=xx_mem_alloc(sizeof(*c));if(!c)goto done;xx_mem_zero(c,sizeof(*c));end=f->base_address;
    inner=xx_format_create_data_structs_reading(r->reader,pd);if(!inner)goto done;
    do{const xx_data_struct *p=xx_format_get_current_data_struct(r->reader,inner);xx_data_struct d;if(!p)break;d=*p;
        if(xx_pd_is_stopped(pd)||++input_count>SC_MAX_ITEMS||d.offset<r->begin||d.offset>r->end||d.total_size<=0||d.total_size>r->end-d.offset)goto done;
        if(d.type==XX_DATA_STRUCT_TYPE_RAW_DATA)continue;
        if(!d.id||(d.id&UINT32_C(0xffff0000))!=sc_native_prefix(f->file_type)||(d.offset>end&&!sc_gap(c,end,d.offset)))goto done;
        d.id=SC_ID_BASE|(d.id&UINT32_C(0xffff));if(!sc_push(c,&d))goto done;if(d.offset+d.total_size>end)end=d.offset+d.total_size;
    }while(xx_format_data_struct_move_to_next(r->reader,inner,pd));
    if(!c->count||xx_pd_is_stopped(pd)||!sc_gap(c,end,xx_io_total_size(f->device)))goto done;
    s=xx_mem_alloc(sizeof(*s));if(!s)goto done;xx_data_struct_state_init(s,f);s->internal_state=c;s->free_internal=sc_struct_free;s->total_structs=c->count;s->current_index=0;s->current_struct=c->items[0];s->has_struct=true;ok=true;
done:
    if(inner)xx_format_free_data_structs_reading(r->reader,inner);sc_close(r);if(!ok){sc_struct_free(c);if(s)xx_data_struct_state_free(s);return NULL;}return s;
}
typedef struct sc_record_context {sc_reader *owner;xx_data_struct_record_state *inner;} sc_record_context;
static void sc_record_free(void *p){sc_record_context *c=p;if(c){if(c->inner)xx_format_free_data_struct_records_reading(c->owner->reader,c->inner);sc_close(c->owner);xx_mem_free(c);}}
static bool sc_record_load(xx_data_struct_record_state *s)
{
    sc_record_context *c=s?s->internal_state:NULL;const xx_data_struct_record *r;
    if(!c||(r=xx_format_get_current_data_struct_record(c->owner->reader,c->inner))==NULL)return false;
    /* Variable structs such as ZIP headers include filenames/comments after
     * their fixed entry prefix. Tables retain a single-row extent. */
    int64_t extent=s->parent_struct.count>1?s->parent_struct.entry_size:s->parent_struct.total_size;
    if(r->offset<0||r->size<=0||r->offset>extent||r->size>extent-r->offset)return false;
    xx_data_struct_record *v=&s->current_record;v->offset=r->offset;v->size=r->size;v->property=r->property;
    if((r->name&&!xx_data_struct_record_set_name(v,r->name))||(r->type&&!xx_data_struct_record_set_type(v,r->type))||(r->raw_value&&!xx_data_struct_record_set_raw_value(v,r->raw_value))||(r->display_value&&!xx_data_struct_record_set_display_value(v,r->display_value))||(r->comment&&!xx_data_struct_record_set_comment(v,r->comment))||!xx_data_struct_record_set_value(v,&r->value))return false;
    s->current_index=c->inner->current_index;s->total_records=c->inner->total_records;s->has_record=true;return true;
}
static bool sc_record_next(Abstractformat *f,xx_data_struct_record_state *s,xx_pd_struct *pd)
{
    sc_record_context *c=s?s->internal_state:NULL;(void)f;if(!c)return false;xx_data_struct_record_cleanup(&s->current_record);s->has_record=false;
    return !xx_pd_is_stopped(pd)&&xx_format_data_struct_record_move_to_next(c->owner->reader,c->inner,pd)&&sc_record_load(s);
}
static xx_data_struct_record_state *sc_records(Abstractformat *f,const xx_data_struct *d,xx_pd_struct *pd)
{
    uint32_t id;sc_reader *owner;sc_record_context *c;xx_data_struct_record_state *s;xx_data_struct native;
    if(!f||!d||!(id=sc_native_id(f->file_type,d->id))||d->type==XX_DATA_STRUCT_TYPE_RAW_DATA||d->entry_size<=0||d->total_size<d->entry_size)return NULL;
    owner=sc_open(f,pd);if(!owner)return NULL;
    if(d->offset<owner->begin||d->offset>owner->end||d->total_size>owner->end-d->offset){sc_close(owner);return NULL;}
    c=xx_mem_alloc(sizeof(*c));if(!c){sc_close(owner);return NULL;}xx_mem_zero(c,sizeof(*c));c->owner=owner;native=*d;native.id=id;
    c->inner=xx_format_create_data_struct_records_reading(owner->reader,&native,pd);if(!c->inner){sc_record_free(c);return NULL;}
    s=xx_mem_alloc(sizeof(*s));if(!s){sc_record_free(c);return NULL;}xx_data_struct_record_state_init(s,f,d);s->internal_state=c;s->free_internal=sc_record_free;
    if(!sc_record_load(s)){xx_data_struct_record_state_free(s);return NULL;}return s;
}
static const StreamDataStructs *sx_nativecarriers_get_stream_datastructs(Abstractformat *f)
{
    if(!f||!sx_nativecarriers_supports(f->file_type))return NULL;
    static const StreamDataStructs stream = {
        .data_struct_id_to_string = sc_id_name,
        .data_struct_string_to_id = sc_name_id,
        .create_data_structs_reading = sc_structs,
        .data_struct_move_to_next = sc_struct_next,
        .create_data_struct_records_reading = sc_records,
        .data_struct_record_move_to_next = sc_record_next
    };
    return &stream;
}
#undef SC_ID_BASE
#undef SC_MAX_ITEMS
#else
static const StreamDataStructs *sx_nativecarriers_get_stream_datastructs(Abstractformat *f){(void)f;return NULL;}
#endif
