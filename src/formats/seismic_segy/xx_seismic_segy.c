/* SPDX-License-Identifier: MIT
 * Independently implemented from https://seg.org/wp-content/uploads/2025/11/seg_y_rev1.pdf */
#include "xxfclib/formats/seismic_segy/xx_seismic_segy.h"
#include "../common/xx_numeric_values.h"

static unsigned sample_width(unsigned x) {switch(x) {case 1:case 2:case 5:return 4;case 3:return 2;case 8:return 1;default:return 0;}}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[400],trace[240],text[3200];uint64_t at,n;unsigned rev,ext,w,index=0,ns,dt,fixed,i;int64_t available=pm_available(f);
    if(binary_stop(pd) || available<3841 || available>67108864 || !pm_read(f,3200,h,400) || !pm_read(f,0,text,3200)) return false;
    /* The textual header has no unique signature. Accept printable ASCII or
       EBCDIC blocks, requiring forty nonempty 80-byte card images. */
    for(i=0;i<40;++i) {unsigned j;bool content=false;for(j=0;j<80;++j) {uint8_t c=text[i*80+j];if(!c || c==255) return false;if(c!=32 && c!=64) content=true;}if(!content) return false;}
    rev=xx_data_get_u16(h+300, 2, 0, true);ext=xx_data_get_u16(h+304, 2, 0, true);fixed=xx_data_get_u16(h+302, 2, 0, true);ns=xx_data_get_u16(h+20, 2, 0, true);dt=xx_data_get_u16(h+16, 2, 0, true);w=sample_width(xx_data_get_u16(h+24, 2, 0, true));
    if((rev!=0 && rev!=256) || ext>16 || fixed>1 || !w || !ns || !dt || (!rev && (ext || fixed))) return false;
    at=3600+(uint64_t)ext*3200;if(at>(uint64_t)available || !pm_add(f,s,"segy-file-headers.bin",0,(int64_t)at)) return false;
    while(at<(uint64_t)available) {unsigned samples,interval;char label[64];
        if(binary_stop(pd) || ++index>1024 || !binary_range(at,240,(uint64_t)available) || !pm_read(f,(int64_t)at,trace,240)) return false;
        samples=xx_data_get_u16(trace+114, 2, 0, true);interval=xx_data_get_u16(trace+116, 2, 0, true);if(!samples || !interval || (fixed && (samples!=ns || interval!=dt)) || (int16_t)xx_data_get_u16(trace+28, 2, 0, true)<-1) return false;
        n=(uint64_t)samples*w;if(!binary_range(at+240,n,(uint64_t)available)) return false;
        xx_rt_snprintf(label,sizeof(label),"trace-%u-header.bin",index-1);if(!pm_add(f,s,label,(int64_t)at,240)) return false;
        xx_rt_snprintf(label,sizeof(label),"trace-%u-samples.bin",index-1);if(!pm_add(f,s,label,(int64_t)(at+240),(int64_t)n)) return false;at+=240+n;
    }if(!index || at!=(uint64_t)available) return false;s->size=available;return true;
}

void xx_seismic_segy_init(xx_seismic_segy *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SEISMIC_SEGY,"seismic_segy"); } }
xx_seismic_segy *xx_seismic_segy_create(xx_io_device *d,int64_t b) { xx_seismic_segy *r=(xx_seismic_segy *)xx_mem_alloc(sizeof(*r)); if(r) xx_seismic_segy_init(r,d,b); return r; }
void xx_seismic_segy_destroy(xx_seismic_segy *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_seismic_segy_free(xx_seismic_segy *r) { if(r) { xx_seismic_segy_destroy(r); xx_mem_free(r); } }
bool xx_seismic_segy_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_seismic_segy_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
