/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Atari XOR-A9 wrapper. Grammar: XArchive/core/xlegacystorearchive.cpp:2382.
 */
#include "xxfclib/formats/is_stored/xx_is_stored.h"
#include "../xx_payload_members.h"
static bool is_stored_magic(const uint8_t *h){return h[0]==0xe7 && h[1]==0x50 && h[2]==0xa9 && h[3]==0xae && h[6]==0xe7 && h[7]==0x50 && h[8]==0xa9 && h[9]==0xae && h[12]==0xdd && h[13]==0xc1 && h[14]==0xc0;}
static bool is_stored_range(Abstractformat *f,pm_member *m,uint64_t at,void *out,size_t n,xx_pd_struct *pd){size_t i;uint8_t *p=(uint8_t *)out;if(xx_pd_is_stopped(pd) || at>(uint64_t)m->size || n>(uint64_t)m->size-at || !pm_read(f,(int64_t)at,p,n))return false;for(i=0;i<n;++i)p[i]^=0xa9;return !xx_pd_is_stopped(pd);}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd){uint8_t h[16];int64_t n=pm_available(f);if(n<16 || xx_pd_is_stopped(pd) || !pm_read(f,0,h,16) || !is_stored_magic(h) || !pm_add(f,s,"decoded.bin",0,n))return false;s->items[0].read_range=is_stored_range;s->size=n;return true;}
void xx_is_stored_init(xx_is_stored *r,xx_io_device *d,int64_t b){if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_IS_STORED,"bin");r->format.check_is_valid=xx_is_stored_check_is_valid;r->format.handle_base_info=xx_is_stored_handle_base_info;}}
xx_is_stored *xx_is_stored_create(xx_io_device *d,int64_t b){xx_is_stored *r=(xx_is_stored *)xx_mem_alloc(sizeof(*r));if(r)xx_is_stored_init(r,d,b);return r;}
void xx_is_stored_destroy(xx_is_stored *r){if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_is_stored_free(xx_is_stored *r){if(r){xx_is_stored_destroy(r);xx_mem_free(r);}}
bool xx_is_stored_check_is_valid(Abstractformat *f,xx_pd_struct *pd){int64_t saved;bool valid;if(!f || !f->device || (saved=xx_io_tell(f->device))<0)return false;valid=pm_valid(f,pd);return xx_io_seek64(f->device,saved,SEEK_SET)==0 && valid;}
bool xx_is_stored_handle_base_info(Abstractformat *f,xx_pd_struct *pd){int64_t saved;bool valid;if(!f || !f->device || (saved=xx_io_tell(f->device))<0)return false;valid=pm_handle(f,pd);if(xx_io_seek64(f->device,saved,SEEK_SET)!=0){f->is_valid=false;return false;}return valid;}
xx_file_type_t xx_is_stored_detect(xx_io_device *d,int64_t b){uint8_t h[16];return xx_io_read_at(d,b,h,16) && is_stored_magic(h)?XX_FILE_TYPE_IS_STORED:XX_FILE_TYPE_UNKNOWN;}
