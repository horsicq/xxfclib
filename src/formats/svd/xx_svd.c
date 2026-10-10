/* SPDX-License-Identifier: MIT. Original Semi-Virtual Diskette component parser.
 * Format facts: author's thesvd.com Downloads/app23.tar.gz, svd12/15/20.c.
 * Blocks always store 256 bytes; WD sectors use documented cyclic rotation.
 * Other encodings retain complete stored blocks and their track descriptors.
 */
#include "xxfclib/formats/svd/xx_svd.h"
#include "../hxc_afi/xx_hxc_tracks.h"
static bool svd_line(hx_blob *b, uint64_t *at, char *line, size_t cap)
{
    size_t n = 0;
    while (*at < b->n && b->p[*at] != 10) {
        uint8_t c = b->p[(*at)++];
        if (c != 13) {
            if (c < 32 || c > 126 || n + 1 >= cap) return false;
            line[n++] = (char)c;
        }
    }
    if (*at >= b->n) return false;
    ++*at;
    line[n] = 0;
    return true;
}
static bool svd_number(hx_blob *b, uint64_t *at, uint32_t *out)
{
    char line[80];
    size_t i = 0;
    uint32_t n = 0;
    bool digit = false;
    if (!svd_line(b, at, line, sizeof(line))) return false;
    while (line[i] == ' ') ++i;
    while (line[i] >= '0' && line[i] <= '9') {
        digit = true;
        if (n > 100000U) return false;
        n = n * 10U + (unsigned)(line[i++] - '0');
    }
    while (line[i] == ' ') ++i;
    if (!digit || line[i]) return false;
    *out = n;
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    hx_blob b;
    uint64_t at = 0, header;
    uint32_t sectors, tracks, sides = 1, sizecode = 1, wprot = 0, i;
    char version[80], name[96], info[384];
    bool old, ok = false;
    if (!hx_load(f, &b, pd)) return false;
    HX_NEED(svd_line(&b, &at, version, sizeof(version)));
    old = !xx_rt_strcmp(version, "1.2");
    HX_NEED(old || !xx_rt_strcmp(version, "1.5") || !xx_rt_strcmp(version, "2.0"));
    HX_NEED(svd_number(&b, &at, &sectors) && svd_number(&b, &at, &tracks));
    if (version[0] == '2') HX_NEED(svd_number(&b, &at, &sides) && svd_number(&b, &at, &sizecode) && svd_number(&b, &at, &wprot));
    HX_NEED(tracks && tracks <= 170 && sides && sides <= 2 && sectors && sectors <= (old ? 28U : 26U) && sizecode <= 127 && wprot <= 255);
    header = at;
    HX_NEED(b.n == at + (uint64_t)tracks * sides * (sectors + 1U) * 256U && hx_emit(f, s, &b, "descriptor.svd", 0, header));
    for (i = 0; i < tracks * sides; ++i) {
        uint32_t k, pos = 0, rotation = old ? 7U : 8U;
        bool wd = true;
        uint8_t *plain = NULL;
        if (old) {
            HX_NEED(sectors * 9U <= 256U);
        } else
            for (k = 0; k < sectors; ++k) {
                uint32_t stride;
                uint8_t type;
                HX_NEED(pos < 256);
                type = b.p[at + pos];
                if (type == 1 || type == 2) stride = 10;
                else if (type == 0) {
                    stride = 25U + (k + 1U == sectors ? 5U : 0U);
                    wd = false;
                } else if (type == 16 || type == 32) {
                    stride = 15;
                    wd = false;
                } else if (type == 64) {
                    stride = k ? 10U : 5U;
                    wd = false;
                } else if (type == 4 || type == 8) {
                    stride = 10;
                    wd = false;
                } else goto done;
                HX_NEED(stride <= 256U - pos);
                pos += stride;
            }
        xx_rt_snprintf(name, sizeof(name), "track-C%03u-H%u.descriptor.svd", i / sides, i % sides);
        HX_NEED(hx_emit(f, s, &b, name, at, 256));
        xx_rt_snprintf(name, sizeof(name), "track-C%03u-H%u.stored-blocks.svd", i / sides, i % sides);
        HX_NEED(hx_emit(f, s, &b, name, at + 256, sectors * 256U));
        if (wd) {
            plain = hx_alloc(f, &b, sectors * 256U);
            HX_NEED(plain);
            for (k = 0; k < sectors; ++k) {
                uint32_t j, r = rotation + k * (old ? 9U : 10U);
                if (r >= 256U) {
                    xx_mem_free(plain);
                    goto done;
                }
                for (j = 0; j < 256U; ++j) plain[k * 256U + j] = b.p[at + 256U + k * 256U + (r + j) % 256U];
            }
            xx_rt_snprintf(name, sizeof(name), "track-C%03u-H%u.wd-sectors", i / sides, i % sides);
            if (!hx_owned(f, s, &b, name, plain, sectors * 256U)) {
                xx_mem_free(plain);
                goto done;
            }
        }
        at += (sectors + 1U) * 256U;
    }
    xx_rt_snprintf(
        info, sizeof(info),
        "Format: Semi-Virtual Diskette %s\nCylinders: %u\nSides: %u\nBlocks per track: %u\nDeclared sector-size code: %u\nWrite protected: %u\nRepresentation: original "
        "256-byte descriptor/rotated blocks; WD-only tracks additionally unrotated\nIntegrity: framing; preserved sector CRCs may intentionally be invalid\n",
        version, tracks, sides, sectors, sizecode, wprot);
    HX_NEED(hx_text(f, s, &b, info));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
HX_API(svd, XX_FILE_TYPE_SVD, "svd")
