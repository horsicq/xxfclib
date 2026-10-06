/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/erlang/otp/master/lib/stdlib/src/beam_lib.erl */
#include "xxfclib/formats/erlang_beam/xx_erlang_beam.h"
#include "../xx_seventh_data.h"

typedef struct beam_chunk {uint32_t id,size;uint64_t at;} beam_chunk;
static bool atoms(Abstractformat *f,const beam_chunk *chunk,unsigned *count,xx_pd_struct *pd) {
    uint8_t b[256];uint32_t n,i;uint64_t at=chunk->at+4,end=chunk->at+chunk->size;
    if(chunk->size<4 || !pm_read(f,(int64_t)chunk->at,b,4) || !(n=pm_be32(b)) || n>65536) return false;
    for(i=0;i<n;++i) {unsigned len;if(fd_stop(pd) || !fd_range(at,1,end) || !pm_read(f,(int64_t)at++,b,1)) return false;len=b[0];if(!fd_range(at,len,end) || !pm_read(f,(int64_t)at,b,len)) return false;
        if(chunk->id==UINT32_C(0x41745538) && !fourth_utf8(b,len,pd)) { return false; } at+=len;
    }if(at!=end) return false;*count=n;return true;
}
static bool table(Abstractformat *f,const beam_chunk *chunk,unsigned atom_count,unsigned labels,bool imports,xx_pd_struct *pd) {
    uint8_t b[12];uint32_t n,i,a,c;
    if(chunk->size<4 || !pm_read(f,(int64_t)chunk->at,b,4) || (n=pm_be32(b))>65536 || chunk->size!=4+(uint64_t)n*12) return false;
    for(i=0;i<n;++i) {if(fd_stop(pd) || !pm_read(f,(int64_t)(chunk->at+4+(uint64_t)i*12),b,12)) return false;a=pm_be32(b);c=pm_be32(b+8);
        if(!a || a>atom_count || (imports && (!pm_be32(b+4) || pm_be32(b+4)>atom_count || c>255)) || (!imports && (pm_be32(b+4)>255 || !c || c>=labels))) return false;
    }return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    beam_chunk chunks[128];uint8_t h[20];uint64_t end,at=12;unsigned count=0,i,natoms=0,labels=0;int atom=-1,code=-1,str=-1,imp=-1,exp=-1;int64_t available=pm_available(f);
    if(fd_stop(pd) || available<12 || !pm_read(f,0,h,12) || xx_rt_memcmp(h,"FOR1",4) || xx_rt_memcmp(h+8,"BEAM",4)) return false;
    end=8+(uint64_t)pm_be32(h+4);if(end<12 || end>(uint64_t)available || end>67108864) return false;
    while(at<end) {uint32_t id,n;uint64_t padded;if(fd_stop(pd) || count==128 || !fd_range(at,8,end) || !pm_read(f,(int64_t)at,h,8)) return false;
        id=pm_be32(h);n=pm_be32(h+4);for(i=0;i<4;++i) if(h[i]<32 || h[i]>126) return false;padded=((uint64_t)n+3)&~UINT64_C(3);
        if(!fd_range(at+8,padded,end)) { return false; } for(i=0;i<count;++i) if(chunks[i].id==id) return false;
        chunks[count].id=id;chunks[count].size=n;chunks[count].at=at+8;
        if(id==UINT32_C(0x41746f6d) || id==UINT32_C(0x41745538)) {if(atom>=0) return false;atom=(int)count;}
        if(id==UINT32_C(0x436f6465)) { code=(int)count; } if(id==UINT32_C(0x53747254)) str=(int)count;
        if(id==UINT32_C(0x496d7054)) { imp=(int)count; } if(id==UINT32_C(0x45787054)) exp=(int)count;
        ++count;at+=8+padded;
    }
    if(at!=end || atom<0 || code<0 || str<0 || imp<0 || exp<0 || !atoms(f,chunks+atom,&natoms,pd) || chunks[code].size<21 || !pm_read(f,(int64_t)chunks[code].at,h,20)) return false;
    if(pm_be32(h)<16 || pm_be32(h)>chunks[code].size-5 || pm_be32(h+4) || !pm_be32(h+8) || pm_be32(h+8)>255 || !(labels=pm_be32(h+12)) || labels>1000000 || !pm_be32(h+16) || pm_be32(h+16)>labels || !table(f,chunks+imp,natoms,labels,true,pd) || !table(f,chunks+exp,natoms,labels,false,pd)) return false;
    if(!pm_add(f,s,"beam-header.bin",0,12)) return false;
    for(i=0;i<count;++i) {char label[64];xx_rt_snprintf(label,sizeof(label),"chunk-%u.bin",i);
        if(!pm_add(f,s,label,(int64_t)chunks[i].at,chunks[i].size)) return false;
    }s->size=(int64_t)end;return true;
}

void xx_erlang_beam_init(xx_erlang_beam *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ERLANG_BEAM,"erlang_beam"); } }
xx_erlang_beam *xx_erlang_beam_create(xx_io_device *d,int64_t b) { xx_erlang_beam *r=(xx_erlang_beam *)xx_mem_alloc(sizeof(*r)); if(r) xx_erlang_beam_init(r,d,b); return r; }
void xx_erlang_beam_destroy(xx_erlang_beam *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_erlang_beam_free(xx_erlang_beam *r) { if(r) { xx_erlang_beam_destroy(r); xx_mem_free(r); } }
bool xx_erlang_beam_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_erlang_beam_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
