/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc791.html */
#include "xxfclib/formats/ip_packet/xx_ip_packet.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_packet.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b;bool ok=false;if(!blob_load(f,&b,pd))return false;BLOB_NEED(packet_ip_add(f,s,&b,0,b.n));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_ip_packet_init(xx_ip_packet *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_IP_PACKET,"bin");}}
xx_ip_packet *xx_ip_packet_create(xx_io_device *d,int64_t b){xx_ip_packet *r=(xx_ip_packet *)xx_mem_alloc(sizeof(*r));if(r)xx_ip_packet_init(r,d,b);return r;}
void xx_ip_packet_destroy(xx_ip_packet *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_ip_packet_free(xx_ip_packet *r){if(r){xx_ip_packet_destroy(r);xx_mem_free(r);}}
bool xx_ip_packet_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_ip_packet_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
