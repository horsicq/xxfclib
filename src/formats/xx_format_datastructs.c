/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded declarative on-disk schemas for readers without a native provider.
 * RAW_DATA remains explicitly unparsed. Each family supplies genuine layouts.
 *
 * This file is the schema engine. Every family unit (xx_format_schema_<family>.c)
 * and every per-format data file is included below through
 * xx_format_schema_units.inc, so the whole schema is this one translation unit
 * and source lists name only this file. tools/generate_schema_registry.py
 * regenerates the unit list and the family table.
 */
#include "xx_format_schema.h"
#include "xxfclib/formats/xx_format_streams.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"

#include "xx_format_schema_variable_field.inc"

#define SX_MAX_ITEMS 100000U
static bool sx_range(sx_context *c, int64_t off, uint64_t n)
{
    if (!c || c->failed || off < 0 || off > c->size || n > (uint64_t)(c->size-off) || xx_pd_is_stopped(c->pd)) {
        if (c) c->failed = true;
        return false;
    }
    return true;
}
static bool sx_read(sx_context *c, int64_t off, void *out, size_t n)
{
    size_t done=0;
    if (!sx_range(c,off,n) || (!out && n)) return false;
    if (!n) return true;
    if (xx_io_seek64(c->f->device,c->f->base_address+off,SEEK_SET) != 0) { c->failed=true; return false; }
    while (done<n) {
        ssize_t got=xx_io_read(c->f->device,(uint8_t *)out+done,n-done);
        if (got<=0 || (size_t)got>n-done || xx_pd_is_stopped(c->pd)) { c->failed=true; return false; }
        done+=(size_t)got;
    }
    return true;
}
static bool sx_u16(sx_context *c,int64_t o,bool be,uint16_t *v) { uint8_t b[2]; if(!v||!sx_read(c,o,b,2))return false;*v=xx_data_get_u16(b,2,0,be);return true; }
static bool sx_u32(sx_context *c,int64_t o,bool be,uint32_t *v) { uint8_t b[4]; if(!v||!sx_read(c,o,b,4))return false;*v=xx_data_get_u32(b,4,0,be);return true; }
static bool sx_u64(sx_context *c,int64_t o,bool be,uint64_t *v) { uint8_t b[8]; if(!v||!sx_read(c,o,b,8))return false;*v=xx_data_get_u64(b,8,0,be);return true; }
static bool sx_push(sx_context *c,const sx_layout *l,int64_t off,int64_t entry,uint64_t count,bool raw)
{
    uint64_t total;
    xx_data_struct *d;
    if (entry<0 || (!raw && (!l || !l->fields || !l->field_count || entry==0))) return false;
    if (!count) return true;
    if ((uint64_t)entry>INT64_MAX/count) return false;
    total=(uint64_t)entry*count;
    if (!sx_range(c,off,total) || c->count>=SX_MAX_ITEMS) {c->failed=true;return false;}
    if (!raw) for(size_t i=0;i<l->field_count;i++) {
        xx_data_struct_field_desc field;wchar_t type[64];
        if(!sx_expand_field(l,i,entry,&field,type)) {c->failed=true;return false;}
    }
    if(c->count==c->capacity) {
        size_t cap=c->capacity?c->capacity*2:32;
        void *p;
        if(cap>SX_MAX_ITEMS)cap=SX_MAX_ITEMS;
        p=xx_mem_realloc(c->items,cap*sizeof(*c->items));
        if(!p){c->failed=true;return false;}
        c->items=p;c->capacity=cap;
    }
    d=&c->items[c->count++];
    xx_mem_zero(d,sizeof(*d));
    d->id=raw?XX_DATA_STRUCT_ID_RAW_DATA:l->id;
    d->offset=c->f->base_address+off; d->address=-1;
    d->entry_size=entry;d->total_size=(int64_t)total;d->count=count;
    d->type=raw?XX_DATA_STRUCT_TYPE_RAW_DATA:XX_DATA_STRUCT_TYPE_STRUCT;
    return true;
}
static bool sx_add(sx_context *c,const sx_layout *l,int64_t o,int64_t n) {return sx_push(c,l,o,n,1,false);}
static bool sx_table(sx_context *c,const sx_layout *l,int64_t o,int64_t n,uint64_t count) {return sx_push(c,l,o,n,count,false);}
static bool sx_raw(sx_context *c,int64_t o,int64_t n) {if(!n)return true;return sx_push(c,NULL,o,n,1,true);}

#include "xx_format_schema_units.inc"
#include "xx_format_schema_families.inc"
#include "xx_format_schema_field_endian.inc"


