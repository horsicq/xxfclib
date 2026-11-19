/* SPDX-License-Identifier: MIT
 * Explicit UTF-8 fields retain bytes and use Unicode display without changing
 * the established single-byte semantics of legacy char[N] fields. */
static bool sx_decode_utf8_field(xx_data_struct_record_state *s,const xx_data_struct_field_desc *f)
{
    if(!f||!f->type||xx_rt_wcsncmp(f->type,L"utf8[",5))return true;
    uint64_t width=0;size_t p=5;bool digit=false;
    for(;f->type[p]>=L'0'&&f->type[p]<=L'9';p++){unsigned d=(unsigned)(f->type[p]-L'0');if(width>(UINT64_MAX-d)/10)return false;width=width*10+d;digit=true;}
    if(!digit||f->type[p]!=L']'||f->type[p+1]||!width||width>4096||f->size!=(int64_t)width||!s||!s->format||!s->format->device||s->parent_struct.offset<0||f->rel_offset<0||f->rel_offset>INT64_MAX-s->parent_struct.offset||s->parent_struct.entry_size<f->size||f->rel_offset>s->parent_struct.entry_size-f->size)return false;
    char *bytes=xx_mem_alloc((size_t)width+1);wchar_t *wide=xx_mem_alloc(((size_t)width+1)*sizeof(*wide));bool ok=false;
    if(!bytes||!wide)goto done;
    if(!xx_io_read_at(s->format->device,s->parent_struct.offset+f->rel_offset,bytes,(size_t)width))goto done;
    bytes[width]=0;size_t i=0,n=0;
    while(i<(size_t)width){
        uint32_t cp=(uint8_t)bytes[i++],min=0;unsigned extra=0;
        if(cp>=0xc2&&cp<=0xdf){cp&=31;extra=1;min=0x80;}
        else if(cp>=0xe0&&cp<=0xef){cp&=15;extra=2;min=0x800;}
        else if(cp>=0xf0&&cp<=0xf4){cp&=7;extra=3;min=0x10000;}
        else if(cp>=0x80||!cp)goto invalid;
        if(extra>(size_t)width-i)goto invalid;
        while(extra--){unsigned b=(uint8_t)bytes[i++];if((b&0xc0)!=0x80)goto invalid;cp=(cp<<6)|(b&63);}
        if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))goto invalid;
        if(cp<0x20||cp==0x7f)cp='.';
#if WCHAR_MAX < 0x10ffff
        if(cp>=0x10000){cp-=0x10000;wide[n++]=(wchar_t)(0xd800+(cp>>10));wide[n++]=(wchar_t)(0xdc00+(cp&1023));}
        else
#endif
        wide[n++]=(wchar_t)cp;
    }
    wide[n]=0;ok=xx_var_set_str(&s->current_record.value,bytes)&&xx_data_struct_record_set_display_value(&s->current_record,wide);goto done;
invalid:
    ok=xx_data_struct_record_set_comment(&s->current_record,L"Invalid UTF-8 text; original bytes preserved");
done:
    xx_mem_free(bytes);xx_mem_free(wide);return ok;
}
