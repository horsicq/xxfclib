/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_gzip/xx_sfx_gzip.h"
#include "../sfx_arcv2/xx_sixth_wrapper_table.h"

static bool w6_gz_at(Abstractformat *f,pm_stream *s,int64_t at,xx_pd_struct *pd) {
    int64_t available=pm_available(f)-at;uint8_t *input=NULL,*output=NULL;size_t n,p=10,consumed=0,written;xx_io_device *dest=NULL;bool ok=false;unsigned j;
    if(available<18) { return false; } n=available>33554432 ? 33554432U:(size_t)available;input=(uint8_t *)xx_mem_alloc(n);if(!input || !pm_read(f,at,input,n) || input[2]!=8 || input[3]&224) goto done;
    if(input[3]&4) { unsigned extra;if(n-p<2) goto done;extra=pm_le16(input+p);p+=2;if(extra>n-p) goto done;p+=extra; }
    for(j=0;j<2;++j) if(input[3]&(8U<<j)) { size_t start=p;while(p<n && input[p] && p-start<4096) ++p;if(p==n || input[p]) goto done;++p; }
    if(input[3]&2) { if(n-p<2 || (w6_crc(input,p)&65535)!=pm_le16(input+p)) goto done;p+=2; }
    if(n-p<8 || wg_stop(pd)) { goto done; } output=(uint8_t *)xx_mem_alloc(67108864);if(!output || !(dest=xx_io_mem_open(output,67108864))) goto done;
    if(!xx_deflate_unpack_memory_to_device_ex(input+p,n-p,dest,&consumed,false,pd) || consumed>n-p || n-p-consumed<8) { goto done; } written=(size_t)xx_io_tell(dest);
    if(written!=pm_le32(input+p+consumed+4) || !w6_crc_checked(output,written,pm_le32(input+p+consumed),pd) || wg_stop(pd)) goto done;
    ok=w6_component(f,s,at,(int64_t)(p+consumed+8),"payload.gz");
done:if(dest) xx_io_close(dest);if(input) xx_mem_free(input);if(output) xx_mem_free(output);return ok;
}
/* UPX launchers can declare SizeOfHeaders=4096 while their first raw section
 * starts at 512. Their bounded DOS image still authenticates the carrier;
 * the complete gzip DEFLATE stream, trailer CRC and ISIZE authenticate the
 * payload before this fallback exposes it. */
static bool w5_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    static const uint8_t sig[]={31,139,8};
    int64_t low,limit=pm_available(f),position,end;
    unsigned candidates=0;
    if(w6_scan(f,s,sig,3,0,true,true,w6_gz_at,pd)) return true;
    if(s->count || wg_stop(pd) || !w5_carrier(f,false,&low,pd) ||
       low<0 || low>=limit) return false;
    end=limit-low>16777216 ? low+16777216 : limit;
    for(position=low;position<=end-3;++position) {
        int64_t found=xx_io_find_bytes_buffer_optimize_ex(
            f->device,f->base_address+position,end-position,sig,sizeof(sig),
            xx_get_file_buffer_size(),pd);
        if(found<0 || wg_stop(pd) || ++candidates>8) break;
        position=found-f->base_address;
        if(w6_gz_at(f,s,position,pd)) return true;
        if(s->count) break;
    }
    return false;
}



static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w5_parse(f,s,pd) && wg_members(s,pd); }
void xx_sfx_gzip_init(xx_sfx_gzip *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_GZIP,"exe"); } }
xx_sfx_gzip *xx_sfx_gzip_create(xx_io_device *d,int64_t b) { xx_sfx_gzip *r=(xx_sfx_gzip *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_gzip_init(r,d,b); return r; }
void xx_sfx_gzip_destroy(xx_sfx_gzip *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_gzip_free(xx_sfx_gzip *r) { if(r) { xx_sfx_gzip_destroy(r); xx_mem_free(r); } }
bool xx_sfx_gzip_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_gzip_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
