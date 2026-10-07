/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/OSGeo/shapelib/master/dbfopen.c */
#include "xxfclib/formats/dbase_dbf/xx_dbase_dbf.h"
#include "../xx_fifth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[32],d[32],b; uint32_t records,i,fields,total=1; uint16_t header,row; uint64_t bytes,end; char names[1024][12];
    if(!pm_read(f,0,h,32) || h[0]!=3 || h[2]<1 || h[2]>12 || h[3]<1 || h[3]>31 || h[14] || h[15] || (records=xx_data_get_u32(h+4, 4, 0, false))>65534 || !(row=xx_data_get_u16(h+10, 2, 0, false)) || (header=xx_data_get_u16(h+8, 2, 0, false))<65 || (header-33)%32) return false;
    fields=(header-33)/32; if(fields>1024) return false;
    for(i=0;i<fields;++i) { unsigned j,k; if(fd_stop(pd) || !pm_read(f,32+i*32,d,32) || !d[0] || !d[16] || d[17]>=d[16]) return false;
        for(j=0;j<11 && d[j];++j) { if(d[j]<32 || d[j]>126) return false; } if(j==11) return false;
        xx_mem_zero(names[i],12); xx_rt_memcpy(names[i],d,j);
        for(k=0;k<i;++k) if(!xx_rt_strcmp(names[k],names[i])) return false;
        if(d[11]!='C' && d[11]!='N' && d[11]!='F' && d[11]!='D' && d[11]!='L') return false;
        if((d[11]=='D' && d[16]!=8) || (d[11]=='L' && d[16]!=1) || ((d[11]=='C' || d[11]=='D' || d[11]=='L') && d[17])) return false;
        total+=d[16];
    }
    if(total!=row || !pm_read(f,header-1,&b,1) || b!=13 || !fd_mul(records,row,&bytes) || !fd_range(header,bytes,(uint64_t)pm_available(f)) || !pm_add(f,s,"field-dictionary.bin",0,header)) return false;
    end=header+bytes;
    for(i=0;i<records;++i) { char name[48]; if(fd_stop(pd) || !pm_read(f,header+(int64_t)i*row,&b,1) || (b!=' ' && b!='*')) return false;
        xx_rt_snprintf(name,sizeof(name),"record-%u.bin",i); if(!pm_add(f,s,name,header+(int64_t)i*row,row)) return false;
    }
    if(end<(uint64_t)pm_available(f) && pm_read(f,(int64_t)end,&b,1) && b==26) ++end;
    s->size=(int64_t)end; return true;
}

void xx_dbase_dbf_init(xx_dbase_dbf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_DBASE_DBF,"dbase_dbf"); } }
xx_dbase_dbf *xx_dbase_dbf_create(xx_io_device *d,int64_t b) { xx_dbase_dbf *r=(xx_dbase_dbf *)xx_mem_alloc(sizeof(*r)); if(r) xx_dbase_dbf_init(r,d,b); return r; }
void xx_dbase_dbf_destroy(xx_dbase_dbf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_dbase_dbf_free(xx_dbase_dbf *r) { if(r) { xx_dbase_dbf_destroy(r); xx_mem_free(r); } }
bool xx_dbase_dbf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_dbase_dbf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
