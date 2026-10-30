/* Original DarkWave Studio3.6.1 serializers, v3/v4 files; primary source retained. */
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/strings/xx_string.h"
static const xx_data_struct_field_desc sxdwp_header_fields[]={
 SX_FIELD("signature","uint8[4]",0,4,SX_SIGNATURE),
 SX_FIELD("major_version","int8",4,1,SX_NONE),
 SX_FIELD("minor_version","int8",5,1,SX_NONE),
 SX_FIELD("beats_per_minute","float",6,4,SX_NONE),
};
static const sx_layout sxdwp_header={UINT32_C(0x60a60001),"darkwave_header",sxdwp_header_fields,SX_COUNT(sxdwp_header_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_ticks_fields[]={
 SX_FIELD("ticks_per_beat","float",0,4,SX_NONE),
};
static const sx_layout sxdwp_ticks={UINT32_C(0x60a60002),"darkwave_ticks",sxdwp_ticks_fields,SX_COUNT(sxdwp_ticks_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_machine_count_fields[]={
 SX_FIELD("machine_count","int32",0,4,SX_COUNT_PROP),
};
static const sx_layout sxdwp_machine_count={UINT32_C(0x60a60003),"darkwave_machine_count",sxdwp_machine_count_fields,SX_COUNT(sxdwp_machine_count_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_machine_fields[]={
 SX_FIELD("name","char[1024]",0,1024,SX_STRING),
 SX_FIELD("unique_id","char[1024]",1024,1024,SX_STRING),
 SX_FIELD("x","int32",2048,4,SX_NONE),
 SX_FIELD("y","int32",2052,4,SX_NONE),
 SX_FIELD("visible_control","int32",2056,4,SX_ID),
 SX_FIELD("property_bytes","int32",2060,4,SX_SIZE),
 SX_FIELD("is_inner","uint8",2064,1,SX_FLAGS),
};
static const sx_layout sxdwp_machine={UINT32_C(0x60a60004),"darkwave_machine",sxdwp_machine_fields,SX_COUNT(sxdwp_machine_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_connections_size_fields[]={
 SX_FIELD("connections_bytes","int32",0,4,SX_SIZE),
};
static const sx_layout sxdwp_connections_size={UINT32_C(0x60a60005),"darkwave_connections_size",sxdwp_connections_size_fields,SX_COUNT(sxdwp_connections_size_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_patterns_size_fields[]={
 SX_FIELD("patterns_bytes","int32",0,4,SX_SIZE),
};
static const sx_layout sxdwp_patterns_size={UINT32_C(0x60a60006),"darkwave_patterns_size",sxdwp_patterns_size_fields,SX_COUNT(sxdwp_patterns_size_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_sequence_size_fields[]={
 SX_FIELD("sequence_bytes","int32",0,4,SX_SIZE),
};
static const sx_layout sxdwp_sequence_size={UINT32_C(0x60a60007),"darkwave_sequence_size",sxdwp_sequence_size_fields,SX_COUNT(sxdwp_sequence_size_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_project_info_size_fields[]={
 SX_FIELD("project_info_bytes","int32",0,4,SX_SIZE),
};
static const sx_layout sxdwp_project_info_size={UINT32_C(0x60a60008),"darkwave_project_info_size",sxdwp_project_info_size_fields,SX_COUNT(sxdwp_project_info_size_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_connections_header_fields[]={
 SX_FIELD("connection_count","int32",0,4,SX_COUNT_PROP),
};
static const sx_layout sxdwp_connections_header={UINT32_C(0x60a60009),"darkwave_connections_header",sxdwp_connections_header_fields,SX_COUNT(sxdwp_connections_header_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_connection_fields[]={
 SX_FIELD("input_machine","int32",0,4,SX_ID),
 SX_FIELD("input_connector","int32",4,4,SX_ID),
 SX_FIELD("output_machine","int32",8,4,SX_ID),
 SX_FIELD("output_connector","int32",12,4,SX_ID),
 SX_FIELD("power_ratio","double",16,8,SX_NONE),
};
static const sx_layout sxdwp_connection={UINT32_C(0x60a6000a),"darkwave_connection",sxdwp_connection_fields,SX_COUNT(sxdwp_connection_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_patterns_header_fields[]={
 SX_FIELD("pattern_count","int32",0,4,SX_COUNT_PROP),
};
static const sx_layout sxdwp_patterns_header={UINT32_C(0x60a6000b),"darkwave_patterns_header",sxdwp_patterns_header_fields,SX_COUNT(sxdwp_patterns_header_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_pattern_fields[]={
 SX_FIELD("index","int32",0,4,SX_ID),
 SX_FIELD("length","int32",4,4,SX_NONE),
 SX_FIELD("name","char[1024]",8,1024,SX_STRING),
 SX_FIELD("note_count","int32",1032,4,SX_COUNT_PROP),
 SX_FIELD("control_track_count","int32",1036,4,SX_COUNT_PROP),
 SX_FIELD("machine","int32",1040,4,SX_ID),
 SX_FIELD("connector","int32",1044,4,SX_ID),
};
static const sx_layout sxdwp_pattern={UINT32_C(0x60a6000c),"darkwave_pattern",sxdwp_pattern_fields,SX_COUNT(sxdwp_pattern_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_note_fields[]={
 SX_FIELD("start","int32",0,4,SX_NONE),
 SX_FIELD("length","int32",4,4,SX_NONE),
 SX_FIELD("note","int32",8,4,SX_ID),
};
static const sx_layout sxdwp_note={UINT32_C(0x60a6000d),"darkwave_note",sxdwp_note_fields,SX_COUNT(sxdwp_note_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_control_track_fields[]={
 SX_FIELD("property_index","int32",0,4,SX_ID),
 SX_FIELD("event_count","int32",4,4,SX_COUNT_PROP),
};
static const sx_layout sxdwp_control_track={UINT32_C(0x60a6000e),"darkwave_control_track",sxdwp_control_track_fields,SX_COUNT(sxdwp_control_track_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_control_event_fields[]={
 SX_FIELD("value","float",0,4,SX_NONE),
 SX_FIELD("used","uint8",4,1,SX_FLAGS),
 SX_FIELD("alignment_byte","uint8",5,1,SX_NONE),
};
static const sx_layout sxdwp_control_event={UINT32_C(0x60a6000f),"darkwave_control_event",sxdwp_control_event_fields,SX_COUNT(sxdwp_control_event_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_sequence_header_fields[]={
 SX_FIELD("length","int32",0,4,SX_NONE),
 SX_FIELD("track_count","int32",4,4,SX_COUNT_PROP),
};
static const sx_layout sxdwp_sequence_header={UINT32_C(0x60a60010),"darkwave_sequence_header",sxdwp_sequence_header_fields,SX_COUNT(sxdwp_sequence_header_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_sequence_track_fields[]={
 SX_FIELD("port","int32",0,4,SX_ID),
 SX_FIELD("event_count","int32",4,4,SX_COUNT_PROP),
};
static const sx_layout sxdwp_sequence_track={UINT32_C(0x60a60011),"darkwave_sequence_track",sxdwp_sequence_track_fields,SX_COUNT(sxdwp_sequence_track_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_sequence_event_fields[]={
 SX_FIELD("start","int32",0,4,SX_NONE),
 SX_FIELD("pattern","int32",4,4,SX_ID),
};
static const sx_layout sxdwp_sequence_event={UINT32_C(0x60a60012),"darkwave_sequence_event",sxdwp_sequence_event_fields,SX_COUNT(sxdwp_sequence_event_fields),SX_LE};
static const xx_data_struct_field_desc sxdwp_project_info_fields[]={
 SX_FIELD("text","char",0,0,SX_STRING),
};
static const sx_layout sxdwp_project_info={UINT32_C(0x60a60013),"darkwave_project_info",sxdwp_project_info_fields,SX_COUNT(sxdwp_project_info_fields),SX_LE};
static const sx_layout *const sx_darkwave_layouts[]={&sxdwp_header,&sxdwp_ticks,&sxdwp_machine_count,&sxdwp_machine,&sxdwp_connections_size,&sxdwp_patterns_size,&sxdwp_sequence_size,&sxdwp_project_info_size,&sxdwp_connections_header,&sxdwp_connection,&sxdwp_patterns_header,&sxdwp_pattern,&sxdwp_note,&sxdwp_control_track,&sxdwp_control_event,&sxdwp_sequence_header,&sxdwp_sequence_track,&sxdwp_sequence_event,&sxdwp_project_info};
static bool sx_darkwave_comment(xx_data_struct_record*r,uint32_t id){if(!r||!r->name)return true;const wchar_t*text=NULL;switch(id){
 case 0x60a60001:
  if(!xx_rt_wcscmp(r->name,L"signature"))text=L"Original little-endian multi-character DWP magic, physical bytes space P W D.";
  if(!xx_rt_wcscmp(r->name,L"major_version"))text=L"Original C char version with the producer MinGW signed-char default. Supported physical variants are 3 and 4.";
  if(!xx_rt_wcscmp(r->name,L"minor_version"))text=L"Original C char minor version; retained unchanged.";
  if(!xx_rt_wcscmp(r->name,L"beats_per_minute"))text=L"Original IEEE-754 binary32 project tempo in beats per minute.";
 break;
 case 0x60a60002:
  if(!xx_rt_wcscmp(r->name,L"ticks_per_beat"))text=L"Version4 stores this IEEE-754 binary32 ticks-per-beat value. Version3 has no stored field; the original reader supplies16 at runtime.";
 break;
 case 0x60a60003:
  if(!xx_rt_wcscmp(r->name,L"machine_count"))text=L"Signed count of physical machine records and subsequent connection/pattern section pairs.";
 break;
 case 0x60a60004:
  if(!xx_rt_wcscmp(r->name,L"name"))text=L"Original fixed1024-byte machine name; raw_value preserves all bytes after the first NUL.";
  if(!xx_rt_wcscmp(r->name,L"unique_id"))text=L"Original fixed1024-byte machine unique identifier; not an on-disk pointer.";
  if(!xx_rt_wcscmp(r->name,L"x"))text=L"Signed saved horizontal editor coordinate.";
  if(!xx_rt_wcscmp(r->name,L"y"))text=L"Signed saved vertical editor coordinate.";
  if(!xx_rt_wcscmp(r->name,L"visible_control"))text=L"Signed saved machine control selection; retained without runtime clamping.";
  if(!xx_rt_wcscmp(r->name,L"property_bytes"))text=L"Signed byte length of the following plugin-specific property block, which remains raw.";
  if(!xx_rt_wcscmp(r->name,L"is_inner"))text=L"Original one-byte C++ bool identifying the saved property representation; physical byte is retained without requiring0/1.";
 break;
 case 0x60a60005:
  if(!xx_rt_wcscmp(r->name,L"connections_bytes"))text=L"Signed physical byte length of the following connections section, excluding this length word. Negative/unbounded lengths stop this optional traversal.";
 break;
 case 0x60a60006:
  if(!xx_rt_wcscmp(r->name,L"patterns_bytes"))text=L"Signed physical byte length of the following patterns section, excluding this length word. Negative/unbounded lengths stop this optional traversal.";
 break;
 case 0x60a60007:
  if(!xx_rt_wcscmp(r->name,L"sequence_bytes"))text=L"Signed physical byte length of the following sequence section, excluding this length word. Negative/unbounded lengths stop this optional traversal.";
 break;
 case 0x60a60008:
  if(!xx_rt_wcscmp(r->name,L"project_info_bytes"))text=L"Signed physical byte length of the following project info section, excluding this length word. Negative/unbounded lengths stop this optional traversal.";
 break;
 case 0x60a60009:
  if(!xx_rt_wcscmp(r->name,L"connection_count"))text=L"Signed number of24-byte connection rows in this machine section.";
 break;
 case 0x60a6000a:
  if(!xx_rt_wcscmp(r->name,L"input_machine"))text=L"Signed source machine-list index for the input endpoint.";
  if(!xx_rt_wcscmp(r->name,L"input_connector"))text=L"Signed input-machine port index.";
  if(!xx_rt_wcscmp(r->name,L"output_machine"))text=L"Signed machine-list index for the output endpoint.";
  if(!xx_rt_wcscmp(r->name,L"output_connector"))text=L"Signed output-machine port index.";
  if(!xx_rt_wcscmp(r->name,L"power_ratio"))text=L"Version3/4 original IEEE-754 binary64 connection power ratio. Older float/no-volume variants are outside this provider scope.";
 break;
 case 0x60a6000b:
  if(!xx_rt_wcscmp(r->name,L"pattern_count"))text=L"Signed number of serialized patterns for this machine.";
 break;
 case 0x60a6000c:
  if(!xx_rt_wcscmp(r->name,L"index"))text=L"Signed original pattern index used by sequence entries.";
  if(!xx_rt_wcscmp(r->name,L"length"))text=L"Signed pattern length in the original editor time grid.";
  if(!xx_rt_wcscmp(r->name,L"name"))text=L"Original fixed1024-byte pattern name; raw bytes after NUL retained.";
  if(!xx_rt_wcscmp(r->name,L"note_count"))text=L"Signed number of following12-byte note rows.";
  if(!xx_rt_wcscmp(r->name,L"control_track_count"))text=L"Signed number of subsequent control tracks and their event arrays.";
  if(!xx_rt_wcscmp(r->name,L"machine"))text=L"Signed machine index.";
  if(!xx_rt_wcscmp(r->name,L"connector"))text=L"Signed machine port index.";
 break;
 case 0x60a6000d:
  if(!xx_rt_wcscmp(r->name,L"start"))text=L"Signed note start in the original editor time grid, not a byte offset.";
  if(!xx_rt_wcscmp(r->name,L"length"))text=L"Signed note duration in the original editor time grid, not a physical byte size.";
  if(!xx_rt_wcscmp(r->name,L"note"))text=L"Signed stored note number; no musical-range validation or clamping.";
 break;
 case 0x60a6000e:
  if(!xx_rt_wcscmp(r->name,L"property_index"))text=L"Signed target property index.";
  if(!xx_rt_wcscmp(r->name,L"event_count"))text=L"Signed number of following6-byte control events.";
 break;
 case 0x60a6000f:
  if(!xx_rt_wcscmp(r->name,L"value"))text=L"Original IEEE-754 binary32 control value.";
  if(!xx_rt_wcscmp(r->name,L"used"))text=L"One-byte C++ bool marking an active control event; stored byte retained.";
  if(!xx_rt_wcscmp(r->name,L"alignment_byte"))text=L"Physical trailing byte introduced by original pack(2) structure alignment. Writer does not initialize or assign it; no reserved-zero claim.";
 break;
 case 0x60a60010:
  if(!xx_rt_wcscmp(r->name,L"length"))text=L"Signed sequence length in the original editor time grid.";
  if(!xx_rt_wcscmp(r->name,L"track_count"))text=L"Signed number of sequence track records.";
 break;
 case 0x60a60011:
  if(!xx_rt_wcscmp(r->name,L"port"))text=L"Signed saved port index.";
  if(!xx_rt_wcscmp(r->name,L"event_count"))text=L"Signed number of following8-byte sequence events; original struct calls this Size.";
 break;
 case 0x60a60012:
  if(!xx_rt_wcscmp(r->name,L"start"))text=L"Signed event start in the original editor time grid, not a byte offset.";
  if(!xx_rt_wcscmp(r->name,L"pattern"))text=L"Signed original pattern index.";
 break;
 case 0x60a60013:
  if(!xx_rt_wcscmp(r->name,L"text"))text=L"Original project information bytes including the producer NUL terminator when present. Values over4096 bytes remain raw; no implicit character encoding conversion.";
 break;
 default:break;}return !text||xx_data_struct_record_set_comment(r,text);}
/* Little-endian original writer grammar. Plugin property bodies remain opaque.
 * Optional damaged sections keep known physical prefixes and their outer bounds.
 * I/O/cancellation is never replaced by a raw-only success. */
typedef struct sxdwp_view {sx_context*c;uint64_t at,end,entries;unsigned work;} sxdwp_view;
static bool sxdwp_span(sxdwp_view*v,uint64_t n){return !xx_pd_is_stopped(v->c->pd)&&v->at<=v->end&&n<=v->end-v->at;}
static bool sxdwp_get(sxdwp_view*v,uint64_t off,uint64_t*x){uint8_t b[4];if(++v->work>100000||!sxdwp_span(v,off)||v->end-v->at-off<4||!sx_read(v->c,(int64_t)(v->at+off),b,4))return false;*x=xx_data_get_u32(b,4,0,false);return true;}
static bool sxdwp_row(sxdwp_view*v,const sx_layout*l,uint64_t n){if(v->entries>=24000||!n||!sxdwp_span(v,n)||!sx_add(v->c,l,(int64_t)v->at,(int64_t)n))return false;v->at+=n;v->entries++;return true;}
static bool sxdwp_word(sxdwp_view*v,const sx_layout*l,uint64_t*n){return sxdwp_get(v,0,n)&&sxdwp_row(v,l,4)&&*n<=INT32_MAX;}
static bool sxdwp_table(sxdwp_view*v,const sx_layout*l,unsigned width,uint64_t n){if(!width||n>INT32_MAX||!sxdwp_span(v,0)||n>(v->end-v->at)/width)return false;if(!n)return true;uint64_t shown=n;if(shown>24000-v->entries)shown=24000-v->entries;if(!shown||!sx_table(v->c,l,(int64_t)v->at,width,shown))return false;v->at+=shown*width;v->entries+=shown;return shown==n;}
static bool sxdwp_block(sxdwp_view*v,const sx_layout*l,uint64_t*end){uint64_t n;if(!sxdwp_word(v,l,&n)||!sxdwp_span(v,n))return false;*end=v->at+n;return true;}
static void sxdwp_connections(sxdwp_view*v){uint64_t n;if(sxdwp_word(v,&sxdwp_connections_header,&n))sxdwp_table(v,&sxdwp_connection,24,n);}
static void sxdwp_patterns(sxdwp_view*v){uint64_t n,notes,tracks,events;if(!sxdwp_word(v,&sxdwp_patterns_header,&n))return;for(uint64_t i=0;i<n;i++){
 if(!sxdwp_span(v,1048)||!sxdwp_get(v,1032,&notes)||!sxdwp_get(v,1036,&tracks)||!sxdwp_row(v,&sxdwp_pattern,1048)||notes>INT32_MAX||tracks>INT32_MAX||!sxdwp_table(v,&sxdwp_note,12,notes))return;
 for(uint64_t j=0;j<tracks;j++){if(!sxdwp_span(v,8)||!sxdwp_get(v,4,&events)||!sxdwp_row(v,&sxdwp_control_track,8)||events>INT32_MAX||!sxdwp_table(v,&sxdwp_control_event,6,events))return;}
}}
static void sxdwp_sequence(sxdwp_view*v){uint64_t n,events;if(!sxdwp_span(v,8)||!sxdwp_get(v,4,&n)||!sxdwp_row(v,&sxdwp_sequence_header,8)||n>INT32_MAX)return;for(uint64_t i=0;i<n;i++){if(!sxdwp_span(v,8)||!sxdwp_get(v,4,&events)||!sxdwp_row(v,&sxdwp_sequence_track,8)||events>INT32_MAX||!sxdwp_table(v,&sxdwp_sequence_event,8,events))return;}}
static bool sxdwp_section(sxdwp_view*v,const sx_layout*l,void(*parse)(sxdwp_view*)){uint64_t end,outer=v->end;if(!sxdwp_block(v,l,&end))return false;v->end=end;parse(v);v->end=outer;v->at=end;return !v->c->failed&&!xx_pd_is_stopped(v->c->pd)&&v->entries<24000&&v->work<=100000;}
static void sxdwp_body(sxdwp_view*v,unsigned major){uint64_t machines,n,end;if(major==4&&!sxdwp_row(v,&sxdwp_ticks,4))return;if(!sxdwp_word(v,&sxdwp_machine_count,&machines))return;
 for(uint64_t i=0;i<machines;i++){if(!sxdwp_span(v,2065)||!sxdwp_get(v,2060,&n)||!sxdwp_row(v,&sxdwp_machine,2065)||n>INT32_MAX||!sxdwp_span(v,n))return;v->at+=n;}
 for(uint64_t i=0;i<machines;i++)if(!sxdwp_section(v,&sxdwp_connections_size,sxdwp_connections)||!sxdwp_section(v,&sxdwp_patterns_size,sxdwp_patterns))return;
 if(!sxdwp_section(v,&sxdwp_sequence_size,sxdwp_sequence)||!sxdwp_block(v,&sxdwp_project_info_size,&end))return;n=end-v->at;if(n&&n<=4096)sxdwp_row(v,&sxdwp_project_info,n);v->at=end;
}
static int sxdwp_compare(const void*aa,const void*bb){const xx_data_struct*a=aa,*b=bb;return a->offset<b->offset?-1:a->offset>b->offset?1:0;}
static bool sxdwp_finish(sx_context*c){size_t n=c->count;int64_t end=0;for(size_t i=0;i<n;i++){int64_t p=c->items[i].offset-c->f->base_address,z=c->items[i].total_size;if(p<end)return false;if(p>end&&!sx_raw(c,end,p-end))return false;end=p+z;}if(end<c->size&&!sx_raw(c,end,c->size-end))return false;xx_rt_qsort(c->items,c->count,sizeof(*c->items),sxdwp_compare);return !c->failed&&!xx_pd_is_stopped(c->pd);}
static bool sx_darkwave_supports(xx_file_type_t t){switch(t){case XX_FILE_TYPE_DIE_MUSIC_WERNER_DARKWAVESTUDIO_DWP:return true;default:return false;}}
static bool sx_darkwave_build(sx_context*c){if(!c||!c->f||c->size<=0||c->f->base_address<0||c->f->base_address>INT64_MAX-c->size||(!c->f->base_info_handled&&!xx_format_handle_base_info(c->f,c->pd))||!c->f->is_valid||xx_pd_is_stopped(c->pd))return false;
 sxdwp_view v={c,0,(uint64_t)c->size,0,0};uint8_t b[10];if(c->size>=10){if(!sx_read(c,0,b,10))return false;if(!xx_rt_memcmp(b," PWD",4)&&(b[4]==3||b[4]==4)){if(!sxdwp_row(&v,&sxdwp_header,10))return false;sxdwp_body(&v,b[4]);}}
 if(c->failed||xx_pd_is_stopped(c->pd))return false;return c->count?sxdwp_finish(c):sx_raw(c,0,c->size);
}
const sx_family sx_darkwave_family={sx_darkwave_supports,sx_darkwave_build,sx_darkwave_layouts,SX_COUNT(sx_darkwave_layouts),sx_darkwave_comment};
