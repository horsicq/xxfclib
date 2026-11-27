/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* Private complete DOS PKLITE ZIP with a stale central-directory disk index. */
#ifndef XX_DOS_ZIP_DISK_H
#define XX_DOS_ZIP_DISK_H
static int sfx_dos_zip_disk_ensure(xx_sfx_zipcentral*r,xx_pd_struct*pd)
{
 Abstractformat*f=&r->format;uint8_t h[64],end[22];int64_t size=pm_available(f),eocd,directory,low,first,legacy;uint16_t count;uint32_t bytes;wchar_t name[256];sfx_carrier_view*view=NULL;bool initialized=false,ok=false;
 if(size<86||!pm_read(f,0,h,sizeof(h))||h[0]!='M'||h[1]!='Z'||xx_rt_memcmp(h+30,"PKLITE",6))return 0;
 eocd=size-22;if(!pm_read(f,eocd,end,sizeof(end))||xx_rt_memcmp(end,"PK\5\6",4)||xx_data_get_u16(end+4,2,0,false)||xx_data_get_u16(end+6,2,0,false)!=1U||xx_data_get_u16(end+20,2,0,false))return 0;
 count=xx_data_get_u16(end+10,2,0,false);bytes=xx_data_get_u32(end+12,4,0,false);directory=xx_data_get_u32(end+16,4,0,false);
 if(!count||count>4096U||count!=xx_data_get_u16(end+8,2,0,false)||directory>eocd||bytes!=(uint64_t)(eocd-directory))return 0;
 if(carrier_stop(pd)||!executable_carrier_carrier(f,&low,false,false,pd)||!sfx_carrier_absolute_directory(f,low,directory,eocd,count,&first,&legacy,name,pd)||first!=low||legacy>=0)goto done;
 view=(sfx_carrier_view*)xx_mem_alloc(sizeof(*view));if(!view)goto done;sfx_carrier_view_init(view,f->device,f->base_address,size);view->patch_offset=eocd+6;view->patch_value=(uint32_t)count<<16;xx_zip_init(&r->inner,&view->device,0);r->inner.allow_leading_data=true;initialized=true;
 if(!xx_zip_handle_base_info(&r->inner.format,pd)||r->inner.format.format_size!=size||r->inner.format.number_of_archive_records!=count||r->inner.cd_offset!=directory||r->inner.eocd_offset!=eocd||r->inner.directory_damaged)goto done;
 r->relative_view=view;view=NULL;r->inner_ready=true;ok=true;
done:
 if(!ok&&initialized)xx_zip_destroy(&r->inner);
 xx_mem_free(view);
 if(!ok)f->priv=(void*)&sfx_zip_resource_invalid;
 return ok?1:-1;
}
#endif
