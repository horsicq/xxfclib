/* DSL2 directory storage: each original byte has 0x33 added modulo256.
 * These explicit opt-in types decode only physical bytes of their own field.
 * raw_value retains the original stored representation. */
static bool sx_decode_bias33(xx_data_struct_record_state*s,const xx_data_struct_field_desc*f)
{
 if(!f||!f->type)return true;
 bool string=!xx_rt_wcsncmp(f->type,L"bias33_char",11),signed32=!xx_rt_wcsncmp(f->type,L"int32_bias33",12),u16=!xx_rt_wcsncmp(f->type,L"uint16_bias33",13);
 if(!string&&!signed32&&!u16)return true;
 uint64_t width=0;
 if(string){size_t i=11;bool digit=false;if(f->type[i++]!=L'[')return false;while(f->type[i]>=L'0'&&f->type[i]<=L'9'){unsigned d=(unsigned)(f->type[i++]-L'0');if(width>(UINT64_MAX-d)/10)return false;width=width*10+d;digit=true;}if(!digit||f->type[i]!=L']'||f->type[i+1]||width<1||width>4096)return false;}
 else if(signed32){if(xx_rt_wcscmp(f->type,L"int32_bias33_le"))return false;width=4;}
 else{if(xx_rt_wcscmp(f->type,L"uint16_bias33_le"))return false;width=2;}
 if(!s||!s->format||!s->format->device||f->size!=(int64_t)width||s->parent_struct.offset<0||f->rel_offset<0||f->rel_offset>INT64_MAX-s->parent_struct.offset||s->parent_struct.entry_size<f->size||f->rel_offset>s->parent_struct.entry_size-f->size)return false;
 int64_t at=s->parent_struct.offset+f->rel_offset;uint8_t bytes[4097];if(!xx_io_read_at(s->format->device,at,bytes,(size_t)width))return false;
 for(size_t i=0;i<(size_t)width;i++)bytes[i]=(uint8_t)(bytes[i]-0x33U);bytes[width]=0;
 if(!xx_data_struct_record_set_raw_bytes(&s->current_record,s->format->device,at,f->size))return false;
 if(string){wchar_t display[4097];size_t n=0;while(n<(size_t)width&&bytes[n]){display[n]=bytes[n]>=32&&bytes[n]!=127?(wchar_t)bytes[n]:L'.';n++;}display[n]=0;return xx_var_set_str(&s->current_record.value,(char*)bytes)&&xx_data_struct_record_set_display_value(&s->current_record,display);}
 uint64_t value=0;for(unsigned i=0;i<(unsigned)width;i++)value|=(uint64_t)bytes[i]<<(8*i);
 int64_t signed_value=signed32&&value>=UINT64_C(0x80000000)?(int64_t)value-INT64_C(0x100000000):(int64_t)value;
 if(signed32)xx_var_set_i64(&s->current_record.value,signed_value);else xx_var_set_u64(&s->current_record.value,value);
 bool hex=(f->property&(SX_ID|SX_FLAGS|SX_SIGNATURE|SX_CHECKSUM|SX_OFFSET|SX_POINTER|SX_ADDRESS))!=0;char text[64];wchar_t display[64];int n;
 if(hex&&signed_value<0)n=xx_rt_snprintf(text,sizeof(text),"-0x%llX",(unsigned long long)(-signed_value));
 else if(hex)n=xx_rt_snprintf(text,sizeof(text),"0x%llX",(unsigned long long)value);
 else n=xx_rt_snprintf(text,sizeof(text),"%lld",(long long)signed_value);
 if(n<0||n>=(int)sizeof(text))return false;for(int i=0;i<=n;i++)display[i]=(unsigned char)text[i];
 return xx_data_struct_record_set_display_value(&s->current_record,display)&&(!(f->property&SX_SIZE)||signed_value<0||sx_size_comment(&s->current_record,value));
}
