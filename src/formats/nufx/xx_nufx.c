/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://nulib.com/library/FTN.e08002.htm
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/nufx/xx_nufx.h"
#include "../sfx_imp/xx_seventh_wrapper_table.h"
static bool w7_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {
    uint8_t*b;size_t n,p=48;uint32_t count,i,total=0;uint64_t checksum=40;bool ok=false;
    b=w7_load(f,&n,pd);if(!b)return false;if(n<48 || xx_rt_memcmp(b,"N\xf5" "F\xe9" "l\xe5",6) || pm_le16(b+28)!=2 || w7_crc16(b+8,40,0,pd)!=pm_le16(b+6) || (count=pm_le32(b+8))==0 || count>W7_COUNT || pm_le32(b+38)!=n)goto done;for(i=30;i<38;++i)if(b[i])goto done;for(i=42;i<48;++i)if(b[i])goto done;
    for(i=0;i<count;++i) {uint16_t attrs,version,filename;uint32_t threads,j;size_t table,data,end;bool named=false;
        if(wg_stop(pd) || !w7_range(n,p,58) || xx_rt_memcmp(b+p,"N\xf5" "F\xd8",4))goto done;attrs=pm_le16(b+p+6);version=pm_le16(b+p+8);threads=pm_le32(b+p+10);if(version>3 || version==2 || attrs<(version ? 60:58) || attrs>4096 || !w7_range(n,p,attrs) || !threads || threads>W7_COUNT-total)goto done;if(version && (pm_le16(b+p+56)>attrs-60 || 58U+((pm_le16(b+p+56)+1U)&~1U)>(unsigned)attrs-2U))goto done;filename=pm_le16(b+p+attrs-2);if(filename>1024 || !w7_range(n,p+attrs,filename))goto done;named=filename!=0;table=p+attrs+filename;if(!w7_range(n,table,(uint64_t)threads*16))goto done;data=table+(size_t)threads*16;end=data;checksum+=data-p-6;if(checksum>67108864 || w7_crc16(b+p+6,data-p-6,0,pd)!=pm_le16(b+p+4))goto done;
        for(j=0;j<threads;++j) {const uint8_t*t=b+table+(size_t)j*16;uint16_t cls=pm_le16(t),format=pm_le16(t+2),kind=pm_le16(t+4);uint32_t raw=pm_le32(t+8),packed=pm_le32(t+12);char label[64];if(wg_stop(pd) || format || cls>3 || !w7_range(n,end,packed))goto done;
            if(cls==2) {if(kind>2 || raw!=packed || checksum+packed>67108864)goto done;checksum+=packed;if(version==3 && w7_crc16(b+end,packed,65535,pd)!=pm_le16(t+6))goto done;xx_rt_snprintf(label,sizeof(label),"record-%u-%s.bin",i,kind==0 ? "data":(kind==1 ? "disk":"resource"));if(!pm_add(f,s,label,(int64_t)end,raw))goto done;}
            else if(cls==3) {if(kind || !raw || raw>packed || packed>4096)goto done;named=true;}
            else if(cls==1) {if(kind || raw || packed)goto done;}
            else if(kind>2 || raw>packed || packed>65536)goto done;
            end+=packed;
        }if(!named)goto done;total+=threads;p=end;
    }ok=p==n && s->count!=0;if(ok)s->size=(int64_t)n;
done:xx_mem_free(b);return ok && !wg_stop(pd);
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w7_parse(f,s,pd) && wg_members(s,pd); }
void xx_nufx_init(xx_nufx *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NUFX,"shk"); } }
xx_nufx *xx_nufx_create(xx_io_device *d,int64_t b) { xx_nufx *r=(xx_nufx *)xx_mem_alloc(sizeof(*r)); if(r) xx_nufx_init(r,d,b); return r; }
void xx_nufx_destroy(xx_nufx *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nufx_free(xx_nufx *r) { if(r) { xx_nufx_destroy(r); xx_mem_free(r); } }
bool xx_nufx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nufx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
