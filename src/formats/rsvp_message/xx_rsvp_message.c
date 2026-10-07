/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc3209.html */
#include "xxfclib/formats/rsvp_message/xx_rsvp_message.h"
#include "xxfclib/data/xx_data.h"
#include "../xx_fifteenth_wrappers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){nh_blob b;bool ok=false;if(!nh_load(f,&b,pd))return false;NH_NEED(b.n==20&&b.p[0]==16&&b.p[1]==20&&b.p[4]>0&&!b.p[5]&&xx_data_get_u16(b.p+6, 2, 0, true)==20&&xx_data_get_u16(b.p+8, 2, 0, true)==12&&b.p[10]==22&&(b.p[11]==1||b.p[11]==2)&&xx_data_get_u32(b.p+12, 4, 0, true)>0&&f15_checksum(&b,0,b.n,0));NH_NEED(nh_add(f,s,&b,"rsvp-header",0,8)&&nh_add(f,s,&b,"hello-object-fields",8,4)&&nh_add(f,s,&b,"hello-instances",12,8));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_rsvp_message_init(xx_rsvp_message *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_RSVP_MESSAGE,"bin");}}
xx_rsvp_message *xx_rsvp_message_create(xx_io_device *d,int64_t b){xx_rsvp_message *r=(xx_rsvp_message *)xx_mem_alloc(sizeof(*r));if(r)xx_rsvp_message_init(r,d,b);return r;}
void xx_rsvp_message_destroy(xx_rsvp_message *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_rsvp_message_free(xx_rsvp_message *r){if(r){xx_rsvp_message_destroy(r);xx_mem_free(r);}}
bool xx_rsvp_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_rsvp_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
