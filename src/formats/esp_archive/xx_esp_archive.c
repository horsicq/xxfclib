/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/archives/xesparchive.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/esp_archive/xx_esp_archive.h"
#include "../common/xx_sfx_carrier.h"

#include "xx_esp_directory.h"
#include "xxfclib/algo/ain/xx_ain.h"
static void esp_directory_unscramble(uint8_t *p,size_t n) {
    static const uint8_t key[4]={'G','y','i','K'};size_t i;
    for(i=0;i<n;++i)p[i]^=key[i&3];
}
static bool esp_directory_at(Abstractformat *f,pm_stream *s,int64_t base,xx_pd_struct *pd) {
    uint8_t h[10],*packed=NULL,*dir=NULL,*body=NULL,*plain=NULL,*prefix=NULL;
    char *paths=NULL;size_t path_size=0,dsize=0,count,i,stream_size=0,position=0,prefix_size=0;
    int64_t limit=pm_available(f),directory,body_size;bool ok=false;uint64_t aggregate=0;
    if(!pm_read(f,base,h,10) || xx_rt_memcmp(h,"ESP>",4) || (h[5]&7)>4 ||
       (h[5]&0x50) || ((h[5]&8) && h[4]>=0x19) || xx_data_get_u32(h+6, 4, 0, false)<10 ||
       !carrier_range(limit,base,xx_data_get_u32(h+6, 4, 0, false)))return false;
    directory=base+xx_data_get_u32(h+6, 4, 0, false);body_size=directory-base-10;
    if(limit-directory<=0 || limit-directory>4194304 || body_size>67108864)return false;
    packed=(uint8_t *)xx_mem_alloc((size_t)(limit-directory));
    dir=(uint8_t *)xx_mem_alloc(2097152);paths=(char *)xx_mem_alloc(65536);
    if(!packed || !dir || !paths || !pm_read(f,directory,packed,(size_t)(limit-directory)))goto done;
    if(h[4]>0x15)esp_directory_unscramble(packed,(size_t)(limit-directory));
    if(!esp_directory_decode(packed,(size_t)(limit-directory),dir,2097152,&dsize,pd) || !dsize || dsize%28)goto done;
    count=dsize/28;if(count>65535)goto done;
    /* Bound both the solid stream and the potentially larger prefix-spliced
     * output before allocating either. Directory rows consume no body bytes. */
    for(i=0;i<count;++i){
        const uint8_t *row=dir+i*28;uint32_t shared=xx_data_get_u32(row, 4, 0, false),bytes=xx_data_get_u32(row+24, 4, 0, false);
        if(carrier_stop(pd) || bytes>16777216 || shared>bytes || (row[19]&0xc0))goto done;
        if(row[19]&0x10)continue;
        stream_size+=bytes-shared;aggregate+=bytes;
        if(stream_size>67108864 || aggregate>67108864)goto done;
    }
    plain=(uint8_t *)xx_mem_alloc(stream_size?stream_size:1);
    prefix=(uint8_t *)xx_mem_alloc(16777216);
    if(!plain || !prefix)goto done;
    if(stream_size){
        if((h[5]&7)==4){if(stream_size>(uint64_t)body_size || !pm_read(f,base+10,plain,stream_size))goto done;}
        else{
            size_t written=0;body=(uint8_t *)xx_mem_alloc((size_t)body_size);
            if(!body || !pm_read(f,base+10,body,(size_t)body_size))goto done;
            if(h[4]>0x15)esp_directory_unscramble(body,(size_t)body_size);
            if(!xx_ain_decode_memory(body,(size_t)body_size,0,plain,stream_size,&written) || written!=stream_size)goto done;
        }
        if(h[5]&8){uint8_t accumulator=0;for(i=0;i<stream_size;++i){accumulator=(uint8_t)(accumulator+plain[i]);plain[i]=accumulator;}}
    }
    for(i=0;i<count;++i){
        const uint8_t *row=dir+i*28;uint32_t shared=xx_data_get_u32(row, 4, 0, false),bytes=xx_data_get_u32(row+24, 4, 0, false),next=i+1<count?xx_data_get_u32(dir+(i+1)*28, 4, 0, false):0;
        uint16_t parent=xx_data_get_u16(row+4, 2, 0, false);size_t name=0,parent_size=0;char label[96];uint8_t *data;size_t fresh=bytes-shared;
        if(carrier_stop(pd))goto done;
        while(name<13 && row[6+name]){if(row[6+name]<32 || row[6+name]>126)goto done;++name;}
        if(!name)goto done;
        if(parent!=65535){if(parent>=path_size)goto done;parent_size=xx_rt_strlen(paths+parent);}
        if(parent_size+name+(parent_size?1:0)>=sizeof(label))goto done;
        if(parent_size){xx_rt_memcpy(label,paths+parent,parent_size);label[parent_size++]='/';}
        xx_rt_memcpy(label+parent_size,row+6,name);label[parent_size+name]=0;
        if(row[19]&0x10){
            size_t length=parent_size+name+1;if(path_size+length>65536)goto done;
            xx_rt_memcpy(paths+path_size,label,length);path_size+=length;continue;
        }
        if(shared>prefix_size || fresh>stream_size-position || next>16777216)goto done;
        data=(uint8_t *)xx_mem_alloc(bytes?bytes:1);if(!data)goto done;
        if(shared)xx_rt_memcpy(data,prefix,shared);
        /* The retained prefix is a rolling buffer, not necessarily the start
         * of the previous file: unchanged bytes survive several members. */
        if(next>prefix_size){
            size_t fill=next-prefix_size;
            if(fill>fresh){xx_mem_free(data);goto done;}
            xx_rt_memcpy(prefix+prefix_size,plain+position,fill);
            xx_rt_memcpy(data+shared,plain+position,fill);
            position+=fill;fresh-=fill;shared+=(uint32_t)fill;
        }
        prefix_size=next;
        if(fresh)xx_rt_memcpy(data+shared,plain+position,fresh);
        position+=fresh;
        if(!pm_add(f,s,label,base,0)){xx_mem_free(data);goto done;}
        s->items[s->count-1].size=bytes;s->items[s->count-1].memory=data;
    }
    if(position!=stream_size) {goto done; } s->size=limit;ok=true;
done:xx_mem_free(packed);xx_mem_free(dir);xx_mem_free(body);xx_mem_free(plain);xx_mem_free(prefix);xx_mem_free(paths);return ok;
}
static bool sfx_carrier_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { uint8_t h[4],*scan; size_t n,i; int64_t low,limit=pm_available(f); unsigned tries=0;
    if(!pm_read(f,0,h,4)) { return false; } if(!xx_rt_memcmp(h,"ESP>",4)) return esp_directory_at(f,s,0,pd);
    if(!sfx_carrier_carrier(f,false,&low,pd) || h[0]!='M') { return false; } n=limit-low>16777216 ? 16777216U : (size_t)(limit-low); scan=(uint8_t *)xx_mem_alloc(n);
    if(!scan || !pm_read(f,low,scan,n)) { if(scan) xx_mem_free(scan); return false; }
    for(i=0;i+10<=n;++i) { if((i&4095U)==0 && carrier_stop(pd)) break; if(xx_rt_memcmp(scan+i,"ESP>",4)) continue; if(++tries>128) break;
        if(esp_directory_at(f,s,low+(int64_t)i,pd)) { xx_mem_free(scan); return true; } if(s->count) break;
    } xx_mem_free(scan); return false;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return sfx_carrier_parse(f,s,pd) && true; }
void xx_esp_archive_init(xx_esp_archive *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ESP_ARCHIVE,"esp"); } }
xx_esp_archive *xx_esp_archive_create(xx_io_device *d,int64_t b) { xx_esp_archive *r=(xx_esp_archive *)xx_mem_alloc(sizeof(*r)); if(r) xx_esp_archive_init(r,d,b); return r; }
void xx_esp_archive_destroy(xx_esp_archive *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_esp_archive_free(xx_esp_archive *r) { if(r) { xx_esp_archive_destroy(r); xx_mem_free(r); } }
bool xx_esp_archive_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_esp_archive_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
