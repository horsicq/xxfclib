/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.hdfgroup.org/archive/support/ftp/HDF/prev-Documentation/HDF4r15_SpecDG.pdf */
#include "xxfclib/formats/hdf4/xx_hdf4.h"
#include "../xx_fifth_data.h"

typedef struct hd_dd { uint16_t tag,ref; uint32_t at,n; } hd_dd;
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[12]; uint32_t block=4,visited[1024],starts[1024],ends[1024],blocks=0,total=0; hd_dd *entries=NULL; uint64_t end=4; bool result=false;
    if(!fd_equal(f,0,"\x0e\x03\x13\x01",4)) return false; entries=(hd_dd *)xx_mem_alloc(4096*sizeof(*entries)); if(!entries) return false;
    while(block) { uint16_t count; uint32_t next,i,j,tableend;
        if(fd_stop(pd) || blocks==1024 || !pm_read(f,block,h,6)) goto done;
        for(i=0;i<blocks;++i) if(visited[i]==block) goto done;
        count=pm_be16(h); next=pm_be32(h+2); if(!count || count>4096-total || !fd_range(block,6+(uint64_t)count*12,(uint64_t)pm_available(f))) goto done;
        tableend=block+6+count*12; if(tableend<block) goto done; visited[blocks]=starts[blocks]=block; ends[blocks++]=tableend; if(tableend>end) end=tableend;
        for(i=0;i<count;++i) { hd_dd d; if(fd_stop(pd) || !pm_read(f,block+6+(int64_t)i*12,h,12)) goto done;
            d.tag=pm_be16(h); d.ref=pm_be16(h+2); d.at=pm_be32(h+4); d.n=pm_be32(h+8);
            if(d.tag==1) continue; if(d.tag<=1 || (d.tag&0x4000) || !d.ref || d.at<4 || !fd_range(d.at,d.n,(uint64_t)pm_available(f))) goto done;
            for(j=0;j<total;++j) if(entries[j].tag==d.tag && entries[j].ref==d.ref) goto done;
            entries[total++]=d; if((uint64_t)d.at+d.n>end) end=(uint64_t)d.at+d.n;
        } block=next;
    }
    { uint32_t i,j; for(i=0;i<blocks;++i) for(j=0;j<i;++j) if(starts[i]<ends[j] && starts[j]<ends[i]) goto done;
      for(i=0;i<total;++i) { hd_dd *d=&entries[i]; char name[48];
        for(j=0;j<blocks;++j) if(d->n && d->at<ends[j] && starts[j]<(uint64_t)d->at+d->n) goto done;
        xx_rt_snprintf(name,sizeof(name),"tag-%u-ref-%u.bin",d->tag,d->ref); if(!pm_add(f,s,name,d->at,d->n)) goto done;
      }
    }
    if(!total || !fd_disjoint(s,(uint64_t)f->base_address+4)) goto done; s->size=(int64_t)end; result=true;
done: xx_mem_free(entries); return result;
}

void xx_hdf4_init(xx_hdf4 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_HDF4,"hdf4"); } }
xx_hdf4 *xx_hdf4_create(xx_io_device *d,int64_t b) { xx_hdf4 *r=(xx_hdf4 *)xx_mem_alloc(sizeof(*r)); if(r) xx_hdf4_init(r,d,b); return r; }
void xx_hdf4_destroy(xx_hdf4 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_hdf4_free(xx_hdf4 *r) { if(r) { xx_hdf4_destroy(r); xx_mem_free(r); } }
bool xx_hdf4_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_hdf4_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
