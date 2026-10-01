/* SPDX-License-Identifier: MIT
 * Independently implemented from https://archive.gfjc.fiu.edu/workshops/resources/literature/ABIF_File_Format.pdf */
#include "xxfclib/formats/sequencing_abif/xx_sequencing_abif.h"
#include "../xx_fifteenth_root.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};bool ok=false;
    NH_NEED(nh_load(f,&b,pd));ok=f15_parse(f,s,&b);
    if(ok) s->size=(int64_t)b.n;
done:xx_mem_free(b.p);return ok;
}

void xx_sequencing_abif_init(xx_sequencing_abif *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SEQUENCING_ABIF,"sequencing_abif"); } }
xx_sequencing_abif *xx_sequencing_abif_create(xx_io_device *d,int64_t b) { xx_sequencing_abif *r=(xx_sequencing_abif *)xx_mem_alloc(sizeof(*r)); if(r) xx_sequencing_abif_init(r,d,b); return r; }
void xx_sequencing_abif_destroy(xx_sequencing_abif *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sequencing_abif_free(xx_sequencing_abif *r) { if(r) { xx_sequencing_abif_destroy(r); xx_mem_free(r); } }
bool xx_sequencing_abif_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sequencing_abif_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
