/* Private ZIP! GEMDOS DATA archive adapter; no executable code is run. */
#ifndef XX_ATARI_ZIP_H
#define XX_ATARI_ZIP_H
static int sfx_atari_zip_ensure(xx_sfx_zipcentral*r,xx_pd_struct*pd)
{
 Abstractformat*f=&r->format;uint8_t h[64],local[4];uint64_t text,data,program,relocation;int64_t total=pm_available(f),start,end,eocd=-1,directory=-1,actual=0,patch=-1;uint32_t stored=0;uint16_t entries=0,comment=0;unsigned tries;bool initialized=false,ok=false;size_t relocations=0;sfx_carrier_view*view=NULL;xx_archive_record_state*st=NULL;
 if(total<64||!pm_read(f,0,h,sizeof(h))||h[0]!=0x60||h[1]!=0x1a||h[28]!=0x2a||h[29]!=0x7c||h[34]!=0x4e||h[35]!=0xf9||xx_rt_memcmp(h+44,"ZIP!",4))return 0;
 text=xx_data_get_u32(h+2,4,0,true);data=xx_data_get_u32(h+6,4,0,true);program=28U+text+data;
 if(carrier_stop(pd)||text<20U||(text&1U)||(data&1U)||data<22U||program>(uint64_t)total||xx_data_get_u32(h+14,4,0,true)||xx_data_get_u32(h+18,4,0,true)||xx_data_get_u16(h+26,2,0,true)||total-(int64_t)program<4||total-(int64_t)program>1024*1024)goto done;
 start=28+(int64_t)text;end=(int64_t)program;
 if(!pm_read(f,start,local,4)||(xx_rt_memcmp(local,"PK\3\4",4)&&xx_rt_memcmp(local,"PK\5\6",4)))goto done;
 if(!pm_read(f,(int64_t)program,local,4))goto done;
 relocation=xx_data_get_u32(local,4,0,true);program+=4;
 if(relocation){
  if((relocation&1U)||relocation>text+data-4U)goto done;
  for(;;){uint8_t delta;if(carrier_stop(pd)||program>=(uint64_t)total||++relocations>1048576U||!pm_read(f,(int64_t)program++,&delta,1))goto done;if(!delta)break;relocation+=delta==1U?254U:delta;if(relocation>text+data-4U||(delta!=1U&&(relocation&1U)))goto done;}
 }
 if(program!=(uint64_t)total)goto done;
 for(tries=0;tries<2&&eocd<0;tries++){
  int64_t stop=end-(int64_t)tries,begin=stop-start>65557?stop-65557:start;size_t size,i;uint8_t*tail;
  if(tries){uint8_t pad;if(!pm_read(f,end-1,&pad,1)||pad!=0)break;}
  if(stop-begin<22)continue;
  size=(size_t)(stop-begin);tail=(uint8_t*)xx_mem_alloc(size);if(!tail)goto done;if(!pm_read(f,begin,tail,size)){xx_mem_free(tail);goto done;}
  for(i=size-21;i>0;i--){size_t at=i-1;if(xx_rt_memcmp(tail+at,"PK\5\6",4)||at+22U+xx_data_get_u16(tail+at+20,2,0,false)!=size)continue;
   if(xx_data_get_u16(tail+at+4,2,0,false)||xx_data_get_u16(tail+at+6,2,0,false)||xx_data_get_u16(tail+at+8,2,0,false)!=xx_data_get_u16(tail+at+10,2,0,false)||xx_data_get_u16(tail+at+10,2,0,false)>4096U)continue;
   eocd=begin+(int64_t)at;stored=xx_data_get_u32(tail+at+12,4,0,false);directory=start+xx_data_get_u32(tail+at+16,4,0,false);entries=xx_data_get_u16(tail+at+10,2,0,false);comment=xx_data_get_u16(tail+at+20,2,0,false);end=stop;break;
  }
  xx_mem_free(tail);
 }
 if(eocd<0||directory<start||directory>eocd)goto done;
 {int64_t cursor=directory;unsigned i;for(i=0;i<entries;i++){uint8_t central[46];uint64_t length;if(carrier_stop(pd)||eocd-cursor<46||!pm_read(f,cursor,central,sizeof(central))||xx_rt_memcmp(central,"PK\1\2",4))goto done;length=46U+(uint64_t)xx_data_get_u16(central+28,2,0,false)+xx_data_get_u16(central+30,2,0,false)+xx_data_get_u16(central+32,2,0,false);if(length>(uint64_t)(eocd-cursor))goto done;cursor+=(int64_t)length;}if(cursor!=eocd)goto done;}
 actual=eocd-directory;
 if((uint64_t)actual!=stored){if(!comment||(uint64_t)actual+comment!=stored)goto done;patch=eocd-start+12;}
 view=(sfx_carrier_view*)xx_mem_alloc(sizeof(*view));if(!view)goto done;sfx_carrier_view_init(view,f->device,f->base_address+start,end-start);view->patch_offset=patch;view->patch_value=(uint32_t)actual;xx_zip_init(&r->inner,&view->device,0);initialized=true;
 if(!xx_zip_handle_base_info(&r->inner.format,pd)||r->inner.format.format_size!=end-start||r->inner.format.number_of_archive_records!=entries)goto done;
 st=xx_zip_create_archive_records_reading(&r->inner.format,NULL,pd);if(!st)goto done;
 {const xx_archive_record*member;uint64_t count=0;while((member=xx_zip_get_current_archive_record(&r->inner.format,st))!=NULL){if(carrier_stop(pd)||++count>4096U||member->compressed_size<0||(member->data_offset<0&&!xx_zip_resolve_record_data_offset(&r->inner,&st->current_record))||member->data_offset<0||member->data_offset>end-start||member->compressed_size>end-start-member->data_offset)goto done;if(!xx_zip_archive_record_move_to_next(&r->inner.format,st,pd))break;}if(count!=entries||r->inner.directory_damaged)goto done;}
 xx_zip_free_archive_records_reading(&r->inner.format,st);st=NULL;r->relative_view=view;view=NULL;r->inner_ready=true;ok=true;
done:
 if(st)xx_zip_free_archive_records_reading(&r->inner.format,st);
 if(!ok&&initialized)xx_zip_destroy(&r->inner);
 xx_mem_free(view);
 if(!ok)f->priv=(void*)&sfx_zip_resource_invalid;
 return ok?1:-1;
}
#endif
