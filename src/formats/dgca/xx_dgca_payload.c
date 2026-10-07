/* Independent DGCA payload codecs. SPDX-License-Identifier: MIT. */
#include "dgca_codec.h"
#include "dgca_entropy.h"
#include "dgca_lzp.h"
#include "dgca_transform.h"
#include <string.h>
#include "xxfclib/data/xx_data.h"
static int stop(const dg_callbacks *cb) { return cb->cancelled&&cb->cancelled(cb->opaque); }
static dg_status copy(const dg_callbacks *cb,unsigned char *out,const unsigned char *in,size_t n) {
    while(n){size_t part=n>4096?4096:n;if(stop(cb))return DG_CANCELLED;memcpy(out,in,part);out+=part;in+=part;n-=part;}return DG_OK;
}
static int unit_size(const unsigned char *in,size_t size,size_t *total,size_t *raw) {
    uint32_t method;uint64_t n;
    if(size<8) {return 0; } method=xx_data_get_u32(in, 4, 0, false);*raw=xx_data_get_u32(in+4, 4, 0, false);
    if(!method)n=8+(uint64_t)*raw;
    else if(method==1&&size>=32)n=32+(uint64_t)xx_data_get_u32(in+24, 4, 0, false)+xx_data_get_u32(in+28, 4, 0, false);
    else return 0;
    if(n>size||n>SIZE_MAX) {return 0; } *total=(size_t)n;return 1;
}
dg_status dg_codec_output_size(const dg_callbacks *cb,const unsigned char *in,size_t size,size_t *raw) {
    size_t total,part,at=8,sum=0;uint32_t method,count,i;
    if(!cb||!in||!raw||size<8) {return DG_FORMAT; } if(stop(cb))return DG_CANCELLED;method=xx_data_get_u32(in, 4, 0, false);
    if(method<2){if(!unit_size(in,size,&total,raw)||total!=size)return DG_FORMAT;return DG_OK;}
    if(method!=2)return DG_UNSUPPORTED_CODEC;
    count=xx_data_get_u32(in+4, 4, 0, false);if(!count||count>(size-8)/8)return DG_FORMAT;
    for(i=0;i<count;i++) {
        if(!(i&4095)&&stop(cb))return DG_CANCELLED;
        if(at>size||!unit_size(in+at,size-at,&total,&part)||part>SIZE_MAX-sum)return DG_FORMAT;
        at+=total;sum+=part;
    }
    if(at!=size) {return DG_FORMAT; } *raw=sum;return DG_OK;
}
static dg_status unit(const dg_callbacks *cb,const unsigned char *in,size_t size,
                      unsigned char *out,size_t raw,int apply_filter) {
    unsigned char *literals=NULL,*sorted=NULL;uint32_t *links=NULL;
    uint32_t method,flags,params=0,primary;size_t total,declared,count,a,b,i;
    dg_status status=DG_FORMAT;
    if(!unit_size(in,size,&total,&declared)||total!=size||declared!=raw)return DG_FORMAT;
    method=xx_data_get_u32(in, 4, 0, false);
    if(!method)return copy(cb,out,in+8,raw);
    flags=xx_data_get_u32(in+8, 4, 0, false);params=xx_data_get_u32(in+12, 4, 0, false);primary=xx_data_get_u32(in+16, 4, 0, false);count=xx_data_get_u32(in+20, 4, 0, false);a=xx_data_get_u32(in+24, 4, 0, false);b=xx_data_get_u32(in+28, 4, 0, false);
    if((flags&~UINT32_C(0x000f0007))||((flags>>16)&15)>2||count>raw||a<2||(!(flags&4)&&(count!=raw||b)))return DG_FORMAT;
    if(stop(cb))return DG_CANCELLED;
    if(count) {
        literals=(unsigned char *)cb->allocate(cb->opaque,count);if(!literals)return DG_MEMORY;
        status=dg_entropy_decode(cb,in+32,a,literals,count,params);if(status!=DG_OK)goto done;
        if(flags&2) {
            int transformed;
            if(count>SIZE_MAX/sizeof(*links)){status=DG_MEMORY;goto done;}
            sorted=(unsigned char *)cb->allocate(cb->opaque,count);
            if(!sorted){status=DG_MEMORY;goto done;}
            links=(uint32_t *)cb->allocate(cb->opaque,count*sizeof(*links));
            if(!links){status=DG_MEMORY;goto done;}
            transformed=dg_inverse_bwt(literals,sorted,count,primary,links,cb->cancelled,cb->opaque);
            if(transformed!=1){status=transformed<0?DG_CANCELLED:DG_FORMAT;goto done;}
            cb->release(cb->opaque,literals);literals=sorted;sorted=NULL;
            cb->release(cb->opaque,links);links=NULL;
        }
    }else if(primary){status=DG_FORMAT;goto done;}
    if(flags&4)status=dg_lzp_expand(cb,literals,count,in+32+a,b,out,raw,params);
    else status=copy(cb,out,literals,raw);
    if(status==DG_OK&&apply_filter) {
        unsigned order=(flags>>16)&15,pass;
        for(pass=0;pass<order;pass++)for(i=1;i<raw;i++) {
            if(!(i&4095)&&stop(cb)){status=DG_CANCELLED;goto done;}
            out[i]=(unsigned char)(out[i]+out[i-1]);
        }
    }
done:
    if(links)cb->release(cb->opaque,links);
    if(sorted)cb->release(cb->opaque,sorted);
    if(literals)cb->release(cb->opaque,literals);
    return status;
}
dg_status dg_codec_decode(const dg_callbacks *cb,const unsigned char *in,size_t size,
                          unsigned char *out,size_t raw) {
    uint32_t method,stride,plane;size_t at,part,total,unit_raw,combined=0;
    dg_status status;
    if(!cb||!cb->allocate||!cb->release||!in||size<8||(raw&&!out))return DG_FORMAT;
    if(stop(cb)) {return DG_CANCELLED; } method=xx_data_get_u32(in, 4, 0, false);
    if(method<2)return unit(cb,in,size,out,raw,0);
    if(method!=2)return DG_UNSUPPORTED_CODEC;
    stride=xx_data_get_u32(in+4, 4, 0, false);if(!stride||stride>(size-8)/8||(raw&&stride>raw))return DG_FORMAT;
    at=8;
    for(plane=0;plane<stride;plane++) {
        size_t wanted=raw/stride+(plane<raw%stride);
        if(!(plane&4095)&&stop(cb))return DG_CANCELLED;
        if(at>size||!unit_size(in+at,size-at,&total,&unit_raw)||unit_raw!=wanted)return DG_FORMAT;
        at+=total;if(unit_raw>SIZE_MAX-combined)return DG_FORMAT;combined+=unit_raw;
    }
    if(at!=size||combined!=raw)return DG_FORMAT;
    at=8;
    for(plane=0;plane<stride;plane++) {
        unsigned char *bytes;size_t offset=plane;
        if(stop(cb))return DG_CANCELLED;
        if(!unit_size(in+at,size-at,&total,&unit_raw))return DG_FORMAT;
        bytes=(unsigned char *)cb->allocate(cb->opaque,unit_raw?unit_raw:1);if(!bytes)return DG_MEMORY;
        status=unit(cb,in+at,total,bytes,unit_raw,1);
        if(status==DG_OK) for(part=0;part<unit_raw;part++) {
            if(!(part&4095)&&stop(cb)){status=DG_CANCELLED;break;}
            if(offset>=raw){status=DG_FORMAT;break;}
            out[offset]=bytes[part];
            if(part+1<unit_raw)offset+=stride;
        }
        cb->release(cb->opaque,bytes);if(status!=DG_OK)return status;at+=total;
    }
    return DG_OK;
}