static const sx_layout *sx_lookup(Abstractformat *f,uint32_t id,const char *name)
{
    const sx_layout *const *rows=NULL;
    const sx_family *family;
    size_t n=0;
    xx_file_type_t type;
    if(!f)return NULL;
    type=sx_alias_canonical_type(f->file_type);
    if(type==XX_FILE_TYPE_BINSH_STARKIT)return sxiw_starkit_lookup(id,name);
    if((family=sx_family_find(type))){rows=family->layouts;n=family->layout_count;}
    for(size_t i=0;i<n;i++)if((name&&xx_rt_strcmp(rows[i]->name,name)==0)||(!name&&rows[i]->id==id))return rows[i];
    return NULL;
}
static const char *sx_id_name(Abstractformat *f,uint32_t id)
{
    const sx_layout *l=sx_lookup(f,id,NULL);
    return id==XX_DATA_STRUCT_ID_RAW_DATA?"RAW_DATA":l?l->name:"UNKNOWN";
}
static uint32_t sx_name_id(Abstractformat *f,const char *name)
{
    const sx_layout *l;
    if(!name)return 0;
    if(!xx_rt_strcmp(name,"RAW_DATA"))return XX_DATA_STRUCT_ID_RAW_DATA;
    l=sx_lookup(f,0,name);return l?l->id:0;
}
static void sx_context_free(void *p)
{
    sx_context *c=p;
    if(c){
        xx_mem_free(c->items);
#if SX_ALIAS_READER_FACTORY
        if(c->owned_reader)xx_format_free_reader(c->owned_reader);
#endif
        xx_mem_free(c);
    }
}
static bool sx_struct_next(Abstractformat *f,xx_data_struct_state *s,xx_pd_struct *pd)
{
    sx_context *c=s?s->internal_state:NULL;(void)f;
    if(!c||xx_pd_is_stopped(pd)){if(s)s->has_struct=false;return false;}
    if(s->current_index<0||(uint64_t)s->current_index+1>=c->count){s->has_struct=false;return false;}
    s->current_index++;s->current_struct=c->items[s->current_index];s->has_struct=true;return true;
}
static xx_data_struct_state *sx_structs(Abstractformat *f,xx_pd_struct *pd)
{
    xx_data_struct_state *s;
    sx_context *c;
    int64_t total, available, content_end;
    bool ok;
    if(!f||!f->device||f->base_address<0||xx_pd_is_stopped(pd)||(total=xx_io_total_size(f->device))<f->base_address)return NULL;
#if SX_ALIAS_READER_FACTORY
    if(sx_alias_supports(f->file_type)) {
        Abstractformat *canonical;
        if((!f->base_info_handled&&!xx_format_handle_base_info(f,pd))||!f->is_valid)return NULL;
        /* Retain the caller's device coordinates. The factory's nonzero-base
         * path creates a sliced view whose descriptors start at zero. */
        canonical=xx_format_create_reader(sx_alias_canonical_type(f->file_type),f->device,0);
        if(!canonical)return NULL;
        xx_format_set_base_address(canonical,f->base_address);
        if(!xx_format_handle_base_info(canonical,pd)||!canonical->is_valid) {xx_format_free_reader(canonical);return NULL;}
        s=sx_structs(canonical,pd);
        if(!s){xx_format_free_reader(canonical);return NULL;}
        ((sx_context *)s->internal_state)->owned_reader=canonical;
        s->format=f;
        return s;
    }
#endif
    c=xx_mem_alloc(sizeof(*c));if(!c)return NULL;xx_mem_zero(c,sizeof(*c));
    c->f=f;c->pd=pd;available=total-f->base_address;content_end=available;
    if(!sx_volume_family.supports(f->file_type)&&f->base_info_handled&&f->is_valid&&f->format_size>0&&f->format_size<=available)content_end=f->format_size;
    c->size=content_end;
    ok=sx_family_build(c);
    if(!ok||c->failed||xx_pd_is_stopped(pd)){sx_context_free(c);return NULL;}
    c->size=available;
    if(content_end<available&&!sx_raw(c,content_end,available-content_end)){sx_context_free(c);return NULL;}
    s=xx_mem_alloc(sizeof(*s));if(!s){sx_context_free(c);return NULL;}
    xx_data_struct_state_init(s,f);s->internal_state=c;s->free_internal=sx_context_free;s->total_structs=(int64_t)c->count;
    if(c->count){s->current_struct=c->items[0];s->current_index=0;s->has_struct=true;}
    return s;
}
typedef struct sx_record_context { const sx_layout *layout; bool be; } sx_record_context;
/* Variable-width integer encodings retain their physical bytes but expose
 * the decoded numeric value and semantic role to the common formatter. */
