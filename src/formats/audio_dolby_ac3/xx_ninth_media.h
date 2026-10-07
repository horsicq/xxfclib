/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Internal bounded primitives. Every reader supplies its own complete grammar.
 */
#ifndef XX_NINTH_MEDIA_H
#define XX_NINTH_MEDIA_H
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
static bool ng_parse(Abstractformat *,pm_stream *,const uint8_t *,uint64_t,xx_pd_struct *);
static bool ng_quick(Abstractformat *,uint64_t);
static __inline bool ng_span(uint64_t at,uint64_t bytes,uint64_t end) { return at<=end && bytes<=end-at; }
static __inline bool pm_tag(const uint8_t *p,const char *tag,size_t n) { return xx_rt_memcmp(p,tag,n)==0; }
static __inline bool ng_stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static __inline bool ng_zero(const uint8_t *p,uint64_t n) { uint64_t i;for(i=0;i<n;++i)if(p[i])return false;return true; }
static __inline bool ng_finite32(const uint8_t *p) { return (xx_data_get_u32(p, 4, 0, false)&0x7f800000U)!=0x7f800000U; }
static __inline bool ng_emit(Abstractformat *f,pm_stream *s,const char *label,uint64_t at,uint64_t n,uint64_t end) { return s->count<4096 && ng_span(at,n,end) && pm_add(f,s,label,(int64_t)at,(int64_t)n); }
typedef struct ng_bits { const uint8_t *b;uint64_t bit,end; } ng_bits;
static __inline bool ng_bits_get(ng_bits *q,unsigned count,uint32_t *v) {
 unsigned i;uint32_t value=0;if(count>32||q->bit>q->end||count>q->end-q->bit)return false;
 for(i=0;i<count;++i){value=(value<<1)|((q->b[q->bit/8]>>(7-(unsigned)(q->bit&7)))&1U);++q->bit;}*v=value;return true;
}
static __inline bool ng_bits_skip(ng_bits *q,uint64_t count) { if(q->bit>q->end||count>q->end-q->bit)return false;q->bit+=count;return true; }
static __inline uint16_t ng_crc16(const uint8_t *b,uint64_t n) { return xx_crc16(XX_CRC_TYPE_CRC16_BUYPASS,b,(size_t)n); }
static __inline uint32_t ng_crc_mpeg(const uint8_t *b,uint64_t n) { return xx_crc32(XX_CRC_TYPE_CRC32_MPEG2,b,(size_t)n); }
static __inline bool ng_probe(Abstractformat *f,uint64_t n,uint8_t *b,size_t count) { return count<=n && pm_read(f,0,b,count); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 int64_t available=pm_available(f);uint8_t *b;bool result;
 if(available<1||available>67108864||ng_stop(pd)||!ng_quick(f,(uint64_t)available))return false;
 b=(uint8_t *)xx_mem_alloc((size_t)available);if(!b)return false;
 result=pm_read(f,0,b,(size_t)available)&&ng_parse(f,s,b,(uint64_t)available,pd);xx_mem_free(b);return result;
}
#endif
