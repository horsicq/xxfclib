/* Private named PE ZIP resource adapter for the existing ZIP view. */
#ifndef XX_ZIP_PE_RESOURCE_H
#define XX_ZIP_PE_RESOURCE_H
#include "../../executable/pe/xx_pe_resources_internal.h"
static const unsigned char sfx_zip_resource_invalid=0;
static int sfx_zip_resource_ensure(xx_sfx_zipcentral*r,xx_pd_struct*pd)
{
 Abstractformat*f=&r->format;xx_pe pe;xx_resource_state*resources=NULL;sfx_carrier_view*view=NULL;bool recognized=false,initialized=false,ok=false;unsigned count=0;uint8_t h[64],magic[4];int64_t total=xx_io_total_size(f->device);uint32_t pe_at;
 if(f->priv==&sfx_zip_resource_invalid)return -1;
 if(pm_available(f)<64||!pm_read(f,0,h,64)||h[0]!='M'||h[1]!='Z')return 0;
 pe_at=xx_data_get_u32(h+60,4,0,false);if(pe_at<64U||pe_at>(uint64_t)pm_available(f)-4U||!pm_read(f,pe_at,magic,4)||xx_rt_memcmp(magic,"PE\0\0",4))return 0;
 xx_pe_init(&pe,f->device,f->base_address);resources=pe_resource_stream_create(&pe.format,pd);if(!resources)goto done;
 for(;;){
  const xx_resource_record*row=pe_resource_stream_current(&pe.format,resources);if(!row)break;
  if(++count>4096U||carrier_stop(pd))goto done;
  if((row->type_id==10U&&((row->name&&!xx_rt_strcmp(row->name,"ZIPFILE"))||(!row->name&&row->name_id<=65535U)))||
     (row->type_name&&!xx_rt_strcmp(row->type_name,"ZIP")&&!row->name&&row->name_id<=65535U)||
     (row->type_name&&!xx_rt_strcmp(row->type_name,"STYLE")&&row->name&&!xx_rt_strcmp(row->name,"XAML_STYLE"))){
   xx_archive_record_state*st;const xx_archive_record*member;uint64_t seen=0;bool good=true;
   if(row->size<0||row->offset<f->base_address||row->offset>total||row->size>total-row->offset)goto done;
   if(row->size<4)goto next;
   if(!xx_io_read_at(f->device,row->offset,magic,4))goto done;
   if(xx_rt_memcmp(magic,"PK\3\4",4)&&xx_rt_memcmp(magic,"PK\5\6",4))goto next;
   recognized=true;if(row->size<22||initialized||row->size>256*1024*1024)goto done;
   view=(sfx_carrier_view*)xx_mem_alloc(sizeof(*view));if(!view)goto done;sfx_carrier_view_init(view,f->device,row->offset,row->size);xx_zip_init(&r->inner,&view->device,0);initialized=true;
   if(!xx_zip_handle_base_info(&r->inner.format,pd)||r->inner.format.format_size!=row->size||r->inner.format.number_of_archive_records>4096U)goto done;
   st=xx_zip_create_archive_records_reading(&r->inner.format,NULL,pd);if(!st)goto done;
   while((member=xx_zip_get_current_archive_record(&r->inner.format,st))!=NULL){
    if(carrier_stop(pd)||++seen>4096U||member->compressed_size<0||(member->data_offset<0&&!xx_zip_resolve_record_data_offset(&r->inner,&st->current_record))||member->data_offset<0||member->data_offset>row->size||member->compressed_size>row->size-member->data_offset){good=false;break;}
    if(!xx_zip_archive_record_move_to_next(&r->inner.format,st,pd))break;
   }
   if(seen!=r->inner.format.number_of_archive_records||r->inner.directory_damaged)good=false;
   xx_zip_free_archive_records_reading(&r->inner.format,st);if(!good)goto done;
  }
next:
  if(!pe_resource_stream_next(&pe.format,resources,pd))break;
 }
 if(resources->failed||!initialized||carrier_stop(pd))goto done;
 r->relative_view=view;view=NULL;r->inner_ready=true;ok=true;
done:
 if(resources)pe_resource_stream_free(&pe.format,resources);
 xx_pe_destroy(&pe);
 if(!ok&&initialized)xx_zip_destroy(&r->inner);
 xx_mem_free(view);
 if(!ok&&recognized)f->priv=(void*)&sfx_zip_resource_invalid;
 return ok?1:(recognized||carrier_stop(pd)?-1:0);
}
#endif
