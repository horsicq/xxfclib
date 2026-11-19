/* Physical fixed-width ASCII decimal and PDP-11 word-swapped and big-endian halfword-swapped integers.
 * Decimal whitespace matches bounded archive readers; no sign is accepted. */
static bool sx_decode_legacy_integer(xx_data_struct_record_state *s,const xx_data_struct_field_desc *field)
{
    bool decimal=field&&field->type&&!xx_rt_wcsncmp(field->type,L"ascii_decimal[",14);
    bool octal=field&&field->type&&!xx_rt_wcsncmp(field->type,L"ascii_octal_padded[",19);
    bool pdp=field&&field->type&&!xx_rt_wcscmp(field->type,L"uint32_pdp");
    bool halfword=field&&field->type&&!xx_rt_wcscmp(field->type,L"uint32_halfword_be");
    uint8_t b[24];uint64_t v=0;char a[40];wchar_t display[40];
    if(!decimal&&!octal&&!pdp&&!halfword)return true;
    if(decimal||octal){
        size_t i=decimal?14:19;uint64_t width=0;bool digit=false;
        for(;field->type[i]>=L'0'&&field->type[i]<=L'9';i++){
            unsigned d=(unsigned)(field->type[i]-L'0');if(width>(UINT64_MAX-d)/10)return false;width=width*10+d;digit=true;
        }
        if(!digit||field->type[i]!=L']'||field->type[i+1]||field->size<1||width!=(uint64_t)field->size)return false;
    }
    if(!s||!s->format||!s->format->device||field->size<1||field->size>(int64_t)sizeof(b)||((pdp||halfword)&&field->size!=4)||s->parent_struct.offset<0||field->rel_offset<0||field->rel_offset>INT64_MAX-s->parent_struct.offset||(halfword&&(s->parent_struct.entry_size<field->size||field->rel_offset>s->parent_struct.entry_size-field->size))||!xx_io_read_at(s->format->device,s->parent_struct.offset+field->rel_offset,b,(size_t)field->size))return false;
    if(halfword)v=((uint32_t)b[2]<<24)|((uint32_t)b[3]<<16)|((uint32_t)b[0]<<8)|b[1];
    else if(pdp)v=((uint32_t)b[1]<<24)|((uint32_t)b[0]<<16)|((uint32_t)b[3]<<8)|b[2];
    else{
        size_t first=0,last=(size_t)field->size;
        while(first<last&&(b[first]==' '||b[first]=='\t'))first++;
        while(last>first&&(b[last-1]==' '||b[last-1]=='\t'||b[last-1]=='\r'||b[last-1]=='\n'))last--;
        unsigned radix=octal?8:10;
        for(size_t i=first;i<last;i++){unsigned d=b[i]-'0';if(d>=radix||v>(UINT64_MAX-d)/radix)return xx_data_struct_record_set_comment(&s->current_record,L"Invalid or overflowing numeric encoding; original bytes preserved");v=v*radix+d;}
    }
    bool hex=(field->property&(SX_ID|SX_FLAGS|SX_SIGNATURE|SX_CHECKSUM|SX_OFFSET|SX_POINTER|SX_ADDRESS))!=0;
    int n=xx_rt_snprintf(a,sizeof(a),hex?"0x%llX":"%llu",(unsigned long long)v);
    if(n<0||n>=(int)sizeof(a))return false;for(int i=0;i<=n;i++)display[i]=(unsigned char)a[i];
    xx_var_set_u64(&s->current_record.value,v);
    return xx_data_struct_record_set_raw_bytes(&s->current_record,s->format->device,s->parent_struct.offset+field->rel_offset,field->size)&&xx_data_struct_record_set_display_value(&s->current_record,display)&&(!(field->property&SX_SIZE)||sx_size_comment(&s->current_record,v));
}
