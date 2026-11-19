/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Field decoding of the DataStructs script host. A script names a field's
 * type; this file turns the bytes into the record's value, raw text and
 * display text. Besides the common formatter (xx_data_struct_record_populate)
 * it knows the extended type names: explicit byte order (uint32be, int16le),
 * text numbers (ascii_octal[n], ascii_hex[n]), variable-length integers
 * (uleb128, uintbase128, zigzag32, ...), legacy integer and real encodings.
 * The decoders are keyed on the type text alone, so every script gets them.
 */
#include "xx_xxds_internal.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>

#define SX_NONE XX_DATA_STRUCT_RECORD_PROPERTY_NONE
#define SX_ID XX_DATA_STRUCT_RECORD_PROPERTY_ID
#define SX_ADDRESS XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS
#define SX_OFFSET XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET
#define SX_SIZE XX_DATA_STRUCT_RECORD_PROPERTY_SIZE
#define SX_COUNT_PROP XX_DATA_STRUCT_RECORD_PROPERTY_COUNT
#define SX_POINTER XX_DATA_STRUCT_RECORD_PROPERTY_POINTER
#define SX_FLAGS XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS
#define SX_TIME XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP
#define SX_STRING XX_DATA_STRUCT_RECORD_PROPERTY_STRING
#define SX_RESERVED XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED
#define SX_SIGNATURE XX_DATA_STRUCT_RECORD_PROPERTY_SIGNATURE
#define SX_CHECKSUM XX_DATA_STRUCT_RECORD_PROPERTY_CHECKSUM

static bool sx_size_comment(xx_data_struct_record *r,uint64_t v)
{
    char a[96];wchar_t w[96];size_t i;
    if(!xx_str_bytes_count_to_string(v,1024,a,sizeof(a)))return false;
    for(i=0;a[i];i++)w[i]=(unsigned char)a[i];w[i]=0;
    return xx_data_struct_record_set_comment(r,w);
}

#include "xx_xxds_field_endian.inc"
#include "xx_xxds_field_legacy_integer.inc"
#include "xx_xxds_field_cpio_integer.inc"
#include "xx_xxds_field_ascii_scalar.inc"
#include "xx_xxds_field_nastran_real.inc"
#include "xx_xxds_field_fortran_real.inc"
#include "xx_xxds_field_utf8.inc"
#include "xx_xxds_field_ones_complement.inc"
#include "xx_xxds_field_msb_bits.inc"
#include "xx_xxds_field_stuffitx_bits.inc"
#include "xx_xxds_field_bias33.inc"
#include "xx_xxds_field_uleb64.inc"
#include "xx_xxds_field_freearc_uint.inc"
#include "xx_xxds_field_zigzag.inc"

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

bool xx_xxds_populate_field(xx_data_struct_record *record, Abstractformat *format, const xx_data_struct *parent, const xx_data_struct_field_desc *field,
                            bool big_endian)
{
    xx_data_struct_record_state state;
    xx_data_struct_record_state *s = &state;
    if (!record || !format || !format->device || !parent || !field) return false;
    xx_mem_zero(&state, sizeof(state));
    state.format = format;
    state.parent_struct = *parent;
    if (!sx_populate_record_field(&state.current_record, format->device, parent->offset, field, big_endian)) return false;
    if (!sx_decode_fortran_real(s, field) ||
        !sx_decode_nastran_real(s, field) ||
        !sx_decode_ascii_scalar(s, field) ||
        !sx_decode_utf8_field(s, field) ||
        !sx_decode_bias33(s, field) ||
        !sx_decode_stuffitx_bits(s, field) ||
        !sx_decode_msb_bits(s, field) ||
        !sx_decode_ones_complement(s, field) ||
        !sx_decode_freearc_uint(s, field) ||
        !sx_decode_uleb64(s, field) ||
        !sx_decode_cpio_integer(s, field) ||
        !sx_decode_legacy_integer(s, field) ||
        !sx_decode_ascii_integer(s, field) ||
        !sx_decode_zigzag(s, field) ||
        !sx_decode_encoded_integer(s, field)) {
        xx_data_struct_record_cleanup(&state.current_record);
        return false;
    }
    *record = state.current_record;
    return true;
}
