/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* Private NE ZIP central size including the22-byte end-record adapter. */
#ifndef XX_NE_ZIP_H
#define XX_NE_ZIP_H
static bool sfx_ne_zip_sections(Abstractformat*f,uint32_t ne,int64_t low,xx_pd_struct*pd)
{
 uint8_t h[64],row[8];uint16_t count,table,shift;unsigned i;bool present=false;
 if(ne>(uint64_t)low||low-ne<64||!pm_read(f,ne,h,sizeof(h)))return false;
 count=xx_data_get_u16(h+28,2,0,false);table=xx_data_get_u16(h+34,2,0,false);shift=xx_data_get_u16(h+50,2,0,false);
 if(!count||count>4096U||table<64U||shift>15U||(uint64_t)ne+table+(uint64_t)count*8U>(uint64_t)low)return false;
 for(i=0;i<count;i++){
  uint64_t begin,length;
  if(carrier_stop(pd)||!pm_read(f,(int64_t)ne+table+i*8U,row,sizeof(row)))return false;
  begin=(uint64_t)xx_data_get_u16(row,2,0,false)<<shift;if(!begin)continue;
  length=xx_data_get_u16(row+2,2,0,false);if(!length)length=65536U;
  if(begin<(uint64_t)ne+64U||begin>(uint64_t)low||length>(uint64_t)low-begin)return false;
  present=true;
 }
 return present;
}
static int sfx_ne_zip_ensure(xx_sfx_zipcentral*r,xx_pd_struct*pd)
{
 Abstractformat*f=&r->format;uint8_t h[64],magic[2],end[22];int64_t size=pm_available(f),eocd,directory,low,first,legacy;uint32_t ne,declared,offset;uint16_t count;wchar_t legacy_name[256];sfx_carrier_view*view=NULL;bool initialized=false,ok=false;
 if(size<86||!pm_read(f,0,h,64)||h[0]!='M'||h[1]!='Z')return 0;
 ne=xx_data_get_u32(h+60,4,0,false);if(ne<64U||ne>(uint64_t)size-2U||!pm_read(f,ne,magic,2)||magic[0]!='N'||magic[1]!='E')return 0;
 eocd=size-22;if(!pm_read(f,eocd,end,sizeof(end))||xx_rt_memcmp(end,"PK\5\6",4)||xx_data_get_u16(end+4,2,0,false)||xx_data_get_u16(end+6,2,0,false)||xx_data_get_u16(end+20,2,0,false))return 0;
 count=xx_data_get_u16(end+10,2,0,false);declared=xx_data_get_u32(end+12,4,0,false);offset=xx_data_get_u32(end+16,4,0,false);directory=offset;
 if(!count||count>4096U||count!=xx_data_get_u16(end+8,2,0,false)||directory>eocd||declared!=(uint64_t)(eocd-directory)+22U)return 0;
 if(carrier_stop(pd)||!executable_carrier_carrier(f,&low,false,false,pd)||!sfx_ne_zip_sections(f,ne,low,pd)||!sfx_carrier_absolute_directory(f,low,directory,eocd,count,&first,&legacy,legacy_name,pd)||legacy>=0)goto done;
 view=(sfx_carrier_view*)xx_mem_alloc(sizeof(*view));if(!view)goto done;sfx_carrier_view_init(view,f->device,f->base_address,size);view->patch_offset=eocd+12;view->patch_value=(uint32_t)(eocd-directory);xx_zip_init(&r->inner,&view->device,0);r->inner.allow_leading_data=true;initialized=true;
 if(!xx_zip_handle_base_info(&r->inner.format,pd)||r->inner.format.format_size!=size||r->inner.format.number_of_archive_records!=count||r->inner.cd_offset!=directory||r->inner.eocd_offset!=eocd||r->inner.directory_damaged)goto done;
 r->relative_view=view;view=NULL;r->inner_ready=true;ok=true;
done:
 if(!ok&&initialized)xx_zip_destroy(&r->inner);
 xx_mem_free(view);
 if(!ok)f->priv=(void*)&sfx_zip_resource_invalid;
 return ok?1:-1;
}
#endif
