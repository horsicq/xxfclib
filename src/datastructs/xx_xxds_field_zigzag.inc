/* Compact Thrift signed integers are ZigZag mapped and stored as ULEB128.
 * Their declared signed width is independent of the physical byte count. */
static bool sx_decode_zigzag(xx_data_struct_record_state *s,const xx_data_struct_field_desc *field)
{
    unsigned bits=field&&field->type&&!xx_rt_wcscmp(field->type,L"zigzag16")?16:field&&field->type&&!xx_rt_wcscmp(field->type,L"zigzag32")?32:field&&field->type&&!xx_rt_wcscmp(field->type,L"zigzag64")?64:0;
    if(!bits)return true;uint8_t b[10];uint64_t u=0;char a[48];wchar_t display[48];
    if(!s||!s->format||!s->format->device||field->size<1||field->size>10||s->parent_struct.offset<0||field->rel_offset<0||field->rel_offset>INT64_MAX-s->parent_struct.offset||!xx_io_read_at(s->format->device,s->parent_struct.offset+field->rel_offset,b,(size_t)field->size))return false;
    for(size_t i=0;i<(size_t)field->size;i++){if(((b[i]&128)!=0)!=(i+1<(size_t)field->size)||(i==9&&(b[i]&254)))return false;u|=(uint64_t)(b[i]&127)<<(7*i);}
    int64_t v=(int64_t)(u>>1)^-(int64_t)(u&1);if((bits==16&&(v<INT16_MIN||v>INT16_MAX))||(bits==32&&(v<INT32_MIN||v>INT32_MAX))){
        /* Native readers may skip an unknown field without interpreting its
         * declared signed width. Keep that optional malformed value visible. */
        return xx_var_set_bytes(&s->current_record.value,b,(size_t)field->size)&&xx_data_struct_record_set_raw_bytes(&s->current_record,s->format->device,s->parent_struct.offset+field->rel_offset,field->size)&&xx_data_struct_record_set_display_value(&s->current_record,s->current_record.raw_value)&&xx_data_struct_record_set_comment(&s->current_record,L"ZigZag value exceeds declared signed width; original bytes preserved");
    }
    bool hex=(field->property&(SX_ID|SX_FLAGS|SX_SIGNATURE|SX_CHECKSUM|SX_OFFSET|SX_POINTER|SX_ADDRESS))!=0;uint64_t mag=v<0?(uint64_t)(-(v+1))+1:(uint64_t)v;
    int n=hex?xx_rt_snprintf(a,sizeof(a),v<0?"-0x%llX":"0x%llX",(unsigned long long)mag):xx_rt_snprintf(a,sizeof(a),"%lld",(long long)v);
    if(n<0||n>=(int)sizeof(a))return false;for(int i=0;i<=n;i++)display[i]=(unsigned char)a[i];xx_var_set_i64(&s->current_record.value,v);
    return xx_data_struct_record_set_raw_bytes(&s->current_record,s->format->device,s->parent_struct.offset+field->rel_offset,field->size)&&xx_data_struct_record_set_display_value(&s->current_record,display)&&(!(field->property&SX_SIZE)||(v<0?xx_data_struct_record_set_comment(&s->current_record,L"Negative encoded size"):sx_size_comment(&s->current_record,(uint64_t)v)));
}
