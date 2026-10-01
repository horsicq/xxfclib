/* SPDX-License-Identifier: MIT.
 * Interplay ACM header: https://github.com/dtiefling/snd2acm-portable/blob/master/src/general.h
 * Preserves encoded bytes; this reader does not decode subband audio.
 */
#include "xxfclib/formats/interplay_acm/xx_interplay_acm.h"
#include "../xx_fifth_data.h"
#ifndef INTERPLAY_ACM
#define XX_FILE_TYPE_INTERPLAY_ACM ((xx_file_type_t)1508)
#endif
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {
    uint8_t h[14]; uint32_t samples; uint16_t ch,rate,shape; unsigned levels,subblocks; int64_t n;
    if(fd_stop(pd) || (n=pm_available(f))<=14 || !pm_read(f,0,h,sizeof(h)) ||
       pm_le32(h)!=UINT32_C(0x01032897)) return false;
    samples=pm_le32(h+4); ch=pm_le16(h+8); rate=pm_le16(h+10); shape=pm_le16(h+12);
    levels=shape&15U; subblocks=shape>>4;
    if(!samples || ch<1 || ch>2 || rate<6000 || rate>49716 ||
       !subblocks || subblocks>2048 || ((uint32_t)subblocks<<levels)>UINT32_C(1048576))
        return false;
    if(!pm_add(f,s,"acm-header.bin",0,14) ||
       !pm_add(f,s,"coded-audio.bin",14,n-14)) return false;
    s->size=n; return true;
}
void xx_interplay_acm_init(xx_interplay_acm*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_INTERPLAY_ACM,"acm");}}
xx_interplay_acm*xx_interplay_acm_create(xx_io_device*d,int64_t b) {xx_interplay_acm*r=(xx_interplay_acm*)xx_mem_alloc(sizeof(*r));if(r)xx_interplay_acm_init(r,d,b);return r;}
void xx_interplay_acm_destroy(xx_interplay_acm*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_interplay_acm_free(xx_interplay_acm*r) {if(r){xx_interplay_acm_destroy(r);xx_mem_free(r);}}
bool xx_interplay_acm_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_interplay_acm_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
