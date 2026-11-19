/* Exact physical one's-complement scalars and bounded fixed byte strings.
 * Decoded values are views of the original physical bytes; raw_value is retained.
 * Integer byte order is explicit and independent of the enclosing reader. */
static bool sx_decode_ones_complement(xx_data_struct_record_state*s,const xx_data_struct_field_desc*f)
{
    if(!f||!f->type)return true;
    bool str=!xx_rt_wcsncmp(f->type,L"ones_char[",10);unsigned width=0;
    if(!xx_rt_wcscmp(f->type,L"uint8_ones"))width=1;
    else if(!xx_rt_wcscmp(f->type,L"uint16_ones_le"))width=2;
    else if(!xx_rt_wcscmp(f->type,L"uint32_ones_le"))width=4;
    if(!str&&!width)return true;
    if(str){uint64_t n=0;size_t i=10;bool digit=false;for(;f->type[i]>=L'0'&&f->type[i]<=L'9';i++){unsigned d=(unsigned)(f->type[i]-L'0');if(n>(UINT64_MAX-d)/10)return false;n=n*10+d;digit=true;}if(!digit||f->type[i]!=L']'||f->type[i+1]||n<1||n>4096||f->size!=(int64_t)n)return false;}
    if(!s||!s->format||!s->format->device||f->size<1||(!str&&f->size!=(int64_t)width)||s->parent_struct.offset<0||f->rel_offset<0||f->rel_offset>INT64_MAX-s->parent_struct.offset||s->parent_struct.entry_size<f->size||f->rel_offset>s->parent_struct.entry_size-f->size)return false;
    uint8_t b[4097];int64_t at=s->parent_struct.offset+f->rel_offset;if(!xx_io_read_at(s->format->device,at,b,(size_t)f->size))return false;
    for(int64_t i=0;i<f->size;i++)b[i]=(uint8_t)~b[i];b[f->size]=0;
    if(!xx_data_struct_record_set_raw_bytes(&s->current_record,s->format->device,at,f->size))return false;
    if(str){wchar_t text[4097];size_t n=0;while(n<(size_t)f->size&&b[n]){text[n]=b[n]>=32&&b[n]!=127?(wchar_t)b[n]:L'.';n++;}text[n]=0;return xx_var_set_str(&s->current_record.value,(char*)b)&&xx_data_struct_record_set_display_value(&s->current_record,text);}
    uint64_t v=0;for(unsigned i=0;i<width;i++)v|=(uint64_t)b[i]<<(8*i);xx_var_set_u64(&s->current_record.value,v);
    char a[40];wchar_t d[40];bool hex=(f->property&(SX_ID|SX_FLAGS|SX_SIGNATURE|SX_CHECKSUM|SX_OFFSET|SX_POINTER|SX_ADDRESS))!=0;int n=xx_rt_snprintf(a,sizeof(a),hex?"0x%llX":"%llu",(unsigned long long)v);if(n<0||n>=(int)sizeof(a))return false;for(int i=0;i<=n;i++)d[i]=(unsigned char)a[i];
    return xx_data_struct_record_set_display_value(&s->current_record,d)&&(!(f->property&SX_SIZE)||sx_size_comment(&s->current_record,v));
}
