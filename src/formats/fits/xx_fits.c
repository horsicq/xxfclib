/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://fits.gsfc.nasa.gov/standard40/fits_standard40aa-le.pdf
 * Stored encoded component extraction; no media decoding claims.
 */
#include "xxfclib/formats/fits/xx_fits.h"
#include "../xx_payload_members.h"

static bool fi_integer(const uint8_t *card,int64_t *value) {
    unsigned at=10; bool negative=false; uint64_t n=0; unsigned digits=0;
    if(card[8]!='=' || card[9]!=' ') return false; while(at<30 && card[at]==' ') ++at;
    if(at<30 && (card[at]=='+' || card[at]=='-')) negative=card[at++]=='-';
    while(at<30 && card[at]>='0' && card[at]<='9') { if(n>((uint64_t)INT64_MAX-(card[at]-'0'))/10) return false; n=n*10+card[at++]-'0'; ++digits; }
    while(at<30 && card[at]==' ') ++at; if(!digits || at!=30) return false; *value=negative ? -(int64_t)n : (int64_t)n; return true;
}
static bool fi_padding(Abstractformat *f,int64_t at,int64_t end,uint8_t expected,xx_pd_struct *pd) {
    size_t capacity=xx_get_file_buffer_size(); uint8_t *b=NULL; bool buffer_result=false; while(at<end) {if(!b) { if((uint64_t)(end-at)<capacity) capacity=(size_t)(end-at); b=(uint8_t *)xx_mem_alloc(capacity); if(!b) { buffer_result=false; goto buffer_done; } }  size_t n=(uint64_t)(end-at)>capacity ? capacity : (size_t)(end-at),i;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at,b,n)) { buffer_result = (false); goto buffer_done; } for(i=0;i<n;++i) if(b[i]!=expected) { buffer_result = (false); goto buffer_done; } at+=(int64_t)n; } { buffer_result = (true); goto buffer_done; }

buffer_done:
    xx_mem_free(b);
    return buffer_result;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    int64_t at=0,limit=pm_available(f); unsigned hdus=0; bool extend=false;
    while(at<limit) { uint8_t card[80]; int64_t start=at,bitpix=0,naxis=0,value,header_end,data_end,padded; uint64_t samples=0,bytes; unsigned cards=0,axis=0; bool ended=false; char label[48];
        if(++hdus>1024 || !pm_read(f,at,card,80)) return false;
        if(hdus==1) { if(xx_rt_memcmp(card,"SIMPLE  = ",10) || card[29]!='T') return false; }
        else { if(!extend) return false; if(xx_rt_memcmp(card,"XTENSION= 'IMAGE   '",20)) { if(!xx_rt_memcmp(card,"XTENSION",8)) return false; --hdus; break; } }
        for(;;) { unsigned i;
            if((pd && xx_pd_is_stopped(pd)) || ++cards>4096 || !pm_read(f,at,card,80)) return false;
            for(i=0;i<80;++i) if(card[i]<32 || card[i]>126) return false;
            if(cards==2) { if(xx_rt_memcmp(card,"BITPIX  ",8) || !fi_integer(card,&bitpix) || (bitpix!=8 && bitpix!=16 && bitpix!=32 && bitpix!=64 && bitpix!=-32 && bitpix!=-64)) return false; }
            else if(cards==3) { if(xx_rt_memcmp(card,"NAXIS   ",8) || !fi_integer(card,&naxis) || naxis<0 || naxis>16) return false; samples=naxis ? 1U : 0U; }
            else if(cards>=4 && cards<4+(unsigned)naxis) { char key[9]; unsigned n=cards-3; xx_rt_snprintf(key,sizeof(key),"NAXIS%-3u",n);
                if(xx_rt_memcmp(card,key,8) || !fi_integer(card,&value) || value<0 || ((uint64_t)value && samples>(uint64_t)INT64_MAX/(uint64_t)value)) return false; samples*=(uint64_t)value; ++axis;
            } else if(hdus>1 && cards==4+(unsigned)naxis) { if(xx_rt_memcmp(card,"PCOUNT  ",8) || !fi_integer(card,&value) || value) return false; }
            else if(hdus>1 && cards==5+(unsigned)naxis) { if(xx_rt_memcmp(card,"GCOUNT  ",8) || !fi_integer(card,&value) || value!=1) return false; }
            if(!xx_rt_memcmp(card,"EXTEND  = ",10)) { if(card[29]!='T' && card[29]!='F') return false; if(hdus==1) extend=card[29]=='T'; }
            if(!xx_rt_memcmp(card,"GROUPS  = ",10) && card[29]=='T') return false;
            if(!xx_rt_memcmp(card,"END     ",8)) { for(i=8;i<80;++i) if(card[i]!=' ') return false; ended=true; at+=80; break; }
            at+=80;
        }
        if(!ended || cards<4+(unsigned)naxis+(hdus>1 ? 2U : 0U) || axis!=(unsigned)naxis) return false;
        header_end=start+((at-start+2879)/2880)*2880; if(header_end>limit || !fi_padding(f,at,header_end,' ',pd)) return false;
        bytes=(uint64_t)(bitpix<0 ? -bitpix : bitpix)/8; if(bytes && samples>(uint64_t)INT64_MAX/bytes) return false; bytes*=samples;
        if(bytes>(uint64_t)(limit-header_end)) return false; data_end=header_end+(int64_t)bytes;
        if(data_end>INT64_MAX-2879) return false; padded=header_end+((int64_t)bytes+2879)/2880*2880; if(padded>limit || !fi_padding(f,data_end,padded,0,pd)) return false;
        xx_rt_snprintf(label,sizeof(label),"hdu-%u-header.txt",hdus-1); if(!pm_add(f,s,label,start,header_end-start)) return false;
        if(bytes) { xx_rt_snprintf(label,sizeof(label),"hdu-%u-array.bin",hdus-1); if(!pm_add(f,s,label,header_end,(int64_t)bytes)) return false; }
        at=padded;
    } s->size=at; return hdus>0;
}

void xx_fits_init(xx_fits *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_FITS,"fits"); } }
xx_fits *xx_fits_create(xx_io_device *d,int64_t b) { xx_fits *r=(xx_fits *)xx_mem_alloc(sizeof(*r)); if(r) xx_fits_init(r,d,b); return r; }
void xx_fits_destroy(xx_fits *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_fits_free(xx_fits *r) { if(r) { xx_fits_destroy(r); xx_mem_free(r); } }
bool xx_fits_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_fits_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
