/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/AcademySoftwareFoundation/OpenColorIO/blob/main/src/OpenColorIO/fileformats/FileFormatSpi3D.cpp */
#include "xxfclib/formats/lut_spi3d/xx_lut_spi3d.h"
#include "../xx_fifteenth_root.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};bool ok=false;
    NH_NEED(nh_load(f,&b,pd));ok=f15_parse(f,s,&b);
    if(ok) s->size=(int64_t)b.n;
done:xx_mem_free(b.p);return ok;
}

void xx_lut_spi3d_init(xx_lut_spi3d *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_LUT_SPI3D,"lut_spi3d"); } }
xx_lut_spi3d *xx_lut_spi3d_create(xx_io_device *d,int64_t b) { xx_lut_spi3d *r=(xx_lut_spi3d *)xx_mem_alloc(sizeof(*r)); if(r) xx_lut_spi3d_init(r,d,b); return r; }
void xx_lut_spi3d_destroy(xx_lut_spi3d *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_lut_spi3d_free(xx_lut_spi3d *r) { if(r) { xx_lut_spi3d_destroy(r); xx_mem_free(r); } }
bool xx_lut_spi3d_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_lut_spi3d_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
