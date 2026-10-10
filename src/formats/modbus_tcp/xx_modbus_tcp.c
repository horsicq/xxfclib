/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf */
#include "xxfclib/formats/modbus_tcp/xx_modbus_tcp.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_fields.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b;uint64_t at=0;unsigned count=0;bool ok=false;if(!blob_load(f,&b,pd))return false;BLOB_NEED(b.n>=9);while(at<b.n){uint64_t start=at;BLOB_NEED(++count<=1024&&protocol_take(&b,&at,b.n,7));uint64_t z=xx_data_get_u16(b.p+(size_t)start+4, 2, 0, true);BLOB_NEED(z>=3&&z<=254&&!xx_data_get_u16(b.p+(size_t)start+2, 2, 0, true)&&b.p[(size_t)start+6]>0&&b.p[(size_t)start+6]<=247);uint64_t p=at,end=start+6+z;BLOB_NEED(end<=b.n);unsigned type=b.p[(size_t)p];uint64_t n=end-p;if(type==3){if(n==5){uint32_t address=xx_data_get_u16(b.p+(size_t)p+1, 2, 0, true),quantity=xx_data_get_u16(b.p+(size_t)p+3, 2, 0, true);BLOB_NEED(quantity&&quantity<=125&&address+quantity<=65536);}else BLOB_NEED(n>=4&&b.p[(size_t)p+1]>0&&!(b.p[(size_t)p+1]&1)&&b.p[(size_t)p+1]<=250&&n==2+(uint64_t)b.p[(size_t)p+1]);}else if(type==5){BLOB_NEED(n==5&&(xx_data_get_u16(b.p+(size_t)p+3, 2, 0, true)==0||xx_data_get_u16(b.p+(size_t)p+3, 2, 0, true)==0xff00));}else if(type==6){BLOB_NEED(n==5);}else if(type==16){BLOB_NEED(n>=5);uint32_t address=xx_data_get_u16(b.p+(size_t)p+1, 2, 0, true),quantity=xx_data_get_u16(b.p+(size_t)p+3, 2, 0, true);BLOB_NEED(quantity&&quantity<=123&&address+quantity<=65536);if(n!=5)BLOB_NEED(n>=8&&b.p[(size_t)p+5]==quantity*2&&n==6+(uint64_t)quantity*2);}else{BLOB_NEED((type==0x83||type==0x85||type==0x86||type==0x90)&&n==2&&((b.p[(size_t)p+1]>=1&&b.p[(size_t)p+1]<=6)||b.p[(size_t)p+1]==8||b.p[(size_t)p+1]==10||b.p[(size_t)p+1]==11));}BLOB_NEED(blob_add(f,s,&b,"mbap-header",start,7)&&blob_add(f,s,&b,"function-fields",p,type==3&&n!=5?2:type==16&&n!=5?6:n));if(type==3&&n!=5)BLOB_NEED(blob_add(f,s,&b,"register-values",p+2,n-2));if(type==16&&n!=5)BLOB_NEED(blob_add(f,s,&b,"register-values",p+6,n-6));at=end;}s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_modbus_tcp_init(xx_modbus_tcp *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_MODBUS_TCP,"bin");}}
xx_modbus_tcp *xx_modbus_tcp_create(xx_io_device *d,int64_t b){xx_modbus_tcp *r=(xx_modbus_tcp *)xx_mem_alloc(sizeof(*r));if(r)xx_modbus_tcp_init(r,d,b);return r;}
void xx_modbus_tcp_destroy(xx_modbus_tcp *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_modbus_tcp_free(xx_modbus_tcp *r){if(r){xx_modbus_tcp_destroy(r);xx_mem_free(r);}}
bool xx_modbus_tcp_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_modbus_tcp_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
