/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.msg.chem.iastate.edu/gamess/GAMESS_Manual/input.pdf */
#include "xxfclib/formats/gamess_input/xx_gamess_input.h"
#include "../xx_sixteenth_root.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};bool ok=false;
    NH_NEED(nh_load(f,&b,pd));ok=f16_parse(f,s,&b);
    if(ok) s->size=(int64_t)b.n;
done:xx_mem_free(b.p);return ok;
}

void xx_gamess_input_init(xx_gamess_input *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GAMESS_INPUT,"gamess_input"); } }
xx_gamess_input *xx_gamess_input_create(xx_io_device *d,int64_t b) { xx_gamess_input *r=(xx_gamess_input *)xx_mem_alloc(sizeof(*r)); if(r) xx_gamess_input_init(r,d,b); return r; }
void xx_gamess_input_destroy(xx_gamess_input *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_gamess_input_free(xx_gamess_input *r) { if(r) { xx_gamess_input_destroy(r); xx_mem_free(r); } }
bool xx_gamess_input_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_gamess_input_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