static bool sx_size_comment(xx_data_struct_record *r,uint64_t v)
{
    char a[96];wchar_t w[96];size_t i;
    if(!xx_str_bytes_count_to_string(v,1024,a,sizeof(a)))return false;
    for(i=0;a[i];i++)w[i]=(unsigned char)a[i];w[i]=0;
    return xx_data_struct_record_set_comment(r,w);
}
#include "xx_format_schema_legacy_integer.inc"
#include "xx_format_schema_cpio_integer.inc"
#include "xx_format_schema_ascii_scalar.inc"
#include "xx_format_schema_nastran_real.inc"
#include "xx_format_schema_fortran_real.inc"
#include "xx_format_schema_utf8_field.inc"
#include "xx_format_schema_ones_complement.inc"
#include "xx_format_schema_msb_bits.inc"
#include "xx_format_schema_stuffitx_bits.inc"
#include "xx_format_schema_bias33.inc"

static bool sx_decode_ascii_integer(xx_data_struct_record_state *s,const xx_data_struct_field_desc *field)
{
    unsigned base=field->type&&!xx_rt_wcsncmp(field->type,L"ascii_hex[",10)?16:field->type&&!xx_rt_wcsncmp(field->type,L"ascii_octal[",12)?8:0;
    uint8_t bytes[24];uint64_t v=0;char a[40];wchar_t w[40];int n;
    if(!base)return true;
    if(field->size<1||field->size>(int64_t)sizeof(bytes)||!xx_io_read_at(s->format->device,s->parent_struct.offset+field->rel_offset,bytes,(size_t)field->size))return false;
    for(int64_t i=0;i<field->size;i++) {
        unsigned d=bytes[i]>='0'&&bytes[i]<='9'?bytes[i]-'0':bytes[i]>='a'&&bytes[i]<='f'?bytes[i]-'a'+10:bytes[i]>='A'&&bytes[i]<='F'?bytes[i]-'A'+10:255;
        if(d>=base||v>(UINT64_MAX-d)/base)return false;v=v*base+d;
    }
    xx_var_set_u64(&s->current_record.value,v);
    bool hex=(field->property&(SX_ID|SX_FLAGS|SX_SIGNATURE|SX_CHECKSUM|SX_OFFSET|SX_POINTER|SX_ADDRESS))!=0;
    n=xx_rt_snprintf(a,sizeof(a),hex?"0x%llX":"%llu",(unsigned long long)v);
    if(n<0||n>=(int)sizeof(a))return false;for(int i=0;i<=n;i++)w[i]=(unsigned char)a[i];
    return xx_data_struct_record_set_display_value(&s->current_record,w)&&(!(field->property&SX_SIZE)||sx_size_comment(&s->current_record,v));
}
#include "xx_format_schema_uleb64.inc"
#include "xx_format_schema_freearc_uint.inc"

#include "xx_format_schema_zigzag.inc"

