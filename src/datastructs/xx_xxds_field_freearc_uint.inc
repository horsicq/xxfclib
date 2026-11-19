/* FreeArc's low-one-bit length prefix; opt-in only, independent of endian.
 * Non-minimal encodings are retained, matching the native reader. */
static bool sx_decode_freearc_uint(xx_data_struct_record_state*s,const xx_data_struct_field_desc*field)
{
    uint8_t b[9];uint64_t value=0;unsigned extra=0;char a[40];wchar_t display[40];
    if(!field||!field->type||xx_rt_wcscmp(field->type,L"freearc_uint"))return true;
    if(!s||!s->format||!s->format->device||field->size<1||field->size>9||
       s->parent_struct.offset<0||s->parent_struct.entry_size<0||field->rel_offset<0||
       field->rel_offset>s->parent_struct.entry_size||field->size>s->parent_struct.entry_size-field->rel_offset||
       field->rel_offset>INT64_MAX-s->parent_struct.offset||
       !xx_io_read_at(s->format->device,s->parent_struct.offset+field->rel_offset,b,(size_t)field->size))return false;
    while(extra<8&&(b[0]&(1U<<extra)))extra++;
    if(field->size!=(int64_t)extra+1)return false;
    if(extra==8){for(unsigned i=0;i<8;i++)value|=(uint64_t)b[i+1]<<(8*i);}
    else{for(unsigned i=0;i<=extra;i++)value|=(uint64_t)b[i]<<(8*i);value>>=extra+1;}
    bool hex=(field->property&(SX_ID|SX_FLAGS|SX_SIGNATURE|SX_OFFSET|SX_POINTER|SX_ADDRESS))!=0;
    int n=xx_rt_snprintf(a,sizeof(a),hex?"0x%llX":"%llu",(unsigned long long)value);
    if(n<0||n>=(int)sizeof(a))return false;for(int i=0;i<=n;i++)display[i]=(unsigned char)a[i];
    xx_var_set_u64(&s->current_record.value,value);
    return xx_data_struct_record_set_raw_bytes(&s->current_record,s->format->device,s->parent_struct.offset+field->rel_offset,field->size)&&
           xx_data_struct_record_set_display_value(&s->current_record,display)&&
           (!(field->property&SX_SIZE)||sx_size_comment(&s->current_record,value));
}
