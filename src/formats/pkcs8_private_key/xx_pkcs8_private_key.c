/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc5208.html */
#include "xxfclib/formats/pkcs8_private_key/xx_pkcs8_private_key.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_security_framing.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){memory_blob b;bool ok=false;uint64_t at=0,p,q,v;der_tlv root,version,alg,oid,key,seed;unsigned work=0;if(!blob_load(f,&b,pd))return false;BLOB_NEED(b.n>=48&&der_take(&b,&at,b.n,48,&root)&&at==b.n&&protocol_tree(&b,0,b.n,0,&work));p=root.value;BLOB_NEED(der_take(&b,&p,root.end,2,&version)&&protocol_uint(&b,&version,&v)&&v==0&&der_take(&b,&p,root.end,48,&alg));q=alg.value;BLOB_NEED(der_take(&b,&q,alg.end,6,&oid)&&q==alg.end&&protocol_eq(&b,oid.value,oid.end-oid.value,"\x2b\x65\x70"));BLOB_NEED(der_take(&b,&p,root.end,4,&key)&&p==root.end);q=key.value;BLOB_NEED(der_take(&b,&q,key.end,4,&seed)&&q==key.end&&seed.end-seed.value==32);BLOB_NEED(protocol_tlv_add(f,s,&b,"pkcs8-version",&version)&&protocol_tlv_add(f,s,&b,"algorithm-identifier",&alg)&&blob_add(f,s,&b,"ed25519-private-seed",seed.value,32));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_pkcs8_private_key_init(xx_pkcs8_private_key *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_PKCS8_PRIVATE_KEY,"bin");}}
xx_pkcs8_private_key *xx_pkcs8_private_key_create(xx_io_device *d,int64_t b){xx_pkcs8_private_key *r=(xx_pkcs8_private_key *)xx_mem_alloc(sizeof(*r));if(r)xx_pkcs8_private_key_init(r,d,b);return r;}
void xx_pkcs8_private_key_destroy(xx_pkcs8_private_key *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_pkcs8_private_key_free(xx_pkcs8_private_key *r){if(r){xx_pkcs8_private_key_destroy(r);xx_mem_free(r);}}
bool xx_pkcs8_private_key_check_is_valid(Abstractformat *f,xx_pd_struct *pd){return pm_valid(f,pd);}
bool xx_pkcs8_private_key_handle_base_info(Abstractformat *f,xx_pd_struct *pd){return pm_handle(f,pd);}
