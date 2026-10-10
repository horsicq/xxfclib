/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://age-encryption.org/v1 */
#include "xxfclib/formats/age_encrypted/xx_age_encrypted.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_packet.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b;uint64_t at=22,p,n,z;uint8_t *decoded=NULL;unsigned recipients=0;bool ok=false;if(!blob_load(f,&b,pd))return false;BLOB_NEED(b.n>=128&&protocol_eq(&b,0,22,"age-encryption.org/v1\n")&&blob_add(f,s,&b,"age-version",0,22));while(true){BLOB_NEED(protocol_line(&b,&at,&p,&n,false));if(n>=4&&protocol_eq(&b,p,4,"--- ")){BLOB_NEED(recipients&&n==47&&packet_b64(&b,p+4,43,false,&decoded,&z)&&z==32&&packet_decoded(f,s,"encoded-header-mac",&decoded,z));break;}BLOB_NEED(++recipients<=128&&n==53&&protocol_eq(&b,p,10,"-> X25519 ")&&packet_b64(&b,p+10,43,false,&decoded,&z)&&z==32&&packet_decoded(f,s,"ephemeral-public-key",&decoded,z));BLOB_NEED(protocol_line(&b,&at,&p,&n,false)&&n==43&&packet_b64(&b,p,n,false,&decoded,&z)&&z==32&&packet_decoded(f,s,"encoded-wrapped-key",&decoded,z));}BLOB_NEED(protocol_take(&b,&at,b.n,16)&&blob_add(f,s,&b,"payload-nonce",at-16,16)&&b.n-at>=16);while(b.n-at>65552){BLOB_NEED(blob_add(f,s,&b,"encoded-ciphertext-chunk",at,65552));at+=65552;}BLOB_NEED(b.n-at>=16&&blob_add(f,s,&b,"encoded-final-ciphertext",at,b.n-at));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(decoded);xx_mem_free(b.p);return ok;}

void xx_age_encrypted_init(xx_age_encrypted *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_AGE_ENCRYPTED,"bin");}}
xx_age_encrypted *xx_age_encrypted_create(xx_io_device *d,int64_t b){xx_age_encrypted *r=(xx_age_encrypted *)xx_mem_alloc(sizeof(*r));if(r)xx_age_encrypted_init(r,d,b);return r;}
void xx_age_encrypted_destroy(xx_age_encrypted *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_age_encrypted_free(xx_age_encrypted *r){if(r){xx_age_encrypted_destroy(r);xx_mem_free(r);}}
bool xx_age_encrypted_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_age_encrypted_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
