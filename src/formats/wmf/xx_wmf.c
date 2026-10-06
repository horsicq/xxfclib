/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://download.microsoft.com/download/0/B/E/0BE8BDD7-E5E8-422A-ABFD-4342ED7AD886/WindowsMetafileFormat%28wmf%29Specification.pdf
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/wmf/xx_wmf.h"
#include "../xx_fifth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[40],b[16],objects[4096]; uint16_t sum=0,handles; uint32_t total,max_record,largest=0,count=0,points=0; uint64_t at=40,end; unsigned i,saved=0;
    if(!pm_read(f,0,h,40) || pm_le32(h)!=0x9ac6cdd7 || pm_le16(h+4) || (int16_t)pm_le16(h+6)>=(int16_t)pm_le16(h+10) || (int16_t)pm_le16(h+8)>=(int16_t)pm_le16(h+12) || !pm_le16(h+14) || pm_le32(h+16)) return false;
    for(i=0;i<10;++i) { sum^=pm_le16(h+i*2); } if(sum!=pm_le16(h+20)) return false;
    total=pm_le32(h+28); max_record=pm_le32(h+34); handles=pm_le16(h+32);
    if((pm_le16(h+22)!=1 && pm_le16(h+22)!=2) || pm_le16(h+24)!=9 || pm_le16(h+26)!=0x300 || total<12 || total>134217728 || handles>4096 || max_record<3 || pm_le16(h+38)) return false;
    end=22+(uint64_t)total*2; if(end>(uint64_t)pm_available(f) || !pm_add(f,s,"wmf-placeable.bin",0,22) || !pm_add(f,s,"wmf-header.bin",22,18)) return false; xx_mem_zero(objects,sizeof(objects));
    while(at<end) { uint32_t words,wanted=0; uint16_t kind,v=0; char label[48];
        if(fd_stop(pd) || ++count>4096 || !fd_range(at,6,end) || !pm_read(f,(int64_t)at,b,6)) { return false; } words=pm_le32(b); kind=pm_le16(b+4);
        if(words<3 || words>max_record || !fd_range(at,(uint64_t)words*2,end) || !pm_read(f,(int64_t)at,b,words<8 ? words*2:16)) { return false; } if(words>largest) largest=words;
        if(kind==0) { if(words!=3 || at+6!=end || largest!=max_record) return false; }
        else if(kind==0x324 || kind==0x325) { uint32_t n=pm_le16(b+6); if(n<2 || (kind==0x324 && n<3) || n>65536-points || words!=4+(uint64_t)n*2) return false; points+=n; }
        else { if(kind==0x214 || kind==0x213 || (kind>=0x20b && kind<=0x20e) || kind==0x201 || kind==0x209) wanted=5;
            else if(kind==0x41b || kind==0x418) wanted=7;
            else if(kind==0x12d || kind==0x1f0 || kind==0x127 || kind==0x102 || kind==0x103 || kind==0x104 || kind==0x106 || kind==0x107 || kind==0x108) wanted=4;
            else if(kind==0x1e) wanted=3; else if(kind==0x2fa) wanted=8; else if(kind==0x2fc) wanted=7; else return false;
            if(words!=wanted) return false;
            if(kind==0x1e) { if(++saved>4096) return false; }
            if(kind==0x127) { int16_t level=(int16_t)pm_le16(b+6); if(level>=0 || (unsigned)(-level)>saved) return false; saved-=(unsigned)(-level); }
            if(kind==0x2fa || kind==0x2fc) { for(i=0;i<handles && objects[i];++i) { } if(i==handles) return false; objects[i]=1;
                if(kind==0x2fa && (pm_le16(b+6)>8 || (int16_t)pm_le16(b+8)<0 || pm_le16(b+10))) return false;
                if(kind==0x2fc && (pm_le16(b+6)>2 || (pm_le16(b+6)!=2 && pm_le16(b+12)))) return false;
            }
            if(kind==0x12d || kind==0x1f0) { v=pm_le16(b+6); if(kind==0x12d && (v&0x8000)) { if((v&0x7fff)>19) return false; }
                else { if(v>=handles || !objects[v]) return false; if(kind==0x1f0) objects[v]=0; }
            }
        }
        xx_rt_snprintf(label,sizeof(label),"wmf-record-%u-type-%04x.bin",count-1,kind); if(!pm_add(f,s,label,(int64_t)at,(int64_t)words*2)) return false; at+=(uint64_t)words*2;
        if(!kind) { s->size=(int64_t)end; return true; }
    } return false;
}

void xx_wmf_init(xx_wmf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_WMF,"wmf"); } }
xx_wmf *xx_wmf_create(xx_io_device *d,int64_t b) { xx_wmf *r=(xx_wmf *)xx_mem_alloc(sizeof(*r)); if(r) xx_wmf_init(r,d,b); return r; }
void xx_wmf_destroy(xx_wmf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_wmf_free(xx_wmf *r) { if(r) { xx_wmf_destroy(r); xx_mem_free(r); } }
bool xx_wmf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_wmf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
