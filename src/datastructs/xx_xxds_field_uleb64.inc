/* Unsigned 64-bit LEB128. Overlong but bounded encodings are retained because
 * native LevelDB and Protobuf readers accept them. All ten physical bytes are
 * preserved; byte ten may carry only bit 0 and may not continue. */
static bool sx_decode_uleb64(xx_data_struct_record_state *s,const xx_data_struct_field_desc *field)
{
    uint8_t b[10];uint64_t value=0;char a[40];wchar_t display[40];
    if(!field||!field->type||xx_rt_wcscmp(field->type,L"uleb128_64"))return true;
    if(!s||!s->format||!s->format->device||field->size<1||field->size>10||
       s->parent_struct.offset<0||field->rel_offset<0||field->rel_offset>INT64_MAX-s->parent_struct.offset||
       !xx_io_read_at(s->format->device,s->parent_struct.offset+field->rel_offset,b,(size_t)field->size))return false;
    for(size_t i=0;i<(size_t)field->size;i++){
        if(((b[i]&128)!=0)!=(i+1<(size_t)field->size)||(i==9&&(b[i]&254)))return false;
        value|=(uint64_t)(b[i]&127)<<(7*i);
    }
    bool hex=(field->property&(SX_ID|SX_FLAGS|SX_SIGNATURE|SX_CHECKSUM|SX_OFFSET|SX_POINTER|SX_ADDRESS))!=0;
    int n=xx_rt_snprintf(a,sizeof(a),hex?"0x%llX":"%llu",(unsigned long long)value);
    if(n<0||n>=(int)sizeof(a))return false;for(int i=0;i<=n;i++)display[i]=(unsigned char)a[i];
    xx_var_set_u64(&s->current_record.value,value);
    return xx_data_struct_record_set_raw_bytes(&s->current_record,s->format->device,s->parent_struct.offset+field->rel_offset,field->size)&&
           xx_data_struct_record_set_display_value(&s->current_record,display)&&
           (!(field->property&SX_SIZE)||sx_size_comment(&s->current_record,value));
}
