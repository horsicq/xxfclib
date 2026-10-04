/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * S-Pack PACK/FI records plus trailing compressed INDX. All index/member
 * correlations and CRCs are checked. Reference: IFF-CDAF.c SPack framing.
 * Single-volume input only; continuation headers are never silently skipped. */
#include "xxfclib/formats/spack/xx_spack.h"
#include "../xx_legacy_archive.h"
#include "../xx_shrink_cdaf_codec.h"
static bool spack_parse(Abstractformat *f,pm_stream *s,ac_blob *b) {
    uint32_t start=0,index,footer,index_size,n,packed,at=4,pos=12; uint8_t *directory;
    if(b->n>6628U && pm_be32(b->p)==0x3F3U && pm_be32(b->p+40U)==0x42ADFFFCU &&
        pm_be32(b->p+48U)==0x2C780004U && pm_be32(b->p+52U)==0x4EAEFE68U && pm_be32(b->p+60U)==0x208093C9U && pm_be32(b->p+64U)==0x4EAEFEDAU) start=b->p[47U]==0xF4U?6624U:6616U;
    if(!ac_span(b,start,6U) || xx_rt_memcmp(b->p+start,"PACKFI",6) || b->n<start+26U) return false;
    if(!xx_rt_memcmp(b->p+b->n-12U,"INDX",4)) { footer=b->n-12U; index_size=pm_be32(b->p+footer+4U); }
    else if(!xx_rt_memcmp(b->p+b->n-8U,"INDX",4)) { footer=b->n-8U; index_size=pm_be32(b->p+footer+4U); }
    else return false;
    if(index_size>footer-start-4U || index_size<14U) return false; index=footer-index_size;
    if(!ac_span(b,index,14U) || pm_be16(b->p+index)!=0x4649U) return false;
    n=pm_be32(b->p+index+4U); packed=pm_be32(b->p+index+8U);
    if(packed!=index_size-14U || n<12U) return false;
    directory=ac_alloc(b,n); if(!directory) return false;
    if(!sc_decode(b,b->p+index+14U,packed,directory,n,7U) || ac_crc16(directory,n)!=pm_be16(b->p+index+12U)) goto fail;
    at+=start;
    while(pos<n) { uint32_t name_n,size,stored,record; uint16_t id,number; char name[96]; uint8_t *out;
        if(!ac_poll(b) || n-pos<13U || index-at<8U) goto fail;
        name_n=pm_be16(directory+pos+6U);
        if(pm_be16(directory+pos+2U)!=0x4649U || !name_n || name_n>90U || name_n>n-pos-13U || !ac_name(name,sizeof(name),directory+pos+8U,name_n)) goto fail;
        id=pm_be16(directory+pos+4U); size=pm_be32(directory+pos+8U+name_n);
        if(directory[pos+12U+name_n]) goto fail;
        number=pm_be16(b->p+at+2U);
        if(pm_be16(b->p+at)!=0x4649U || (number&0x7FFFU)!=id || pm_be32(b->p+at+4U)!=size) goto fail;
        if(number&0x8000U) {
            if(size>index-at-8U || !ac_emit(f,s,b,name,at+8U,size)) goto fail; record=8U+size;
        } else {
            if(index-at<14U) goto fail; stored=pm_be32(b->p+at+8U);
            if(stored>index-at-14U) goto fail; out=ac_alloc(b,size); if(!out) goto fail;
            if(!sc_decode(b,b->p+at+14U,stored,out,size,7U) || ac_crc16(out,size)!=pm_be16(b->p+at+12U)) { ac_release(b,out,size); goto fail; }
            if(!ac_memory(f,s,b,name,out,size,stored,7U)) goto fail; record=14U+stored;
        }
        at+=record; pos+=13U+name_n;
    }
    if(at!=index || pos!=n || !s->count) goto fail;
    ac_release(b,directory,n); return true;
fail: ac_release(b,directory,n); return ac_error(b,"S-Pack index/member bounds, continuation or CRC invalid");
}
AC_PARSE(spack_parse)
AC_DEFINE(spack,XX_FILE_TYPE_SPACK,"spk")
