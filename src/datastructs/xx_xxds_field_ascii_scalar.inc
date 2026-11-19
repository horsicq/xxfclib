/* Bounded, locale-independent physical ASCII signed integers and decimal reals.
 * Exact types opt in; malformed or unrepresentable values keep their raw bytes. */
static bool sx_decode_ascii_scalar(xx_data_struct_record_state*s,const xx_data_struct_field_desc*f)
{
 bool signed_number=f&&f->type&&!xx_rt_wcsncmp(f->type,L"ascii_signed[",13);
 bool real=f&&f->type&&!xx_rt_wcsncmp(f->type,L"ascii_real[",11);
 if(!signed_number&&!real)return true;
 size_t k=signed_number?13:11;uint64_t width=0;bool digit=false;
 for(;f->type[k]>=L'0'&&f->type[k]<=L'9';k++){unsigned d=(unsigned)(f->type[k]-L'0');if(width>(UINT64_MAX-d)/10)return false;width=width*10+d;digit=true;}
 if(!digit||f->type[k]!=L']'||f->type[k+1]||!width||width>128||f->size!=(int64_t)width||!s||!s->format||!s->format->device||s->parent_struct.offset<0||f->rel_offset<0||f->rel_offset>INT64_MAX-s->parent_struct.offset||s->parent_struct.entry_size<f->size||f->rel_offset>s->parent_struct.entry_size-f->size)return false;
 char bytes[129],display[96];wchar_t wide[96];int64_t at=s->parent_struct.offset+f->rel_offset;
 if(!xx_io_read_at(s->format->device,at,bytes,(size_t)width))return false;bytes[width]=0;
 size_t first=0,end=(size_t)width;while(first<end&&(bytes[first]==' '||bytes[first]=='\t'||bytes[first]=='\r'||bytes[first]=='\n'))first++;while(end>first&&(bytes[end-1]==' '||bytes[end-1]=='\t'||bytes[end-1]=='\r'||bytes[end-1]=='\n'))end--;bytes[end]=0;
 size_t p=first;bool negative=p<end&&bytes[p]=='-';if(p<end&&(bytes[p]=='-'||bytes[p]=='+'))p++;size_t mantissa=p;unsigned digits=0;bool nonzero=false;
 uint64_t magnitude=0,limit=negative?UINT64_C(0x8000000000000000):UINT64_C(0x7fffffffffffffff);
 while(p<end&&bytes[p]>='0'&&bytes[p]<='9'){unsigned d=(unsigned)(bytes[p++]-'0');digits++;nonzero|=d!=0;if(signed_number){if(magnitude>(limit-d)/10)goto invalid;magnitude=magnitude*10+d;}}
 if(real&&p<end&&bytes[p]=='.'){p++;while(p<end&&bytes[p]>='0'&&bytes[p]<='9'){nonzero|=bytes[p]!='0';digits++;p++;}}
 if(!digits||p==mantissa)goto invalid;
 if(real&&p<end&&(bytes[p]=='e'||bytes[p]=='E')){p++;if(p<end&&(bytes[p]=='+'||bytes[p]=='-'))p++;size_t exponent=p;while(p<end&&bytes[p]>='0'&&bytes[p]<='9')p++;if(p==exponent)goto invalid;}
 if(p!=end)goto invalid;
 int n;
 if(real){const char*tail=NULL;double value=xx_rt_strtod(bytes+first,&tail);uint64_t bits;if(sizeof(value)!=sizeof(bits))goto invalid;xx_rt_memcpy(&bits,&value,sizeof(bits));if(tail!=bytes+end||(bits&UINT64_C(0x7ff0000000000000))==UINT64_C(0x7ff0000000000000)||(nonzero&&value==0))goto invalid;xx_var_set_double(&s->current_record.value,value);n=xx_rt_snprintf(display,sizeof(display),"%.17g",value);}
 else{int64_t value=negative?(magnitude==UINT64_C(0x8000000000000000)?INT64_MIN:-(int64_t)magnitude):(int64_t)magnitude;xx_var_set_i64(&s->current_record.value,value);bool hex=(f->property&(SX_ID|SX_FLAGS|SX_SIGNATURE|SX_CHECKSUM|SX_OFFSET|SX_POINTER|SX_ADDRESS))!=0;if(hex)n=xx_rt_snprintf(display,sizeof(display),negative&&magnitude?"-0x%llX":"0x%llX",(unsigned long long)magnitude);else n=xx_rt_snprintf(display,sizeof(display),"%lld",(long long)value);}
 if(n<0||n>=(int)sizeof(display))return false;for(int i=0;i<=n;i++)wide[i]=(unsigned char)display[i];
 return xx_data_struct_record_set_raw_bytes(&s->current_record,s->format->device,at,f->size)&&xx_data_struct_record_set_display_value(&s->current_record,wide)&&(!signed_number||!(f->property&SX_SIZE)||xx_var_get_i64(&s->current_record.value)<0||sx_size_comment(&s->current_record,xx_var_get_u64(&s->current_record.value)));
invalid:
 return xx_data_struct_record_set_comment(&s->current_record,L"Invalid or unrepresentable ASCII number; original bytes preserved");
}
