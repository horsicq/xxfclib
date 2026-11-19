/* Explicit Solaris/cpio fixed-width numeric grammar: space/NUL edge padding,
 * hexadecimal or octal digits, blank means zero. Existing types stay strict. */
static bool sx_decode_cpio_integer(xx_data_struct_record_state*s,const xx_data_struct_field_desc*f)
{
 bool hex=f&&f->type&&!xx_rt_wcsncmp(f->type,L"ascii_cpio_hex[",15);
 bool oct=f&&f->type&&!xx_rt_wcsncmp(f->type,L"ascii_cpio_octal[",17);
 if(!hex&&!oct)return true;
 size_t i=hex?15:17;uint64_t width=0,value=0;bool digit=false;
 for(;f->type[i]>=L'0'&&f->type[i]<=L'9';i++){unsigned d=(unsigned)(f->type[i]-L'0');if(width>(UINT64_MAX-d)/10)return false;width=width*10+d;digit=true;}
 if(!digit||f->type[i]!=L']'||f->type[i+1]||!width||width>24||f->size<1||width!=(uint64_t)f->size)return false;
 if(!s||!s->format||!s->format->device||f->rel_offset<0||s->parent_struct.offset<0||s->parent_struct.entry_size<f->size||f->rel_offset>s->parent_struct.entry_size-f->size||f->rel_offset>INT64_MAX-s->parent_struct.offset)return false;
 uint8_t b[24];if(!xx_io_read_at(s->format->device,s->parent_struct.offset+f->rel_offset,b,(size_t)width))return false;
 size_t first=0,last=(size_t)width;while(first<last&&(b[first]==' '||!b[first]))first++;while(last>first&&(b[last-1]==' '||!b[last-1]))last--;
 unsigned radix=hex?16:8;
 for(i=first;i<last;i++){unsigned d=b[i]>='0'&&b[i]<='9'?b[i]-'0':b[i]>='A'&&b[i]<='F'?b[i]-'A'+10:b[i]>='a'&&b[i]<='f'?b[i]-'a'+10:255;if(d>=radix||value>(UINT64_MAX-d)/radix)return xx_data_struct_record_set_comment(&s->current_record,L"Invalid or overflowing CPIO numeric encoding; original bytes preserved");value=value*radix+d;}
 uint32_t mask=SX_ID|SX_FLAGS|SX_SIGNATURE|SX_OFFSET|SX_POINTER|SX_ADDRESS;
#ifdef SX_CHECKSUM
 mask|=SX_CHECKSUM;
#endif
 char a[40];wchar_t text[40];int n=xx_rt_snprintf(a,sizeof(a),(f->property&mask)?"0x%llX":"%llu",(unsigned long long)value);if(n<0||n>=(int)sizeof(a))return false;for(int j=0;j<=n;j++)text[j]=(unsigned char)a[j];
 xx_var_set_u64(&s->current_record.value,value);
 return xx_data_struct_record_set_raw_bytes(&s->current_record,s->format->device,s->parent_struct.offset+f->rel_offset,f->size)&&xx_data_struct_record_set_display_value(&s->current_record,text)&&(!(f->property&SX_SIZE)||sx_size_comment(&s->current_record,value));
}
