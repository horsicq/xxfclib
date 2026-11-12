/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * .NET assemblies that embed files as manifest resources (ECMA-335 II.22.24):
 * every ManifestResource row whose Implementation is 0 lives in this file,
 * at the CLR header's Resources directory + Offset as a u32 length and the
 * bytes. Measured on the SQL Server 2017 installer stub (SSEI, a WPF
 * downloader): 27 rows, 15 of them assemblies (the installer engine and its
 * satellites), 11 licence texts and the WPF resource set. Members are the
 * embedded rows under their resource names; Costura's "*.compressed" rows
 * (a raw Deflate stream) are inflated under the name without the suffix. A
 * file counts only when at least one embedded row is not a .NET resource
 * set (CE CA EF BE), so plain WPF applications stay plain. The library's
 * .NET inspection provides the table layout; nothing is executed.
 */
#include "xxfclib/formats/executable/dotnet_resources_archive/xx_dotnet_resources_archive.h"
#include "../../common/xx_executable_carrier.h"
#include "xxfclib/formats/executable/dotnet/xx_dotnet_inspect.h"
#include "xxfclib/formats/executable/pe/xx_pe_inspect.h"
#include "xxfclib/algo/deflate/xx_deflate.h"

#define DR_MAX_MEMBERS 4096U
#define DR_MAX_NAME 512U
#define DR_COR_SIZE 32U

static const uint8_t dr_set_signature[4] = {0xCE, 0xCA, 0xEF, 0xBE};
static const char dr_compressed_suffix[] = ".compressed";

/* A Costura row: the raw Deflate stream inflated into the destination (or
 * into a temporary device for validation). */
static bool dr_inflate_all(Abstractformat *f, pm_member *m, xx_io_device *device, xx_pd_struct *pd)
{
    xx_io_device *out = device ? device : xx_io_temp_open();
    bool ok = out != NULL && m->packed_size > 0 && xx_deflate_unpack_device(f->device, m->offset, m->packed_size, out, false, pd);
    if (out && !device) xx_io_close(out);
    return ok && !(pd && xx_pd_is_stopped(pd));
}

