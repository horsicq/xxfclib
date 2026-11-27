/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* Private bounded RAR1.4 header adapter. No header checksum exists here. */
#ifndef XX_RAR14_INTERNAL_H
#define XX_RAR14_INTERNAL_H
static bool xx_rar14_parse_block(xx_io_device *d,int64_t total,int64_t at,xx_rar_block *b)
{
 uint8_t h[21];uint16_t hs;unsigned j;uint32_t packed=0;bool main;
 if(!d||!b||!xx_rar_range_valid(total,at,7)||!xx_rar_read_exact_at(d,at,h,7))return false;
 main=xx_mem_compare(h,"RE~^",4)==0;
 if(main){hs=xx_data_get_u16(h+4,2,0,false);if(hs<7||(h[6]&1U))return false;}
 else{
  if(!xx_rar_range_valid(total,at,21)||!xx_rar_read_exact_at(d,at,h,21))return false;
  hs=xx_data_get_u16(h+10,2,0,false);packed=xx_data_get_u32(h,4,0,false);
  if(h[19]==0||hs!=21U+h[19]||(h[17]&15U)||(h[18]!=1U&&h[18]!=2U)||h[20]>5U)return false;
  if((h[16]&16U)&&(packed||xx_data_get_u32(h+4,4,0,false)))return false;
 }
 if(!xx_rar_range_valid(total,at,hs)||packed>(uint64_t)(total-at-hs))return false;
 if(!main)for(j=0;j<h[19];j++)if(xx_io_get_u8(d,at+21+j)==0)return false;
 xx_mem_zero(b,sizeof(*b));b->version=XX_RAR_VERSION_14;b->offset=at;b->header_size=hs;b->data_offset=at+hs;b->data_size=packed;b->next_offset=at+hs+packed;b->type=main?XX_RAR4_HEADER_MAIN:XX_RAR4_HEADER_FILE;b->flags=main?h[6]:(uint32_t)h[17]|XX_RAR4_FLAG_LONG_BLOCK;return true;
}
static bool xx_rar14_file_info(xx_io_device*d,const xx_rar_block*b,xx_rar_file_info*i)
{
 uint8_t h[21];if(!d||!b||!i||b->type!=XX_RAR4_HEADER_FILE||!xx_rar_read_exact_at(d,b->offset,h,21))return false;
 xx_mem_zero(i,sizeof(*i));i->block=*b;i->unpacked_size=xx_data_get_u32(h+4,4,0,false);i->attributes=h[16];i->timestamp=xx_data_get_u32(h+12,4,0,false);i->data_crc=xx_data_get_u16(h+8,2,0,false);i->has_crc=true;i->file_flags=h[17];i->method=0x30U+h[20];i->unpack_version=h[18]==2U?13U:10U;i->name_offset=b->offset+21;i->name_size=h[19];i->is_folder=(h[16]&16U)!=0;
 return xx_rar_normalize_codec(i);
}
#endif
