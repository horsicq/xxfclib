/* SPDX-License-Identifier: MIT
 * Exact opt-in NASTRAN decimal reals: D/d, E/e and implicit signed exponents. Original bytes
 * remain the raw value; this does not change ascii_real or legacy text types. */
static bool sx_decode_nastran_real(xx_data_struct_record_state *s,const xx_data_struct_field_desc *f)
{
 if(!f||!f->type||xx_rt_wcsncmp(f->type,L"ascii_nastran_real[",19))return true;
 size_t k=19;uint64_t width=0;bool digit=false;
 for(;f->type[k]>=L'0'&&f->type[k]<=L'9';k++){unsigned d=(unsigned)(f->type[k]-L'0');if(width>(UINT64_MAX-d)/10)return false;width=width*10+d;digit=true;}
 if(!digit||f->type[k]!=L']'||f->type[k+1]||!width||width>128||f->size!=(int64_t)width||!s||!s->format||!s->format->device||s->parent_struct.offset<0||f->rel_offset<0||f->rel_offset>INT64_MAX-s->parent_struct.offset||s->parent_struct.entry_size<f->size||f->rel_offset>s->parent_struct.entry_size-f->size)return false;
 char bytes[130],display[96];wchar_t wide[96];int64_t at=s->parent_struct.offset+f->rel_offset;
 if(!xx_io_read_at(s->format->device,at,bytes,(size_t)width))return false;bytes[width]=0;
 size_t first=0,end=(size_t)width;while(first<end&&(bytes[first]==' '||bytes[first]=='\t'||bytes[first]=='\r'||bytes[first]=='\n'))first++;while(end>first&&(bytes[end-1]==' '||bytes[end-1]=='\t'||bytes[end-1]=='\r'||bytes[end-1]=='\n'))end--;bytes[end]=0;
 size_t p=first;unsigned digits=0;bool nonzero=false;if(p<end&&(bytes[p]=='-'||bytes[p]=='+'))p++;
 while(p<end&&bytes[p]>='0'&&bytes[p]<='9'){nonzero|=bytes[p]!='0';digits++;p++;}
 if(p<end&&bytes[p]=='.'){p++;while(p<end&&bytes[p]>='0'&&bytes[p]<='9'){nonzero|=bytes[p]!='0';digits++;p++;}}
 if(!digits)goto invalid;
 if(p<end&&(bytes[p]=='+'||bytes[p]=='-')){xx_rt_memmove(bytes+p+1,bytes+p,end-p+1);bytes[p]='e';end++;}
 if(p<end&&(bytes[p]=='e'||bytes[p]=='E'||bytes[p]=='d'||bytes[p]=='D')){bytes[p++]='e';if(p<end&&(bytes[p]=='+'||bytes[p]=='-'))p++;size_t exponent=p;while(p<end&&bytes[p]>='0'&&bytes[p]<='9')p++;if(p==exponent)goto invalid;}
 if(p!=end)goto invalid;
 const char *tail=NULL;double value=xx_rt_strtod(bytes+first,&tail);uint64_t bits;
 if(sizeof(value)!=sizeof(bits))goto invalid;xx_rt_memcpy(&bits,&value,sizeof(bits));
 if(tail!=bytes+end||(bits&UINT64_C(0x7ff0000000000000))==UINT64_C(0x7ff0000000000000)||(nonzero&&value==0))goto invalid;
 int n=xx_rt_snprintf(display,sizeof(display),"%.17g",value);if(n<0||n>=(int)sizeof(display))return false;
 for(int i=0;i<=n;i++)wide[i]=(unsigned char)display[i];
 xx_var_set_double(&s->current_record.value,value);
 return xx_data_struct_record_set_raw_bytes(&s->current_record,s->format->device,at,f->size)&&xx_data_struct_record_set_display_value(&s->current_record,wide);
invalid:
 return xx_data_struct_record_set_comment(&s->current_record,L"Invalid or unrepresentable NASTRAN real; original bytes preserved");
}
