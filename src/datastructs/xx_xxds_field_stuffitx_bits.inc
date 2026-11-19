/* SPDX-License-Identifier: MIT. Opt-in StuffItX P2 and physical LSB windows.
 * The encoded bytes are retained. Adjacent bit fields can share physical bytes. */
static bool sx_stuffitx_p2_type(const wchar_t*t,unsigned*start,unsigned*width)
{
 if(!t||xx_rt_wcsncmp(t,L"stuffitx_p2_",12))return false;
 size_t k=12;if(t[k]<L'0'||t[k]>L'7')return false;*start=(unsigned)(t[k++]-L'0');if(t[k++]!=L'[')return false;
 unsigned n=0;size_t first=k;for(;t[k]>=L'0'&&t[k]<=L'9';k++){unsigned d=(unsigned)(t[k]-L'0');if(n>(17U-d)/10U)return false;n=n*10U+d;}
 if(k==first||!n||n>17||t[k]!=L']'||t[k+1])return false;*width=n;return true;
}
static bool sx_stuffitx_p2_value(const uint8_t*b,unsigned bytes,unsigned start,uint64_t*out,unsigned*used)
{
 if(!b||!out||!used||!bytes||bytes>17||start>7)return false;unsigned at=start,ones=1,pos=0;uint64_t value=0;
 for(;;){if(at>=bytes*8)return false;unsigned bit=(b[at/8]>>(at%8))&1U;at++;if(!bit)break;if(++ones>=64)return false;}
 while(ones){if(at>=bytes*8||pos>=64)return false;unsigned bit=(b[at/8]>>(at%8))&1U;at++;if(bit){value|=UINT64_C(1)<<pos;ones--;}pos++;}
 if((at+7)/8!=bytes||!value)return false;*out=value-1;*used=at-start;return true;
}
static bool sx_decode_stuffitx_bits(xx_data_struct_record_state*s,const xx_data_struct_field_desc*f)
{
 if(!f||!f->type)return true;const wchar_t*t=f->type;bool p2=!xx_rt_wcsncmp(t,L"stuffitx_p2_",12),lsb=!xx_rt_wcsncmp(t,L"lsb_bits[",9);if(!p2&&!lsb)return true;
 unsigned start=0,bytes=0,width=0;if(p2){if(!sx_stuffitx_p2_type(t,&start,&bytes))return false;}else{
  size_t k=9;if(t[k]<L'0'||t[k]>L'7')return false;start=(unsigned)(t[k++]-L'0');if(t[k++]!=L',')return false;size_t first=k;
  for(;t[k]>=L'0'&&t[k]<=L'9';k++){unsigned d=(unsigned)(t[k]-L'0');if(width>(64U-d)/10U)return false;width=width*10U+d;}
  if(k==first||!width||width>64||t[k]!=L']'||t[k+1])return false;bytes=(start+width+7)/8;
 }
 if(!s||!s->format||!s->format->device||f->size!=(int64_t)bytes||s->parent_struct.offset<0||f->rel_offset<0||f->rel_offset>INT64_MAX-s->parent_struct.offset||s->parent_struct.entry_size<f->size||f->rel_offset>s->parent_struct.entry_size-f->size)return false;
 uint8_t raw[17];int64_t at=s->parent_struct.offset+f->rel_offset;if(!xx_io_read_at(s->format->device,at,raw,bytes))return false;uint64_t value=0;
 if(p2){unsigned used;if(!sx_stuffitx_p2_value(raw,bytes,start,&value,&used))return false;}else for(unsigned i=0;i<width;i++)value|=(uint64_t)((raw[(start+i)/8]>>((start+i)%8))&1U)<<i;
 xx_var_set_u64(&s->current_record.value,value);char narrow[40];wchar_t display[40];bool hex=(f->property&(SX_ID|SX_FLAGS|SX_SIGNATURE|SX_CHECKSUM|SX_OFFSET|SX_POINTER|SX_ADDRESS))!=0;int n=xx_rt_snprintf(narrow,sizeof(narrow),hex?"0x%llX":"%llu",(unsigned long long)value);if(n<0||n>=(int)sizeof(narrow))return false;for(int i=0;i<=n;i++)display[i]=(unsigned char)narrow[i];
 return xx_data_struct_record_set_raw_bytes(&s->current_record,s->format->device,at,bytes)&&xx_data_struct_record_set_display_value(&s->current_record,display)&&(!(f->property&SX_SIZE)||sx_size_comment(&s->current_record,value));
}
