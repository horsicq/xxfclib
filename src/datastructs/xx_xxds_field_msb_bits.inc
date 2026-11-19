/* Exact opt-in physical MSB-first bit fields. The byte span remains original;
 * start is0..7 from its first byte's MSB, and width is1..64. */
static bool sx_decode_msb_bits(xx_data_struct_record_state*s,const xx_data_struct_field_desc*f)
{
 if(!f||!f->type||xx_rt_wcsncmp(f->type,L"msb_bits[",9))return true;
 const wchar_t*t=f->type;unsigned start,width=0;size_t k=9;
 if(t[k]<L'0'||t[k]>L'7')return false;start=(unsigned)(t[k++]-L'0');if(t[k++]!=L',')return false;size_t first=k;
 for(;t[k]>=L'0'&&t[k]<=L'9';k++){unsigned d=(unsigned)(t[k]-L'0');if(width>(64U-d)/10U)return false;width=width*10U+d;}
 if(k==first||!width||width>64||t[k]!=L']'||t[k+1])return false;unsigned bytes=(start+width+7U)/8U;
 if(!s||!s->format||!s->format->device||f->size!=(int64_t)bytes||s->parent_struct.offset<0||f->rel_offset<0||f->rel_offset>INT64_MAX-s->parent_struct.offset||s->parent_struct.entry_size<f->size||f->rel_offset>s->parent_struct.entry_size-f->size)return false;
 uint8_t raw[9];int64_t at=s->parent_struct.offset+f->rel_offset;if(!xx_io_read_at(s->format->device,at,raw,bytes))return false;
 uint64_t value=0;for(unsigned i=0;i<width;i++){unsigned bit=start+i;value=(value<<1)|((raw[bit/8]>>(7U-bit%8))&1U);}
 xx_var_set_u64(&s->current_record.value,value);char narrow[40];wchar_t display[40];bool hex=(f->property&(SX_ID|SX_FLAGS|SX_SIGNATURE|SX_CHECKSUM|SX_OFFSET|SX_POINTER|SX_ADDRESS))!=0;int n=xx_rt_snprintf(narrow,sizeof(narrow),hex?"0x%llX":"%llu",(unsigned long long)value);if(n<0||n>=(int)sizeof(narrow))return false;for(int i=0;i<=n;i++)display[i]=(unsigned char)narrow[i];
 return xx_data_struct_record_set_raw_bytes(&s->current_record,s->format->device,at,bytes)&&xx_data_struct_record_set_display_value(&s->current_record,display)&&(!(f->property&SX_SIZE)||sx_size_comment(&s->current_record,value));
}
