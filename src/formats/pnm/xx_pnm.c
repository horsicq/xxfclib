/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://netpbm.sourceforge.net/doc/pbm.html, https://netpbm.sourceforge.net/doc/pgm.html, https://netpbm.sourceforge.net/doc/ppm.html, https://netpbm.sourceforge.net/doc/pam.html
 * Stored encoded component extraction; no media decoding claims.
 */
#include "xxfclib/formats/pnm/xx_pnm.h"
#include "../xx_payload_members.h"

typedef struct pn_bytes { Abstractformat *f; xx_pd_struct *pd; int64_t at,end,start; size_t count,capacity; uint8_t *data; } pn_bytes;
static bool pn_byte(pn_bytes *r,uint8_t *b,bool consume) {
    if(r->at>=r->end) return false;
    if(!r->data) { if((uint64_t)(r->end-r->at)<r->capacity) r->capacity=(size_t)(r->end-r->at); r->data=(uint8_t *)xx_mem_alloc(r->capacity); if(!r->data) return false; }
    if(r->at<r->start || r->at-r->start>=(int64_t)r->count) { r->count=(uint64_t)(r->end-r->at)>r->capacity ? r->capacity : (size_t)(r->end-r->at);
        if((r->pd && xx_pd_is_stopped(r->pd)) || !pm_read(r->f,r->at,r->data,r->count)) return false; r->start=r->at; }
    *b=r->data[(size_t)(r->at-r->start)]; if(consume) ++r->at; return true;
}
static bool pn_space(uint8_t b) { return b==9 || b==10 || b==11 || b==12 || b==13 || b==32; }
static bool pn_skip(pn_bytes *r) { uint8_t b;
    while(pn_byte(r,&b,false)) { if(pn_space(b)) { ++r->at; continue; }
        if(b=='#') { do { ++r->at; } while(pn_byte(r,&b,false) && b!=10 && b!=13); continue; } return true; }
    return false;
}
static bool pn_num(pn_bytes *r,uint32_t *n) { uint8_t b; uint32_t value=0; unsigned digits=0;
    if(!pn_skip(r)) return false;
    while(pn_byte(r,&b,false) && b>='0' && b<='9') { if(++digits>10 || value>(UINT32_MAX-(b-'0'))/10U) return false; value=value*10U+b-'0'; ++r->at; }
    if(!digits || !pn_byte(r,&b,false) || (!pn_space(b) && b!='#')) return false; *n=value; return true;
}
static bool pn_delimiter(pn_bytes *r) { uint8_t b,next; if(!pn_byte(r,&b,true) || !pn_space(b)) return false;
    if(b==13 && pn_byte(r,&next,false) && next==10) ++r->at; return true;
}
static bool pn_line(pn_bytes *r,char *line,size_t capacity) { uint8_t b; size_t n=0;
    while(pn_byte(r,&b,true)) { if(b==10) { line[n]=0; return true; } if(b==13) continue; if(b<32 && b!=9) return false; if(n+1>=capacity) return false; line[n++]=(char)b; } return false;
}
static bool pn_decimal(const char *p,uint32_t *v) { uint32_t n=0; unsigned digits=0; while(*p==' ' || *p=='\t') ++p;
    while(*p>='0' && *p<='9') { if(++digits>10 || n>(UINT32_MAX-(unsigned)(*p-'0'))/10U) return false; n=n*10U+(unsigned)(*p++-'0'); }
    while(*p==' ' || *p=='\t') ++p; if(!digits || (*p && *p!='#')) return false; *v=n; return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    pn_bytes r;bool buffer_result=false; unsigned images=0; xx_mem_zero(&r,sizeof(r)); r.f=f; r.pd=pd; r.end=pm_available(f); r.start=-1;r.capacity=xx_get_file_buffer_size();
    while(r.at<r.end) { int64_t start=r.at,data,end; uint8_t magic,kind,b; uint32_t width=0,height=0,depth=0,max=1; uint64_t samples,bytes,i; char label[48];
        if(++images>1024 || !pn_byte(&r,&magic,true) || magic!='P' || !pn_byte(&r,&kind,true) || kind<'1' || kind>'7' || !pn_byte(&r,&b,false) || !pn_space(b)) { buffer_result = (false); goto buffer_done; }
        if(kind=='7') { char line[512]; unsigned fields=0,lines=0;
            if(!pn_delimiter(&r)) { buffer_result = (false); goto buffer_done; }
            for(;;) { char *key,*value; uint32_t n; if(++lines>1024 || !pn_line(&r,line,sizeof(line))) { buffer_result = (false); goto buffer_done; } key=line;
                while(*key==' ' || *key=='\t') ++key; if(!*key || *key=='#') continue;
                value=key; while(*value && *value!=' ' && *value!='\t') ++value; if(*value) *value++=0;
                if(!xx_rt_strcmp(key,"ENDHDR")) { while(*value==' ' || *value=='\t') ++value; if(*value && *value!='#') { buffer_result = (false); goto buffer_done; } break; }
                if(!xx_rt_strcmp(key,"TUPLTYPE")) { if(!*value) { buffer_result = (false); goto buffer_done; } continue; }
                if(!pn_decimal(value,&n)) { buffer_result = (false); goto buffer_done; }
                if(!xx_rt_strcmp(key,"WIDTH")) { if(fields&1) { buffer_result = (false); goto buffer_done; } width=n; fields|=1; }
                else if(!xx_rt_strcmp(key,"HEIGHT")) { if(fields&2) { buffer_result = (false); goto buffer_done; } height=n; fields|=2; }
                else if(!xx_rt_strcmp(key,"DEPTH")) { if(fields&4) { buffer_result = (false); goto buffer_done; } depth=n; fields|=4; }
                else if(!xx_rt_strcmp(key,"MAXVAL")) { if(fields&8) { buffer_result = (false); goto buffer_done; } max=n; fields|=8; }
                else { buffer_result = (false); goto buffer_done; }
            } if(fields!=15 || !depth || depth>16) { buffer_result = (false); goto buffer_done; }
        } else { if(!pn_num(&r,&width) || !pn_num(&r,&height) || ((kind!='1' && kind!='4') && !pn_num(&r,&max)) || !pn_delimiter(&r)) { buffer_result = (false); goto buffer_done; } depth=(kind=='3' || kind=='6') ? 3U : 1U; }
        if(!width || !height || !max || max>65535 || (uint64_t)width*height>16777216U/depth) { buffer_result = (false); goto buffer_done; }
        samples=(uint64_t)width*height*depth; data=r.at;
        if(kind<'4') { for(i=0;i<samples;++i) { uint32_t value;
                if(kind=='1') { if(!pn_skip(&r) || !pn_byte(&r,&b,true) || (b!='0' && b!='1')) { buffer_result = (false); goto buffer_done; } }
                else if(!pn_num(&r,&value) || value>max) { buffer_result = (false); goto buffer_done; }
            } if(!pn_delimiter(&r)) { buffer_result = (false); goto buffer_done; } end=r.at;
        } else { bytes=kind=='4' ? ((uint64_t)width+7)/8*height : samples*(max>255 ? 2U : 1U);
            if(bytes>(uint64_t)(r.end-r.at)) { buffer_result = (false); goto buffer_done; }
            if(kind=='4') r.at+=(int64_t)bytes;
            else for(i=0;i<samples;++i) { uint32_t value; if(!pn_byte(&r,&b,true)) { buffer_result = (false); goto buffer_done; } value=b;
                if(max>255) { uint8_t low; if(!pn_byte(&r,&low,true)) { buffer_result = (false); goto buffer_done; } value=value*256U+low; } if(value>max) { buffer_result = (false); goto buffer_done; } }
            end=r.at;
        }
        xx_rt_snprintf(label,sizeof(label),"image-%u-header.txt",images-1); if(!pm_add(f,s,label,start,data-start)) { buffer_result = (false); goto buffer_done; }
        xx_rt_snprintf(label,sizeof(label),"image-%u-raster.%s",images-1,kind<'4' ? "txt" : "bin"); if(!pm_add(f,s,label,data,end-data)) { buffer_result = (false); goto buffer_done; }
        if(r.at==r.end) break;
        if(!pn_skip(&r)) { r.at=end; break; }
        if(!pn_byte(&r,&b,false) || b!='P') { r.at=end; break; }
    } s->size=r.at; { buffer_result = (images>0); goto buffer_done; }

buffer_done:
    xx_mem_free(r.data);
    return buffer_result;
}

void xx_pnm_init(xx_pnm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_PNM,"pnm"); } }
xx_pnm *xx_pnm_create(xx_io_device *d,int64_t b) { xx_pnm *r=(xx_pnm *)xx_mem_alloc(sizeof(*r)); if(r) xx_pnm_init(r,d,b); return r; }
void xx_pnm_destroy(xx_pnm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_pnm_free(xx_pnm *r) { if(r) { xx_pnm_destroy(r); xx_mem_free(r); } }
bool xx_pnm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_pnm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
