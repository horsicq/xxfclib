/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/installers/xinstallanywhere.cpp
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/installanywhere_unix/xx_installanywhere_unix.h"
#include "../common/xx_carrier_helpers.h"

static bool carrier_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    size_t len, vars;
    char *text = carrier_shell(f, &len), *second;
    uint64_t block = 32768, start, jre = 0, arch, res = 0, blocks, preamble, previous;
    int64_t limit = pm_available(f);
    bool ok = false;
    if (!text) {
        return false;
    }
    if ((!xx_rt_strstr(text, "InstallAnywhere (tm) UNIX Self Extractor") && !xx_rt_strstr(text, "InstallAnywhere UNIX Self Extractor") &&
         !xx_rt_strstr(text, "InstallAnywhere is preparing to install")) ||
        !(second = xx_rt_strstr(text + 10, "#!/bin/sh")) || second - text > 4096)
        goto done;
    vars = (size_t)(second - text);
    {
        const char *v;
        size_t n;
        if (carrier_assignment(text, vars, "BLOCKSIZE", &v, &n) && !carrier_decimal(v, n, &block)) goto done;
    }
    if (block < 512 || block > 1048576 || (block & (block - 1)) || !carrier_number(text, vars, "ARCHREALSIZE", &arch) || arch < 22) goto done;
    if (carrier_number(text, vars, "JRESTART", &start)) {
        uint8_t h[3];
        if (!carrier_number(text, vars, "JREREALSIZE", &jre) || jre < 10 || start > (uint64_t)limit / block || !carrier_range(limit, start * block, jre) ||
            !pm_read(f, (int64_t)(start * block), h, 3) || h[0] != 31 || (h[1] != 139 && h[1] != 157))
            goto done;
        preamble = start * block;
        if (!pm_add(f, s, h[1] == 139 ? "vm.tar.gz" : "vm.tar.Z", (int64_t)preamble, (int64_t)jre)) goto done;
        start += (jre + block - 1) / block;
    } else {
        if (!carrier_number(text, vars, "ARCHSTART", &start) || start > (uint64_t)limit / block) goto done;
        preamble = start * block;
    }
    if (preamble < vars || start > (uint64_t)limit / block || !carrier_range(limit, start * block, arch) ||
        !carrier_zip(f, (int64_t)(start * block), (int64_t)(start * block + arch), pd))
        goto done;
    if (carrier_number(text, vars, "ARCHSIZE", &blocks) && blocks != (arch + block - 1) / block) goto done;
    if (!pm_add(f, s, "installer.zip", (int64_t)(start * block), (int64_t)arch)) {
        goto done;
    }
    previous = start * block + arch;
    start += (arch + block - 1) / block;
    if (carrier_number(text, vars, "RESSIZE", &blocks) && blocks) {
        if (!carrier_number(text, vars, "RESREALSIZE", &res) || res < 22 || blocks != (res + block - 1) / block || start > (uint64_t)limit / block ||
            !carrier_range(limit, start * block, res) || !carrier_zip(f, (int64_t)(start * block), (int64_t)(start * block + res), pd) ||
            !pm_add(f, s, "Resource1.zip", (int64_t)(start * block), (int64_t)res)) {
            goto done;
        }
        previous = start * block + res;
    }
    s->size = (int64_t)previous;
    ok = true;
done:
    xx_mem_free(text);
    return ok;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return carrier_parse(f, s, pd) && carrier_members(s, pd);
}
void xx_installanywhere_unix_init(xx_installanywhere_unix *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_INSTALLANYWHERE_UNIX, "bin");
    }
}
xx_installanywhere_unix *xx_installanywhere_unix_create(xx_io_device *d, int64_t b)
{
    xx_installanywhere_unix *r = (xx_installanywhere_unix *)xx_mem_alloc(sizeof(*r));
    if (r) xx_installanywhere_unix_init(r, d, b);
    return r;
}
void xx_installanywhere_unix_destroy(xx_installanywhere_unix *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_installanywhere_unix_free(xx_installanywhere_unix *r)
{
    if (r) {
        xx_installanywhere_unix_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_installanywhere_unix_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_installanywhere_unix_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
