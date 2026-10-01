/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://raw.githubusercontent.com/qt/qtbase/5.15/src/gui/image/qpicture.cpp
 * Qt QPicture version9 bounded painter command stream with original CRC16, counted begin/end framing, typed integer/double point/polygon/drawing/save/restore records and bounded geometry. Only explicitly validated drawing opcodes; fonts, images, paths and replay unsupported.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#include "xxfclib/formats/qt_qpicture/xx_qt_qpicture.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../wavefront_obj/xx_eleventh_media.h"
static bool eg_quick(Abstractformat *f,uint64_t n) {uint8_t b[12];return n>=34&&pm_read(f,0,b,12)&&eg_tag(b,"QPIC",4)&&pm_be16(b+6)==9;}
static uint16_t qp_crc(const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
    xx_crc_context crc;
    uint64_t at=0U;
    if(!xx_crc_context_init_type(&crc,XX_CRC_TYPE_CRC16_X25)) return 0U;
    while(at<n) {
        size_t part=n-at>4096U ? 4096U : (size_t)(n-at);
        if(eg_stop(pd)) return 0U;
        xx_crc_context_update(&crc,b+(size_t)at,part);
        at+=part;
    }
    return (uint16_t)xx_crc_context_final(&crc);
}
static bool qp_numbers(const uint8_t *b,uint64_t p,uint32_t count,uint64_t end) {uint32_t i;if(!eg_span(p,(uint64_t)count*8,end))return false;for(i=0;i<count;++i)if((pm_be32(b+p+(uint64_t)i*8)&0x7ff00000U)==0x7ff00000U)return false;return true;}
static bool eg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint64_t p=32;uint32_t records,i,depth=0,drawings=0;char label[64];
 if(!eg_tag(b,"QPIC",4)||pm_be16(b+6)!=9||pm_be16(b+8)||b[10]!=30||b[11]!=4||pm_be16(b+4)!=qp_crc(b+6,n-6,pd)||(int32_t)pm_be32(b+20)<0||(int32_t)pm_be32(b+24)<0||(records=pm_be32(b+28))<2||records>4095||!eg_emit(f,s,"descriptor.qpic",0,32,n))return false;
 for(i=0;i<records;++i){uint64_t start=p,data,end;uint32_t z,command;
  if(eg_stop(pd)||!eg_span(p,2,n))return false;command=b[p++];z=b[p++];if(z==255){if(!eg_span(p,4,n))return false;z=pm_be32(b+p);p+=4;if(z<255)return false;}data=p;if(!eg_span(p,z,n))return false;end=p+z;
  if(command==31){if(z||i+1!=records||end!=n||depth||!drawings)return false;}
  else if(command==0){if(z)return false;}
  else if(command==32){if(z||++depth>64)return false;}
  else if(command==33){if(z||!depth)return false;--depth;}
  else if(command==1){if(z!=16||!qp_numbers(b,data,2,end))return false;++drawings;}
  else if(command==4||command==5||command==7){if(z!=32||!qp_numbers(b,data,4,end))return false;++drawings;}
  else if(command==6||command==8||command==9||command==10){if(z!=36||!qp_numbers(b,data,4,end))return false;++drawings;}
  else if(command==12||command==13){uint32_t count;if(z<4||(count=pm_be32(b+data))<1||count>65536||z!=4+(uint64_t)count*16+(command==13?1:0)||!qp_numbers(b,data+4,count*2,end)||(command==13&&b[end-1]>1))return false;++drawings;}
  else if(command==41||command==42||command==63||command==64){uint32_t v;if(z!=4)return false;v=pm_be32(b+data);if((command==41&&v>1)||(command==64&&v>37)||(command==63&&(v&~0x7fU)))return false;}
  else if(command==65){if(z!=1||b[data]>1)return false;}
  else if(command==66){uint32_t hi;if(z!=8||!qp_numbers(b,data,1,end)||(hi=pm_be32(b+data))>0x3ff00000U||hi==0x3ff00000U&&pm_be32(b+data+4))return false;}
  else return false;
  if(i+1==records&&command!=31)return false;
  p=end;xx_rt_snprintf(label,sizeof(label),"record-%u-opcode-%u.qpic",i,command);if(!eg_emit(f,s,label,start,p-start,n))return false;
 }
 if(p!=n)return false;s->size=(int64_t)n;return true;
}

void xx_qt_qpicture_init(xx_qt_qpicture *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_QT_QPICTURE,"qpic");}}
xx_qt_qpicture *xx_qt_qpicture_create(xx_io_device *d,int64_t at) {xx_qt_qpicture *r=(xx_qt_qpicture *)xx_mem_alloc(sizeof(*r));if(r)xx_qt_qpicture_init(r,d,at);return r;}
void xx_qt_qpicture_destroy(xx_qt_qpicture *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_qt_qpicture_free(xx_qt_qpicture *r) {if(r){xx_qt_qpicture_destroy(r);xx_mem_free(r);}}
bool xx_qt_qpicture_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_qt_qpicture_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
