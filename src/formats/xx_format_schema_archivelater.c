/* SPDX-License-Identifier: MIT. Physical archive metadata. Scope: docs/data_struct_archivelater_scope.txt. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
#include "xx_format_schema_archivelater.h"

static const sx_layout *const sx_archivelater_layouts[] = {
 &sxal_aldus_header,
 &sxal_aldus_timestamp,
 &sxal_aldus_stream,
 &sxal_aldus_block_size,
 &sxal_ampk_header,
 &sxal_ampk_record,
 &sxal_ampk_file,
 &sxal_arx_member,
 &sxal_arx_end,
 &sxal_dos_packit_header,
 &sxal_dos_packit_member,
 &sxal_dos_packit_end,
 &sxal_asar_header,
 &sxal_pea_header,
 &sxal_pea_trigger_le,
 &sxal_pea_stream_le,
 &sxal_pea_block_size_le,
 &sxal_pea_name_size_le,
 &sxal_pea_object_le,
 &sxal_pea_file_size_le,
 &sxal_pea_packed_size_le,
 &sxal_pea_last_size_le,
 &sxal_pea_trigger_be,
 &sxal_pea_stream_be,
 &sxal_pea_block_size_be,
 &sxal_pea_name_size_be,
 &sxal_pea_object_be,
 &sxal_pea_file_size_be,
 &sxal_pea_packed_size_be,
 &sxal_pea_last_size_be,
 &sxal_sit5_header,
 &sxal_sit5_file,
 &sxal_sit5_folder,
 &sxal_sit5_second_v1,
 &sxal_sit5_second_v3,
 &sxal_sit5_resource,
 &sxal_diskdoubler_file,
 &sxal_dda2_header,
 &sxal_dda2_packed,
 &sxal_dda2_folder,
 &sxal_dda2_stored,
 &sxal_dda2_end,
 &sxal_dda2_footer,
 &sxal_dda2_index_word,
 &sxal_ddar_header,
 &sxal_ddar_member,
 &sxal_xpk_header,
 &sxal_xpk_extension_size,
 &sxal_xpk_chunk2,
 &sxal_xpk_chunk4,
 &sxal_ppmd_header,
 &sxal_lrzip_header,
 &sxal_lrzip_chunk1,
 &sxal_lrzip_block1,
 &sxal_lrzip_chunk2,
 &sxal_lrzip_block2,
 &sxal_lrzip_chunk3,
 &sxal_lrzip_block3,
 &sxal_lrzip_chunk4,
 &sxal_lrzip_block4,
 &sxal_lrzip_chunk5,
 &sxal_lrzip_block5,
 &sxal_lrzip_chunk6,
 &sxal_lrzip_block6,
 &sxal_lrzip_chunk7,
 &sxal_lrzip_block7,
 &sxal_lrzip_chunk8,
 &sxal_lrzip_block8,
 &sxal_lrzip_md5,
 &sxal_rzip_header,
 &sxal_rzip_block,
 &sxal_dos_backup_header,
 &sxal_dos_backup2_header,
 &sxal_dos_backup2_directory,
 &sxal_dos_backup2_file,
 &sxal_esp_header,
 &sxal_lpak_header,
 &sxal_uharc_header,
 &sxal_is_stored_signature,
 &sxal_is_stored_marker,
 &sxal_tarx2_signature,
 &sxal_tarx2_marker,
};
static bool sx_archivelater_comment(xx_data_struct_record *r,uint32_t id){
 if(!r||!r->name||id<0x60200000U||id>=0x60210000U)return true;
 const wchar_t *text=NULL;switch(id){
 case 0x6020004eU:
 if(!xx_rt_wcscmp(r->name,L"signature"))text=L"Three-byte UHA container signature; the entropy-coded archive stream begins at offset 4.";
 if(!xx_rt_wcscmp(r->name,L"version"))text=L"Stored UHARC version byte: high nibble is major and low nibble is minor (for example, 0x04 is version 0.4).";
 break;
 case 0x6020004fU:
 if(!xx_rt_wcscmp(r->name,L"signature"))text=L"Four-byte fixed marker in the XOR-A9 stored wrapper, verified by the native reader at offsets 0 and 6.";
 break;
 case 0x60200050U:
 if(!xx_rt_wcscmp(r->name,L"marker"))text=L"Three-byte fixed marker verified by the native reader at offset 12. Bytes 4-5, 10-11 and 15 are left raw.";
 break;
 case 0x60200051U:
 if(!xx_rt_wcscmp(r->name,L"signature"))text=L"Four-byte TARX2 clear-header signature at offset 0, checked by the native transport decoder.";
 break;
 case 0x60200052U:
 if(!xx_rt_wcscmp(r->name,L"marker"))text=L"Four-byte TARX2 clear-header marker at offset 8, checked by the native transport decoder.";
 break;
 case 0x60200002U:
 if(!xx_rt_wcscmp(r->name,L"modified"))text=L"Packed DOS date/time, not a Unix timestamp.";
 break;
 case 0x60200005U:
 if(!xx_rt_wcscmp(r->name,L"directory_count"))text=L"Stored count hint omits the first directory in historical writers.";
 if(!xx_rt_wcscmp(r->name,L"file_count"))text=L"Stored count hint can be inaccurate in historical writers.";
 if(!xx_rt_wcscmp(r->name,L"packed_size_hint"))text=L"Declared total packed-data byte count; historical writers may leave unreliable hints.";
 break;
 case 0x60200006U:
 if(!xx_rt_wcscmp(r->name,L"kind"))text=L"0=directory end, 1=directory start, 2=file.";
 if(!xx_rt_wcscmp(r->name,L"directory_size_plus3"))text=L"Directory records encode their complete subtree byte size plus 3; other record kinds use zero.";
 break;
 case 0x60200007U:
 if(!xx_rt_wcscmp(r->name,L"packed_size_hint"))text=L"Stored files consume unpacked_size bytes regardless of this hint.";
 if(!xx_rt_wcscmp(r->name,L"method"))text=L"0=stored, 1=LZARI, 2=LZSS, 3=LH1.";
 if(!xx_rt_wcscmp(r->name,L"protection"))text=L"Complete big-endian Amiga protection flags; the low byte alone is not the full physical field.";
 break;
 case 0x60200008U:
 if(!xx_rt_wcscmp(r->name,L"header_size_minus2"))text=L"Complete member header byte size is this value plus 2.";
 if(!xx_rt_wcscmp(r->name,L"packed_size"))text=L"Zero means stored, with unpacked_size physical payload bytes.";
 if(!xx_rt_wcscmp(r->name,L"time"))text=L"Packed DOS date/time, not a Unix timestamp.";
 if(!xx_rt_wcscmp(r->name,L"date"))text=L"Packed DOS date/time, not a Unix timestamp.";
 break;
 case 0x6020000bU:
 if(!xx_rt_wcscmp(r->name,L"date"))text=L"Packed DOS date/time, not a Unix timestamp.";
 if(!xx_rt_wcscmp(r->name,L"time"))text=L"Packed DOS date/time, not a Unix timestamp.";
 break;
 case 0x6020000dU:
 if(!xx_rt_wcscmp(r->name,L"header_pickle_size"))text=L"Physical payload starts at byte 8 plus this value.";
 if(!xx_rt_wcscmp(r->name,L"json_size"))text=L"JSON directory byte count excluding alignment padding; its members remain text.";
 break;
 case 0x6020000eU:
 if(!xx_rt_wcscmp(r->name,L"byte_order_flags"))text=L"Bit 7 selects big-endian size fields.";
 break;
 case 0x60200010U:
 if(!xx_rt_wcscmp(r->name,L"compression"))text=L"0=stored; 1..3=zlib block compression.";
 break;
 case 0x60200013U:
 if(!xx_rt_wcscmp(r->name,L"modified"))text=L"Packed DOS date/time, not a Unix timestamp.";
 break;
 case 0x60200018U:
 if(!xx_rt_wcscmp(r->name,L"compression"))text=L"0=stored; 1..3=zlib block compression.";
 break;
 case 0x6020001bU:
 if(!xx_rt_wcscmp(r->name,L"modified"))text=L"Packed DOS date/time, not a Unix timestamp.";
 break;
 case 0x60200020U:
 if(!xx_rt_wcscmp(r->name,L"created"))text=L"Seconds since 1904-01-01, not Unix epoch.";
 if(!xx_rt_wcscmp(r->name,L"modified"))text=L"Seconds since 1904-01-01, not Unix epoch.";
 break;
 case 0x60200021U:
 if(!xx_rt_wcscmp(r->name,L"created"))text=L"Seconds since 1904-01-01, not Unix epoch.";
 if(!xx_rt_wcscmp(r->name,L"modified"))text=L"Seconds since 1904-01-01, not Unix epoch.";
 if(!xx_rt_wcscmp(r->name,L"first_child_offset"))text=L"0xFFFFFFFF marks a folder-end record, which has no second header or payload.";
 break;
 case 0x60200025U:
 if(!xx_rt_wcscmp(r->name,L"data_method"))text=L"Low 7 bits select the fork codec; high bit carries codec-specific flags.";
 if(!xx_rt_wcscmp(r->name,L"resource_method"))text=L"Low 7 bits select the fork codec; high bit carries codec-specific flags.";
 break;
 case 0x6020002cU:
 if(!xx_rt_wcscmp(r->name,L"folder_id_or_record_offset"))text=L"High bit marks a folder id; otherwise this is a record byte offset. A marked folder id is followed by its record offset.";
 break;
 case 0x6020002eU:
 if(!xx_rt_wcscmp(r->name,L"data_size"))text=L"Ignored for folder-end records, which have no payload.";
 if(!xx_rt_wcscmp(r->name,L"resource_size"))text=L"Ignored for folder-end records, which have no payload.";
 break;
 case 0x6020002fU:
 if(!xx_rt_wcscmp(r->name,L"packed_size_minus8"))text=L"Physical stream byte length is this value plus 8.";
 if(!xx_rt_wcscmp(r->name,L"flags"))text=L"Bit 0=long chunk headers, bit 1=password, bit 2=extended header.";
 break;
 case 0x60200031U:
 if(!xx_rt_wcscmp(r->name,L"kind"))text=L"0=raw, 1=packed, 15=end marker.";
 break;
 case 0x60200032U:
 if(!xx_rt_wcscmp(r->name,L"kind"))text=L"0=raw, 1=packed, 15=end marker.";
 break;
 case 0x60200033U:
 if(!xx_rt_wcscmp(r->name,L"model_info"))text=L"Low 4 bits=order minus1; next 8=memory MiB minus1; high 4=variant minus A (7=H,8=I).";
 if(!xx_rt_wcscmp(r->name,L"name_size_and_restore"))text=L"Variant I: low 14 bits=name byte length, high 2=model restore method. Variant H uses all 16 bits for length.";
 if(!xx_rt_wcscmp(r->name,L"modified"))text=L"Packed DOS date/time, not a Unix timestamp.";
 break;
 case 0x60200036U:
 if(!xx_rt_wcscmp(r->name,L"next_block_offset"))text=L"Byte offset relative to the chunk stream-header base, after the chunk prefix.";
 break;
 case 0x60200038U:
 if(!xx_rt_wcscmp(r->name,L"next_block_offset"))text=L"Byte offset relative to the chunk stream-header base, after the chunk prefix.";
 break;
 case 0x6020003aU:
 if(!xx_rt_wcscmp(r->name,L"next_block_offset"))text=L"Byte offset relative to the chunk stream-header base, after the chunk prefix.";
 break;
 case 0x6020003cU:
 if(!xx_rt_wcscmp(r->name,L"next_block_offset"))text=L"Byte offset relative to the chunk stream-header base, after the chunk prefix.";
 break;
 case 0x6020003eU:
 if(!xx_rt_wcscmp(r->name,L"next_block_offset"))text=L"Byte offset relative to the chunk stream-header base, after the chunk prefix.";
 break;
 case 0x60200040U:
 if(!xx_rt_wcscmp(r->name,L"next_block_offset"))text=L"Byte offset relative to the chunk stream-header base, after the chunk prefix.";
 break;
 case 0x60200042U:
 if(!xx_rt_wcscmp(r->name,L"next_block_offset"))text=L"Byte offset relative to the chunk stream-header base, after the chunk prefix.";
 break;
 case 0x60200044U:
 if(!xx_rt_wcscmp(r->name,L"next_block_offset"))text=L"Byte offset relative to the chunk stream-header base, after the chunk prefix.";
 break;
 case 0x60200046U:
 if(!xx_rt_wcscmp(r->name,L"unpacked_size_low"))text=L"Low 32 bits of the uncompressed byte length; combine the following high word.";
 if(!xx_rt_wcscmp(r->name,L"unpacked_size_high"))text=L"High 32 bits of the uncompressed byte length.";
 break;
 case 0x60200047U:
 if(!xx_rt_wcscmp(r->name,L"next_block_offset"))text=L"Byte offset relative to the current two-stream chunk header base.";
 break;
 case 0x6020004aU:
 if(!xx_rt_wcscmp(r->name,L"next_directory_offset"))text=L"0xFFFFFFFF means the final directory. Other values are writer offsets/hints; the native reader visits complete directory items sequentially.";
 break;
 case 0x6020004bU:
 if(!xx_rt_wcscmp(r->name,L"data_offset"))text=L"Byte offset in the separate BACKUP.nnn file, not this CONTROL catalogue.";
 if(!xx_rt_wcscmp(r->name,L"time"))text=L"Packed DOS date/time, not a Unix timestamp.";
 if(!xx_rt_wcscmp(r->name,L"date"))text=L"Packed DOS date/time, not a Unix timestamp.";
 break;
 case 0x6020004cU:
 if(!xx_rt_wcscmp(r->name,L"flags"))text=L"Low 3 bits select compression; bit3 applies delta filter. Directory bytes are compressed and may be XOR-scrambled.";
 break;
 default:break;}
 if(id==0x6020002cU){char a[32];wchar_t b[32];int n=xx_rt_snprintf(a,sizeof(a),"0x%llX",(unsigned long long)xx_var_get_u64(&r->value));if(n<0||n>=(int)sizeof(a))return false;for(int i=0;i<=n;i++)b[i]=(unsigned char)a[i];if(!xx_data_struct_record_set_display_value(r,b))return false;}
 return !text||xx_data_struct_record_set_comment(r,text);
}
bool sxal_span(sx_context *c,uint64_t p,uint64_t n){return c->size>=0&&p<=(uint64_t)c->size&&n<=(uint64_t)c->size-p;}
bool sxal_read(sx_context *c,uint64_t p,void *b,size_t n){return sxal_span(c,p,n)&&sx_read(c,(int64_t)p,b,n);}
uint64_t sxal_uint(const uint8_t *b,unsigned n,bool be){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)b[i]<<(8*(be?n-1-i:i));return v;}
uint16_t sxal16(const uint8_t *b,bool be){return (uint16_t)sxal_uint(b,2,be);}
uint32_t sxal32(const uint8_t *b,bool be){return (uint32_t)sxal_uint(b,4,be);}
bool sxal_add(sx_context *c,const sx_layout *l,uint64_t p){const xx_data_struct_field_desc *f=&l->fields[l->field_count-1];return c->count<SXAL_LIMIT&&p<=INT64_MAX&&sx_add(c,l,(int64_t)p,f->rel_offset+f->size);}
static int sxal_order(const void *a,const void *b){const xx_data_struct *x=a,*y=b;return x->offset<y->offset?-1:x->offset>y->offset?1:0;}
static bool sxal_finish(sx_context *c){size_t n=c->count;int64_t end=0;xx_rt_qsort(c->items,n,sizeof(*c->items),sxal_order);for(size_t i=0;i<n;i++){int64_t p=c->items[i].offset-c->f->base_address;if(p<end)return false;if(p>end&&!sx_raw(c,end,p-end))return false;end=p+c->items[i].total_size;}if(end<c->size&&!sx_raw(c,end,c->size-end))return false;xx_rt_qsort(c->items,c->count,sizeof(*c->items),sxal_order);return !c->failed;}
static bool sx_archivelater_supports(xx_file_type_t type){switch(type){case XX_FILE_TYPE_ALDUS:case XX_FILE_TYPE_AMPK:case XX_FILE_TYPE_ARX:case XX_FILE_TYPE_PACKIT:case XX_FILE_TYPE_ASAR:case XX_FILE_TYPE_PEA:case XX_FILE_TYPE_STUFFIT5:case XX_FILE_TYPE_DISKDOUBLER:case XX_FILE_TYPE_XPK_COMPRESSED_FILE:case XX_FILE_TYPE_PPMD:case XX_FILE_TYPE_LRZIP:case XX_FILE_TYPE_RZIP:case XX_FILE_TYPE_MS_DOS_BACKUP:case XX_FILE_TYPE_MS_DOS_BACKUP2:case XX_FILE_TYPE_ESP_ARCHIVE:case XX_FILE_TYPE_LPAK:case XX_FILE_TYPE_UHARC:case XX_FILE_TYPE_IS_STORED:case XX_FILE_TYPE_TARX2:return true;default:return false;}}
static bool sx_archivelater_build(sx_context *c){if(!c||(!c->f->base_info_handled&&!xx_format_handle_base_info(c->f,c->pd))||!c->f->is_valid)return false;bool ok=false;switch(c->f->file_type){case XX_FILE_TYPE_ALDUS:ok=sx_aldus_build(c);break;case XX_FILE_TYPE_AMPK:ok=sx_ampk_build(c);break;case XX_FILE_TYPE_ARX:ok=sx_arx_build(c);break;case XX_FILE_TYPE_PACKIT:ok=sx_packit_build(c);break;case XX_FILE_TYPE_ASAR:ok=sx_asar_build(c);break;case XX_FILE_TYPE_PEA:ok=sx_pea_build(c);break;case XX_FILE_TYPE_STUFFIT5:ok=sx_stuffit5_build(c);break;case XX_FILE_TYPE_DISKDOUBLER:ok=sx_diskdoubler_build(c);break;case XX_FILE_TYPE_XPK_COMPRESSED_FILE:ok=sx_xpk_compressed_file_build(c);break;case XX_FILE_TYPE_PPMD:ok=sx_ppmd_build(c);break;case XX_FILE_TYPE_LRZIP:ok=sx_lrzip_build(c);break;case XX_FILE_TYPE_RZIP:ok=sx_rzip_build(c);break;case XX_FILE_TYPE_MS_DOS_BACKUP:ok=sx_ms_dos_backup_build(c);break;case XX_FILE_TYPE_MS_DOS_BACKUP2:ok=sx_ms_dos_backup2_build(c);break;case XX_FILE_TYPE_ESP_ARCHIVE:ok=sx_esp_archive_build(c);break;case XX_FILE_TYPE_LPAK:ok=sx_lpak_build(c);break;case XX_FILE_TYPE_UHARC:ok=sx_uharc_build(c);break;case XX_FILE_TYPE_IS_STORED:ok=sx_is_stored_build(c);break;case XX_FILE_TYPE_TARX2:ok=sx_tarx2_build(c);break;default:break;}return ok&&sxal_finish(c);}
const sx_family sx_archivelater_family={sx_archivelater_supports,sx_archivelater_build,sx_archivelater_layouts,SX_COUNT(sx_archivelater_layouts),sx_archivelater_comment};
