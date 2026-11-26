/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * AutoPlay IR_ZCD launcher credentials. This derives one candidate; the shared
 * SFX ZIP adapter validates complete plaintext and CRC before exposing it. */
#ifndef XX_AUTOPLAY_PASSWORD_H
#define XX_AUTOPLAY_PASSWORD_H
#include "xxfclib/algo/md5/xx_md5.h"
static const uint8_t *autoplay_find(const uint8_t *p, size_t n, const char *tag)
{
    size_t i, k = xx_rt_strlen(tag);
    for (i = 0; k <= n && i <= n - k; ++i)
        if (!xx_rt_memcmp(p + i, tag, k)) return p + i;
    return NULL;
}
static bool autoplay_field(const uint8_t *p, size_t n, const char *open, const char *close, const uint8_t **value, size_t *length)
{
    const uint8_t *a = autoplay_find(p, n, open), *b;
    size_t skip;
    if (!a) return false;
    skip = (size_t)(a - p) + xx_rt_strlen(open);
    a = p + skip;
    if (autoplay_find(a, n - skip, open) || !(b = autoplay_find(a, n - skip, close))) return false;
    *value = a;
    *length = (size_t)(b - a);
    return true;
}
static void autoplay_locate(Abstractformat *f, const xx_list_s *opts, demolition_state *state, xx_pd_struct *pd)
{
    uint8_t h[10], digest[16], *config = NULL;
    const uint8_t *pid, *enabled;
    size_t n = 0, pid_size, enabled_size, i, end;
    int64_t overlay, cab, cabend, cursor = xx_io_tell(f->device);
    uint64_t budget = 1048576U;
    char password[33];
    static const char hex[] = "0123456789abcdef";
    if (!state || state->count >= DEMOLITION_CANDIDATES || carrier_stop(pd) || !demolition_lower_limit(f, opts, XX_META_ID_OPT_MEMORY_LIMIT, &budget) ||
        !carrier_pe(f, &overlay, &cab, &cabend, pd) || !pm_read(f, overlay, h, sizeof(h)) || xx_rt_memcmp(h, "\x0b\x0b\xaf\xaf\x0b\x0b", 6U))
        goto done;
    n = xx_data_get_u32(h + 6, 4, 0, false);
    if (n < 64U || n > 65536U || n + sizeof(demolition_state) + 4096U > budget || !carrier_range(pm_available(f), (uint64_t)overlay + 10U, n + 4U)) goto done;
    config = (uint8_t *)xx_mem_alloc(n);
    if (!config || !pm_read(f, overlay + 10, config, n) || !pm_read(f, overlay + 10 + (int64_t)n, h, 4U) || xx_rt_memcmp(h, "PK\3\4", 4U)) goto done;
    for (i = 0; i < n; ++i)
        if (!config[i] || carrier_stop(pd)) goto done;
    end = n;
    while (end && (config[end - 1U] == '\r' || config[end - 1U] == '\n')) --end;
    if (end < 17U || xx_rt_memcmp(config, "<IR_ZCD>", 8U) || xx_rt_memcmp(config + end - 9U, "</IR_ZCD>", 9U) ||
        !autoplay_field(config, end, "<UsePW>", "</UsePW>", &enabled, &enabled_size) || enabled_size != 1U || enabled[0] != '1' ||
        !autoplay_field(config, end, "<pid>", "</pid>", &pid, &pid_size) || pid_size != 36U)
        goto done;
    for (i = 0; i < pid_size; ++i) {
        unsigned c = pid[i];
        if (i == 8U || i == 13U || i == 18U || i == 23U) {
            if (c != '-') goto done;
        } else if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) goto done;
    }
    if (carrier_stop(pd) || !xx_md5_memory(pid, pid_size, digest)) goto done;
    for (i = 0; i < 16U; ++i) {
        password[i * 2U] = hex[digest[i] >> 4];
        password[i * 2U + 1U] = hex[digest[i] & 15U];
    }
    password[32] = 0;
    for (i = 0; i < state->count; ++i)
        if (!xx_rt_strcmp(state->candidates[i], password)) goto done;
    xx_rt_memcpy(state->candidates[state->count++], password, sizeof(password));
done:
    if (config) {
        xx_mem_zero(config, n);
        xx_mem_free(config);
    }
    xx_mem_zero(password, sizeof(password));
    xx_mem_zero(digest, sizeof(digest));
    if (cursor >= 0) (void)xx_io_seek64(f->device, cursor, SEEK_SET);
}
#endif
