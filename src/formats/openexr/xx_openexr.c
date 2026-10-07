/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://openexr.com/en/latest/OpenEXRFileLayout.html
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#include "xxfclib/formats/openexr/xx_openexr.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool ex_string(Abstractformat *f,int64_t *at,int64_t end,char *text,unsigned capacity) {
    unsigned n=0; uint8_t b;
    do { if(*at>=end || !pm_read(f,(*at)++,&b,1) || n+1>=capacity) return false; text[n++]=(char)b; } while(b); return true;
}
typedef struct ex_channel { uint32_t bytes,xs,ys; } ex_channel;
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[24]; int64_t at=8,limit=pm_available(f),table,end; uint32_t version,compression=UINT32_MAX,mask=0,height,width,chcount=0; int32_t xmin=0,ymin=0,xmax=0,ymax=0; ex_channel channels[256]; unsigned block,chunk_count,i; size_t first_chunk;
    static const unsigned lines[12]={1,1,1,16,32,16,32,32,32,256,256,32};
    if(!pm_read(f,0,h,8) || xx_data_get_u32(h, 4, 0, false)!=20000630U || ((version=xx_data_get_u32(h+4, 4, 0, false))&255)!=2 || (version&~0x402U)) return false;
    for(;;) { char name[258],type[258],label[96]; uint32_t n;
        if((pd && xx_pd_is_stopped(pd)) || !ex_string(f,&at,limit,name,(version&0x400) ? 258U : 34U)) { return false; } if(!name[0]) break;
        if(!ex_string(f,&at,limit,type,(version&0x400) ? 258U : 34U) || limit-at<4 || !pm_read(f,at,h,4)) { return false; } n=xx_data_get_u32(h, 4, 0, false); at+=4;
        if(n>INT32_MAX || n>(uint64_t)(limit-at)) return false;
        if(!xx_rt_strcmp(name,"channels")) { int64_t p=at,stop=at+n; if(mask&1 || xx_rt_strcmp(type,"chlist")) return false;
            for(;;) { char channel[258]; if(!ex_string(f,&p,stop,channel,(version&0x400) ? 258U : 34U)) return false; if(!channel[0]) break;
                if(chcount==256 || stop-p<16 || !pm_read(f,p,h,16) || xx_data_get_u32(h, 4, 0, false)>2 || h[4]>1 || h[5] || h[6] || h[7] || !xx_data_get_u32(h+8, 4, 0, false) || !xx_data_get_u32(h+12, 4, 0, false) || xx_data_get_u32(h+8, 4, 0, false)>INT32_MAX || xx_data_get_u32(h+12, 4, 0, false)>INT32_MAX) return false;
                channels[chcount].bytes=xx_data_get_u32(h, 4, 0, false)==1 ? 2U : 4U; channels[chcount].xs=xx_data_get_u32(h+8, 4, 0, false); channels[chcount++].ys=xx_data_get_u32(h+12, 4, 0, false); p+=16;
            } if(p!=stop || !chcount) return false; mask|=1;
        } else if(!xx_rt_strcmp(name,"compression")) { if(mask&2 || xx_rt_strcmp(type,"compression") || n!=1 || !pm_read(f,at,h,1) || h[0]>11) return false; compression=h[0]; mask|=2; }
        else if(!xx_rt_strcmp(name,"dataWindow")) { if(mask&4 || xx_rt_strcmp(type,"box2i") || n!=16 || !pm_read(f,at,h,16)) return false; xmin=(int32_t)xx_data_get_u32(h, 4, 0, false); ymin=(int32_t)xx_data_get_u32(h+4, 4, 0, false); xmax=(int32_t)xx_data_get_u32(h+8, 4, 0, false); ymax=(int32_t)xx_data_get_u32(h+12, 4, 0, false); if(xmin>xmax || ymin>ymax) return false; mask|=4; }
        else if(!xx_rt_strcmp(name,"displayWindow")) { if(mask&8 || xx_rt_strcmp(type,"box2i") || n!=16 || !pm_read(f,at,h,16) || (int32_t)xx_data_get_u32(h, 4, 0, false)>(int32_t)xx_data_get_u32(h+8, 4, 0, false) || (int32_t)xx_data_get_u32(h+4, 4, 0, false)>(int32_t)xx_data_get_u32(h+12, 4, 0, false)) return false; mask|=8; }
        else if(!xx_rt_strcmp(name,"lineOrder")) { if(mask&16 || xx_rt_strcmp(type,"lineOrder") || n!=1 || !pm_read(f,at,h,1) || h[0]>2) return false; mask|=16; }
        else if(!xx_rt_strcmp(name,"pixelAspectRatio")) { if(mask&32 || xx_rt_strcmp(type,"float") || n!=4 || !pm_read(f,at,h,4) || (xx_data_get_u32(h, 4, 0, false)&0x80000000U) || !(xx_data_get_u32(h, 4, 0, false)&0x7FFFFFFFU) || (xx_data_get_u32(h, 4, 0, false)&0x7F800000U)==0x7F800000U) return false; mask|=32; }
        else if(!xx_rt_strcmp(name,"screenWindowCenter")) { if(mask&64 || xx_rt_strcmp(type,"v2f") || n!=8) return false; mask|=64; }
        else if(!xx_rt_strcmp(name,"screenWindowWidth")) { if(mask&128 || xx_rt_strcmp(type,"float") || n!=4) return false; mask|=128; }
        else if(!xx_rt_strcmp(name,"tiles") || !xx_rt_strcmp(name,"chunkCount")) return false;
        { char safe[61]; unsigned j; for(j=0;j<60 && name[j];++j) { unsigned char b=(unsigned char)name[j]; safe[j]=((b>='a' && b<='z') || (b>='A' && b<='Z') || (b>='0' && b<='9') || b=='_' || b=='-') ? (char)b : '_'; } safe[j]=0;
            xx_rt_snprintf(label,sizeof(label),"attribute-%s.bin",safe); }
        if(!pm_add(f,s,label,at,n)) { return false; } at+=n;
    }
    if(mask!=255) return false;
    if((int64_t)xmax-xmin+1>UINT32_MAX || (int64_t)ymax-ymin+1>UINT32_MAX) return false;
    width=(uint32_t)((int64_t)xmax-xmin+1); height=(uint32_t)((int64_t)ymax-ymin+1); block=lines[compression];
    /* Keep overlap validation bounded for attacker-controlled offset tables. */
    if(((uint64_t)height+block-1)/block>4096) { return false; } chunk_count=(height+block-1)/block; table=at;
    if((uint64_t)chunk_count*8>(uint64_t)(limit-table)) { return false; } end=table+(int64_t)chunk_count*8; first_chunk=s->count;
    for(i=0;i<chunk_count;++i) { uint64_t offset; uint32_t bytes; int32_t y; unsigned j; char name[48]; uint64_t raw=0;
        if(pd && xx_pd_is_stopped(pd)) return false;
        if(!pm_read(f,table+(int64_t)i*8,h,8) || (offset=xx_data_get_u64(h, 8, 0, false))>(uint64_t)limit || offset<(uint64_t)(table+(int64_t)chunk_count*8) || limit-(int64_t)offset<8 || !pm_read(f,(int64_t)offset,h,8)) return false;
        y=(int32_t)xx_data_get_u32(h, 4, 0, false); bytes=xx_data_get_u32(h+4, 4, 0, false);
        if((int64_t)y!=(int64_t)ymin+(int64_t)i*block || !bytes || bytes>(uint64_t)(limit-(int64_t)offset-8)) return false;
        for(j=0;j<i;++j) { pm_member *m=&s->items[first_chunk+j]; int64_t start=m->offset-f->base_address-8,stop=start+8+m->size;
            if(!(j&255U) && pd && xx_pd_is_stopped(pd)) return false;
            if((int64_t)offset<stop && (int64_t)offset+8+bytes>start) return false; }
        if(!compression) { unsigned k; for(k=0;k<chcount;++k) { uint64_t n; int64_t first=xmin,last=xmax;
                if(y%(int32_t)channels[k].ys) continue;
                /* Count sample positions divisible by xSampling, including negative windows. */
                { int64_t remainder=first%(int32_t)channels[k].xs; if(remainder) first+=remainder<0 ? -remainder : (int64_t)channels[k].xs-remainder; }
                n=first>last ? 0U : (uint64_t)(last-first)/(uint64_t)channels[k].xs+1; raw+=n*channels[k].bytes;
            } if(raw!=bytes) return false; }
        xx_rt_snprintf(name,sizeof(name),"scanline-%u.encoded",i); if(!pm_add(f,s,name,(int64_t)offset+8,bytes)) return false; if((int64_t)offset+8+bytes>end) end=(int64_t)offset+8+bytes;
    }
    (void)width; s->size=end; return true;
}

void xx_openexr_init(xx_openexr *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_OPENEXR,"openexr"); } }
xx_openexr *xx_openexr_create(xx_io_device *d,int64_t b) { xx_openexr *r=(xx_openexr *)xx_mem_alloc(sizeof(*r)); if(r) xx_openexr_init(r,d,b); return r; }
void xx_openexr_destroy(xx_openexr *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_openexr_free(xx_openexr *r) { if(r) { xx_openexr_destroy(r); xx_mem_free(r); } }
bool xx_openexr_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_openexr_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