/* A #Strings heap entry as a bounded, printable, NUL-terminated name. */
static bool dr_name(Abstractformat *f, int64_t strings, int64_t strings_size, uint32_t index, char *name)
{
    size_t n = strings_size - (int64_t)index > (int64_t)DR_MAX_NAME ? DR_MAX_NAME : (size_t)(strings_size - (int64_t)index), i;
    if ((int64_t)index >= strings_size || n < 2 || !pm_read(f, strings + (int64_t)index, name, n)) return false;
    for (i = 0; i < n && name[i]; ++i) {
        if ((unsigned char)name[i] < 0x20U || name[i] == 0x7F) name[i] = '_';
    }
    if (i == 0 || i >= n) return false; /* empty, or not terminated within the bound */
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    xx_dotnet_inspection st;
    uint8_t cor[DR_COR_SIZE], row[32], head[4];
    int64_t blob, table, image_end, cabinet, cabinet_end, available = pm_available(f);
    uint32_t rows, i, resources_rva, resources_size, element, string_size, implementation_size;
    bool foreign = false, ok = false;
    xx_mem_zero(&st, sizeof(st));
    if (carrier_stop(pd) || !carrier_pe(f, &image_end, &cabinet, &cabinet_end, pd)) return false;
    if (xx_dotnet_inspect_analyze_from_device(&st, f->device, f->base_address, pd) != 1 || !st.bIsNet || !st.cli.bValid) goto done;
    rows = st.cli.pRows[XX_DOTNET_MDT_ManifestResource];
    table = st.cli.pTableOffset[XX_DOTNET_MDT_ManifestResource];
    element = (uint32_t)st.cli.pElementSize[XX_DOTNET_MDT_ManifestResource];
    string_size = (uint32_t)st.cli.nStringIndexSize;
    implementation_size = (uint32_t)st.cli.nImplementationSize;
    if (!rows || rows > DR_MAX_MEMBERS || table <= 0 || element != 8U + string_size + implementation_size || element > sizeof(row) ||
        (string_size != 2U && string_size != 4U) || (implementation_size != 2U && implementation_size != 4U) || st.cli.nCliOffset <= 0 ||
        st.cli.nStringsOffset <= 0 || st.cli.nStringsSize <= 0 || !pm_read(f, st.cli.nCliOffset, cor, sizeof(cor)))
        goto done;
    resources_rva = xx_data_get_u32(cor + 24, 4, 0, false);
    resources_size = xx_data_get_u32(cor + 28, 4, 0, false);
    blob = resources_rva ? xx_pe_inspect_rva_to_offset(&st.pe, resources_rva) : -1;
    if (blob < 0 || resources_size < 4U || blob > available || (int64_t)resources_size > available - blob) goto done;
    for (i = 0; i < rows; ++i) {
        char name[DR_MAX_NAME], *suffix;
        uint32_t offset, size, name_index, implementation;
        int64_t at;
        size_t length;
        if (carrier_stop(pd) || !pm_read(f, table + (int64_t)i * element, row, element)) goto done;
        offset = xx_data_get_u32(row, 4, 0, false);
        name_index = string_size == 4U ? xx_data_get_u32(row + 8, 4, 0, false) : xx_data_get_u16(row + 8, 2, 0, false);
        implementation = implementation_size == 4U ? xx_data_get_u32(row + 8 + string_size, 4, 0, false) : xx_data_get_u16(row + 8 + string_size, 2, 0, false);
        if (implementation) continue; /* in another file or assembly */
        if (offset > resources_size - 4U || !pm_read(f, blob + (int64_t)offset, head, 4) || !dr_name(f, st.cli.nStringsOffset, st.cli.nStringsSize, name_index, name)) goto done;
        size = xx_data_get_u32(head, 4, 0, false);
        if (size > resources_size - 4U - offset) goto done;
        if (!size) continue;
        at = blob + (int64_t)offset + 4;
        if (size >= 4U && pm_read(f, at, head, 4) && xx_rt_memcmp(head, dr_set_signature, 4) != 0) foreign = true;
        length = xx_rt_strlen(name);
        suffix = length > sizeof(dr_compressed_suffix) - 1U ? name + length - (sizeof(dr_compressed_suffix) - 1U) : NULL;
        if (suffix) {
            size_t k;
            for (k = 0; k < sizeof(dr_compressed_suffix) - 1U; ++k) {
                char c = suffix[k] >= 'A' && suffix[k] <= 'Z' ? (char)(suffix[k] + 32) : suffix[k];
                if (c != dr_compressed_suffix[k]) {
                    suffix = NULL;
                    break;
                }
            }
        }
        if (suffix) *suffix = 0;
        if (s->count >= DR_MAX_MEMBERS || !executable_carrier_component(f, s, at, (int64_t)size, name)) goto done;
        if (suffix) {
            pm_member *m = &s->items[s->count - 1U];
            m->read_all = dr_inflate_all;
            m->size = -1; /* known only after inflating */
            m->compression_method = 8U;
        }
    }
    if (s->size < image_end) s->size = image_end;
    ok = foreign && !carrier_stop(pd) && carrier_members(s, pd);
done:
    xx_dotnet_inspect_free(&st);
    return ok;
}

void xx_dotnet_resources_archive_init(xx_dotnet_resources_archive *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_DOTNET_RESOURCES_ARCHIVE, "exe");
    }
}
xx_dotnet_resources_archive *xx_dotnet_resources_archive_create(xx_io_device *d, int64_t b)
{
    xx_dotnet_resources_archive *r = (xx_dotnet_resources_archive *)xx_mem_alloc(sizeof(*r));
    if (r) xx_dotnet_resources_archive_init(r, d, b);
    return r;
}
void xx_dotnet_resources_archive_destroy(xx_dotnet_resources_archive *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_dotnet_resources_archive_free(xx_dotnet_resources_archive *r)
{
    if (r) {
        xx_dotnet_resources_archive_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_dotnet_resources_archive_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_dotnet_resources_archive_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
