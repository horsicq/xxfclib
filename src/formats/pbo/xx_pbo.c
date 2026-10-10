/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bohemia PBO archive.
 * https://community.bohemia.net/wiki/PBO
 */
#include "xxfclib/formats/pbo/xx_pbo.h"
#include "../xx_game_resource_helpers.h"
#include "../xx_format_abstract_extractor_adapter.h"

typedef struct pbo_input { Abstractformat *f; uint64_t next,end; size_t pos,count; uint8_t bytes[8192]; } pbo_input;
static bool pbo_byte(pbo_input *in,unsigned *value) {
    if(in->pos==in->count) {
        uint64_t left=in->end-in->next;
        in->count=left<sizeof(in->bytes)?(size_t)left:sizeof(in->bytes);in->pos=0;
        if(!in->count || !pm_read(in->f,(int64_t)in->next,in->bytes,in->count)) return false;
        in->next+=in->count;
    }
    *value=in->bytes[in->pos++];return true;
}
/* PBO compressed bytes use a 4 KiB space-filled history and a trailing
 * additive checksum. Decode into a fixed buffer even during archive TEST. */
static bool pbo_pbo_read(Abstractformat *f,pm_member *m,xx_io_device *out,xx_pd_struct *pd) {
    uint8_t ring[4096],buffer[4096],b[4];uint64_t p=(uint64_t)(m->offset-f->base_address),end=p+(uint64_t)m->packed_size,produced=0;size_t used=0;unsigned flags=0,history=0;uint32_t checksum=0;pbo_input in;
    memset(ring,32,sizeof(ring));
    if(m->packed_size<4) return false;
    xx_mem_zero(&in,sizeof(in));in.f=f;in.next=p;in.end=end-4;
    while(produced<(uint64_t)m->size) {
        unsigned c,count=1,from=0;
        if(xgr_stop(pd)) return false;
        flags>>=1;
        if(!(flags&256)) { unsigned bits;if(!pbo_byte(&in,&bits)) return false;flags=bits|0xff00U; }
        if(!pbo_byte(&in,&c)) return false;
        if(!(flags&1)) {
            unsigned d;if(!pbo_byte(&in,&d)) return false;
            from=(history-((d>>4)*256U+c))&4095U;count=(d&15U)+3U;
        }
        if(count>(uint64_t)m->size-produced) return false;
        while(count--) {
            if(!(flags&1)) { c=ring[from];from=(from+1)&4095U; }
            ring[history]=(uint8_t)c;history=(history+1)&4095U;buffer[used++]=(uint8_t)c;checksum+=c;++produced;
            if(used==sizeof(buffer)) { if(!xgr_write(out,buffer,used)) return false;used=0; }
        }
    }
    /* Any unused bits in the final flag byte need no extra token bytes. */
    if(in.next-(in.count-in.pos)!=end-4 || !pm_read(f,(int64_t)(end-4),b,4) || checksum!=xgr_le32(b)) return false;
    return !xgr_stop(pd) && xgr_write(out,buffer,used);
}
static bool pbo_pbo(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint64_t n=(uint64_t)pm_available(f),p=0,data;uint8_t h[20];uint32_t i;bool properties=false;
    if(n<21) return false;
    for(;;) {
        char name[XGR_MAX_NAME+1];uint32_t method,size,original;
        if(xgr_stop(pd) || !xgr_zstr(f,&p,n,name) || !pm_read(f,(int64_t)p,h,20)) return false;p+=20;
        method=xgr_le32(h);original=xgr_le32(h+4);size=xgr_le32(h+16);
        if(!name[0]) {
            if(method==0x56657273U && !s->count && !properties) {
                properties=true;
                if(original || xgr_le32(h+8) || xgr_le32(h+12) || size) return false;
                for(;;) { char value[XGR_MAX_NAME+1];if(!xgr_zstr(f,&p,n,name)) return false;if(!name[0]) break;if(!xgr_zstr(f,&p,n,value)) return false; }
                continue;
            }
            /* OFP also uses the last compression tag in the empty sentinel. */
            if((method!=0 && method!=0x43707273U) || original || xgr_le32(h+8) || xgr_le32(h+12) || size) return false;
            break;
        }
        if((method!=0 && method!=0x43707273U) || xgr_le32(h+8) || (method==0 && original && original!=size) || (method!=0 && size<4) || size>n) return false;
        if(!xgr_add(f,s,name,0,size)) return false;
        if(method) { pm_member *m=&s->items[s->count-1];m->size=original;m->compression_method=1;m->read_all=pbo_pbo_read; }
    }
    data=p;
    for(i=0;i<s->count;++i) { pm_member *m=&s->items[i];if(!xgr_range(data,(uint64_t)m->packed_size,n)) return false;m->offset=f->base_address+(int64_t)data;data+=(uint64_t)m->packed_size; }
    /* Classic PBO has no overall trailer. Refuse unknown trailing material,
     * rather than claim to validate an unimplemented signature variant. */
    if(data!=n || !s->count) return false;
    s->size=(int64_t)n;return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    return pm_available(f)>=0 && !xgr_stop(pd) && pbo_pbo(f,s,pd);
}
Abstractformat *xx_pbo_create(xx_io_device *d,int64_t base) {
    Abstractformat *f=(Abstractformat *)xx_mem_alloc(sizeof(*f));
    if(!f) return NULL;
    xx_mem_zero(f,sizeof(*f));pm_init(f,d,base,XX_FILE_TYPE_PBO,"pbo");return f;
}
void xx_pbo_free(Abstractformat *f) {
    if(f) { xx_format_cleanup_extra_parameters(f);xx_mem_free(f); }
}
xx_file_type_t xx_pbo_detect(xx_io_device *d,int64_t base) {
    Abstractformat f;uint8_t h[64];int64_t size,old=xx_io_tell(d);
    xx_file_type_t type=XX_FILE_TYPE_UNKNOWN;
    xx_mem_zero(&f,sizeof(f));f.device=d;f.base_address=base;size=pm_available(&f);
    if(size<8 || !pm_read(&f,0,h,(size_t)(size<64?size:64))) goto done;
    size_t i,limit=(size_t)(size<64?size:64);
    uint32_t magic=xgr_le32(h);
    /* A signatureless PBO must not supersede a recognized resource header. */
    if(magic==0x32524556U || magic==0x4b504650U ||
       magic==0x0012ec9cU || magic==0x773e8b56U || magic==0x00ba09b8U ||
       magic==0x5a495558U ||
       (magic==0x5f4d4150U && size>=36 && !memcmp(h,"PAM_PAK\0",8)) ||
       (magic==0x00435241U && size>=32 && xgr_le32(h+4)==1) ||
       magic==0x504a4743U || magic==0x53524950U ||
       magic==0x4556494cU || magic==0x204e4f43U ||
       (size>=62 && !memcmp(h,"Photodex Presenter Stream\x1a\n\r\n\x1a",30)) ||
       (size>=52 && !memcmp(h,"SBPAK V 1.0\r\n",13))) goto done;
    /* Preserve the complete bounded-entry heuristic and full table probe. */
    if(h[0]==0 && size>=21 && xgr_le32(h+1)==0x56657273U) type=XX_FILE_TYPE_PBO;
    else for(i=0;i<limit;++i) {
        if(!h[i]) {
            if(i>0 && i+21<=limit &&
               (xgr_le32(h+i+1)==0 || xgr_le32(h+i+1)==0x43707273U) &&
               xgr_le32(h+i+9)==0) type=XX_FILE_TYPE_PBO;
            break;
        }
        if(h[i]<32 || h[i]>=127) break;
    }
    if(type==XX_FILE_TYPE_PBO) {
        Abstractformat *probe=xx_pbo_create(d,base);
        bool valid=probe && pm_valid(probe,NULL);
        xx_pbo_free(probe);
        if(!valid) type=XX_FILE_TYPE_UNKNOWN;
    }
done:
    if(old>=0) xx_io_seek64(d,old,SEEK_SET);
    return type;
}
static Abstractformat *pbo_open(xx_io_device *d) { return xx_pbo_create(d,0); }
static const xx_file_type_t pbo_types[]={XX_FILE_TYPE_PBO};
static const xx_format_search_desc pbo_desc={
    pbo_types,1,NULL,0,pbo_open,xx_pbo_free,true
};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(pbo,pbo_desc)
