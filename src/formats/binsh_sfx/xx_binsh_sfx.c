/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Generic facade over the shared nonexecuting tail-carve parser.
 */
#include "xxfclib/formats/binsh_sfx/xx_binsh_sfx.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
void xx_binsh_sfx_init(xx_binsh_sfx *r, xx_io_device *d, int64_t b) {
  if (r) {
    xx_sun_java_binsh_init(r, d, b);
    r->format.file_type = XX_FILE_TYPE_BINSH_SFX;
    r->format.check_is_valid = xx_binsh_sfx_check_is_valid;
    r->format.handle_base_info = xx_binsh_sfx_handle_base_info;
  }
}
xx_binsh_sfx *xx_binsh_sfx_create(xx_io_device *d, int64_t b) {
  xx_binsh_sfx *r = (xx_binsh_sfx *)xx_mem_alloc(sizeof(*r));
  if (r)
    xx_binsh_sfx_init(r, d, b);
  return r;
}
void xx_binsh_sfx_destroy(xx_binsh_sfx *r) { xx_sun_java_binsh_destroy(r); }
void xx_binsh_sfx_free(xx_binsh_sfx *r) { xx_sun_java_binsh_free(r); }
bool xx_binsh_sfx_check_is_valid(Abstractformat *f, xx_pd_struct *pd) {
  int64_t at;
  bool ok;
  if (!f || !f->device || (at = xx_io_tell(f->device)) < 0)
    return false;
  ok = xx_sun_java_binsh_check_is_valid(f, pd);
  return xx_io_seek64(f->device, at, SEEK_SET) == 0 && ok;
}
bool xx_binsh_sfx_handle_base_info(Abstractformat *f, xx_pd_struct *pd) {
  int64_t at;
  bool ok;
  if (!f || !f->device || (at = xx_io_tell(f->device)) < 0)
    return false;
  ok = xx_sun_java_binsh_handle_base_info(f, pd);
  if (xx_io_seek64(f->device, at, SEEK_SET) != 0) {
    f->is_valid = false;
    return false;
  }
  return ok;
}
xx_file_type_t xx_binsh_sfx_detect(xx_io_device *d, int64_t b) {
  uint8_t h[3];
  xx_binsh_sfx r;
  bool ok;
  if (!xx_io_read_at(d, b, h, 3) || h[0] != '#' || h[1] != '!' ||
      (h[2] != '/' && h[2] != ' '))
    return XX_FILE_TYPE_UNKNOWN;
  /* The global signature-reader pass precedes product shell routing. Preserve
   * Sun identity, and let the dedicated Makeself/InstallAnywhere gates run. */
  xx_sun_java_binsh_init(&r, d, b);
  ok = xx_sun_java_binsh_check_is_valid(&r.format, NULL);
  xx_sun_java_binsh_destroy(&r);
  if (ok)
    return XX_FILE_TYPE_SUN_JAVA_BINSH;
  {
    uint8_t text[4096];
    int64_t total = xx_io_total_size(d);
    size_t n, i;
    if (b < 0 || total < b)
      return XX_FILE_TYPE_UNKNOWN;
    n = total - b > 4096 ? 4096U : (size_t)(total - b);
    if (!xx_io_read_at(d, b, text, n))
      return XX_FILE_TYPE_UNKNOWN;
    for (i = 0; i < n; ++i) {
      if ((n - i >= 8 && !xx_rt_memcmp(text + i, "Makeself", 8)) ||
          (n - i >= 15 && !xx_rt_memcmp(text + i, "InstallAnywhere", 15)))
        return XX_FILE_TYPE_UNKNOWN;
    }
  }
  xx_binsh_sfx_init(&r, d, b);
  ok = xx_binsh_sfx_check_is_valid(&r.format, NULL);
  xx_binsh_sfx_destroy(&r);
  return ok ? XX_FILE_TYPE_BINSH_SFX : XX_FILE_TYPE_UNKNOWN;
}
