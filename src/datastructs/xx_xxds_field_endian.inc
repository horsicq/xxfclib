/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Explicit integer field byte order overrides a containing layout's order.
 * Types uint8le..uint64le / uint8be..uint64be and signed int equivalents
 * use the common formatter after normalizing the suffix. Physical raw bytes
 * and the original explicit type name remain available to the caller. */
static bool sx_populate_record_field(xx_data_struct_record *record, xx_io_device *device,
                                     int64_t parent, const xx_data_struct_field_desc *field, bool fallback_be)
{
    const wchar_t *type=field?field->type:NULL;size_t prefix=0,len=type?xx_rt_wcslen(type):0;
    wchar_t canonical[8];unsigned bits=0;bool be=false;xx_data_struct_field_desc normalized;
    if(type&&!xx_rt_wcsncmp(type,L"uint",4))prefix=4;
    else if(type&&!xx_rt_wcsncmp(type,L"int",3))prefix=3;
    if(!prefix||len<prefix+3||len>prefix+4||(xx_rt_wcscmp(type+len-2,L"be")&&xx_rt_wcscmp(type+len-2,L"le")))
        return xx_data_struct_record_populate(record,device,parent,field,fallback_be);
    for(size_t i=prefix;i<len-2;i++){if(type[i]<L'0'||type[i]>L'9')return false;bits=bits*10+(unsigned)(type[i]-L'0');}
    if(!bits||bits>64||(bits%8)||field->size!=(int64_t)(bits/8))return false;
    for(size_t i=0;i<len-2;i++)canonical[i]=type[i];canonical[len-2]=0;
    normalized=*field;normalized.type=canonical;be=type[len-2]==L'b';
    if(!xx_data_struct_record_populate(record,device,parent,&normalized,be))return false;
    if(!xx_data_struct_record_set_type(record,type)||!xx_data_struct_record_set_raw_bytes(record,device,parent+field->rel_offset,field->size)){
        xx_data_struct_record_cleanup(record);return false;
    }
    return true;
}