static bool sx_decode_encoded_integer(xx_data_struct_record_state *s, const xx_data_struct_field_desc *field)
{
    bool base128=field->type&&!xx_rt_wcscmp(field->type,L"uintbase128");
    bool uleb=field->type&&!xx_rt_wcscmp(field->type,L"uleb128");
    uint8_t bytes[5];uint32_t v=0;char ascii[32];wchar_t display[32];
    if(!base128&&!uleb)return true;
    if(field->size<1||field->size>5||!xx_io_read_at(s->format->device,s->parent_struct.offset+field->rel_offset,bytes,(size_t)field->size))return false;
    for(size_t i=0;i<(size_t)field->size;i++) {
        if(((bytes[i]&128)!=0)!=(i+1<(size_t)field->size))return false;
        if(base128){if((i==0&&bytes[i]==128)||(v&0xfe000000U))return false;v=(v<<7)|(bytes[i]&127);}
        else{if(i==4&&(bytes[i]&0xf0))return false;v|=(uint32_t)(bytes[i]&127)<<(7*i);}
    }
    xx_var_set_u32(&s->current_record.value,v);
    int n=xx_rt_snprintf(ascii,sizeof(ascii),"%u",(unsigned)v);
    if(n<0||n>=(int)sizeof(ascii))return false;
    for(int i=0;i<=n;i++)display[i]=(unsigned char)ascii[i];
    return xx_data_struct_record_set_raw_bytes(&s->current_record,s->format->device,s->parent_struct.offset+field->rel_offset,field->size)&&
           xx_data_struct_record_set_display_value(&s->current_record,display)&&
           (!(field->property&SX_SIZE)||sx_size_comment(&s->current_record,v));
}
static bool sx_record_load(xx_data_struct_record_state *s)
{
    sx_record_context *c=s->internal_state;
    const xx_data_struct_field_desc *field;
    if(s->current_index<0||(uint64_t)s->current_index>=c->layout->field_count)return false;
    xx_data_struct_field_desc expanded;wchar_t type[64];
    if(!sx_expand_field(c->layout,(size_t)s->current_index,s->parent_struct.entry_size,&expanded,type))return false;
    field=&expanded;
    if(!sx_populate_record_field(&s->current_record,s->format->device,s->parent_struct.offset,field,c->be))return false;
    if(!sx_decode_fortran_real(s,field)||!sx_decode_nastran_real(s,field)||!sx_decode_ascii_scalar(s,field)||!sx_decode_utf8_field(s,field)||!sx_decode_bias33(s,field)||!sx_decode_stuffitx_bits(s,field)||!sx_decode_msb_bits(s,field)||!sx_decode_ones_complement(s,field)||!sx_decode_freearc_uint(s,field)||!sx_decode_uleb64(s,field)||!sx_decode_cpio_integer(s,field)||!sx_decode_legacy_integer(s,field)||!sx_decode_ascii_integer(s,field)||!sx_decode_zigzag(s,field)||!sx_decode_encoded_integer(s,field)||!sx_family_comments(&s->current_record,c->layout->id)) {
        xx_data_struct_record_cleanup(&s->current_record);return false;
    }
    s->has_record=true;return true;
}
static bool sx_record_next(Abstractformat *f,xx_data_struct_record_state *s,xx_pd_struct *pd)
{
    (void)f;
    if(!s||!s->internal_state)return false;
    xx_data_struct_record_cleanup(&s->current_record);s->has_record=false;
    if(xx_pd_is_stopped(pd))return false;
    s->current_index++;return sx_record_load(s);
}
static xx_data_struct_record_state *sx_records(Abstractformat *f,const xx_data_struct *d,xx_pd_struct *pd)
{
    const sx_layout *l;
    xx_data_struct_record_state *s;
    sx_record_context *c;
    int64_t total;
    if(!f||!d||xx_pd_is_stopped(pd)||!(l=sx_lookup(f,d->id,NULL))||f->base_address<0||d->offset<f->base_address||d->entry_size<=0||d->count==0||d->total_size<d->entry_size)return NULL;
    total=xx_io_total_size(f->device);
    if(d->offset>total||d->total_size>total-d->offset||d->count>(uint64_t)d->total_size/(uint64_t)d->entry_size)return NULL;
    for(size_t i=0;i<l->field_count;i++){xx_data_struct_field_desc field;wchar_t type[64];if(!sx_expand_field(l,i,d->entry_size,&field,type))return NULL;}
    s=xx_mem_alloc(sizeof(*s));if(!s)return NULL;
    xx_data_struct_record_state_init(s,f,d);
    c=xx_mem_alloc(sizeof(*c));if(!c){xx_data_struct_record_state_free(s);return NULL;}
    c->layout=l;c->be=l->endian==SX_BE||(l->endian==SX_FILE&&f->endian==XX_ENDIAN_BIG);
    s->internal_state=c;s->free_internal=xx_mem_free;s->total_records=(int64_t)l->field_count;s->current_index=0;
    if(!sx_record_load(s)){xx_data_struct_record_state_free(s);return NULL;}return s;
}
#include "xx_format_schema_disknext_adapter.inc"
#include "xx_format_schema_installerwire_adapter.inc"

const StreamDataStructs *xx_format_schema_get_stream_datastructs(Abstractformat *f)
{
    xx_file_type_t type;
    if(!f)return NULL;
    const StreamDataStructs *native = sx_nativecarriers_get_stream_datastructs(f);
    if(native)return native;
    type=sx_alias_canonical_type(f->file_type);
    if(!sx_family_find(type))return NULL;
    static const StreamDataStructs standard = {
        .data_struct_id_to_string = sx_id_name,
        .data_struct_string_to_id = sx_name_id,
        .create_data_structs_reading = sx_structs,
        .data_struct_move_to_next = sx_struct_next,
        .create_data_struct_records_reading = sx_records,
        .data_struct_record_move_to_next = sx_record_next
    };
    static const StreamDataStructs starkit = {
        .data_struct_id_to_string = sx_id_name,
        .data_struct_string_to_id = sx_name_id,
        .create_data_structs_reading = sx_structs,
        .data_struct_move_to_next = sx_struct_next,
        .create_data_struct_records_reading = sx_installerwire_records,
        .data_struct_record_move_to_next = sx_record_next
    };
    static const StreamDataStructs psadisk = {
        .data_struct_id_to_string = sx_id_name,
        .data_struct_string_to_id = sx_name_id,
        .create_data_structs_reading = sx_structs,
        .data_struct_move_to_next = sx_struct_next,
        .create_data_struct_records_reading = sx_disknext_records,
        .data_struct_record_move_to_next = sx_record_next
    };
    return f->file_type == XX_FILE_TYPE_BINSH_STARKIT ? &starkit :
           f->file_type == XX_FILE_TYPE_PSA_DISK ? &psadisk : &standard;
}
