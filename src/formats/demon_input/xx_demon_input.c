/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.ase-lib.org/_modules/ase/calculators/demon/demon.html */
#include "xxfclib/formats/demon_input/xx_demon_input.h"
#include "../xx_sixteenth_root.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};bool ok=false;
    NH_NEED(nh_load(f,&b,pd));ok=f16_parse(f,s,&b);
    if(ok) s->size=(int64_t)b.n;
done:xx_mem_free(b.p);return ok;
}

void xx_demon_input_init(xx_demon_input *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_DEMON_INPUT,"demon_input"); } }
xx_demon_input *xx_demon_input_create(xx_io_device *d,int64_t b) { xx_demon_input *r=(xx_demon_input *)xx_mem_alloc(sizeof(*r)); if(r) xx_demon_input_init(r,d,b); return r; }
void xx_demon_input_destroy(xx_demon_input *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_demon_input_free(xx_demon_input *r) { if(r) { xx_demon_input_destroy(r); xx_mem_free(r); } }
bool xx_demon_input_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_demon_input_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
