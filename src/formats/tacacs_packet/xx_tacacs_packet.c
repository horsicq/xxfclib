/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc8907.html */
#include "xxfclib/formats/tacacs_packet/xx_tacacs_packet.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_packet.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b;uint64_t at=20;bool ok=false;if(!blob_load(f,&b,pd))return false;BLOB_NEED(b.n>=20&&(b.p[0]==192||b.p[0]==193)&&b.p[1]==1&&b.p[2]==1&&b.p[3]==1&&xx_data_get_u32(b.p+8, 4, 0, true)==b.n-12&&b.p[12]==1&&b.p[13]<=15&&b.p[14]>=1&&b.p[14]<=6&&b.p[15]>=1&&b.p[15]<=9);BLOB_NEED(b.p[16]>0&&b.p[17]>0&&b.p[18]>0&&blob_add(f,s,&b,"tacacs-header",0,12)&&blob_add(f,s,&b,"auth-start-fields",12,8));const char *labels[4]={"user","port","remote-address","encoded-data"};for(unsigned i=0;i<4;++i){uint64_t n=b.p[16+i],p=at;BLOB_NEED(protocol_take(&b,&at,b.n,n));if(i<3)BLOB_NEED(packet_utf(&b,p,n));if(n)BLOB_NEED(blob_add(f,s,&b,labels[i],p,n));}BLOB_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_tacacs_packet_init(xx_tacacs_packet *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TACACS_PACKET,"bin");}}
xx_tacacs_packet *xx_tacacs_packet_create(xx_io_device *d,int64_t b){xx_tacacs_packet *r=(xx_tacacs_packet *)xx_mem_alloc(sizeof(*r));if(r)xx_tacacs_packet_init(r,d,b);return r;}
void xx_tacacs_packet_destroy(xx_tacacs_packet *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tacacs_packet_free(xx_tacacs_packet *r){if(r){xx_tacacs_packet_destroy(r);xx_mem_free(r);}}
bool xx_tacacs_packet_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_tacacs_packet_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
