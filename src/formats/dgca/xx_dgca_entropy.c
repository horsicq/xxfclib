/* Independent DGCA entropy model. SPDX-License-Identifier: MIT. */
#include "dgca_entropy.h"
#include "dgca_range.h"
#include <string.h>
typedef struct dg_model {
    uint32_t global[256], context[256][256], weights[32], runs[256][32];
} dg_model;
static int stopped(const dg_callbacks *cb)
{
    return cb->cancelled && cb->cancelled(cb->opaque);
}
static uint32_t update(uint32_t p, unsigned bit, unsigned rate)
{
    return bit ? p - (p >> rate) : p + ((4096 - p) >> rate);
}
static unsigned weight_index(uint32_t value)
{
    unsigned i = 0;
    if (!value) return 31;
    while (value >>= 1) i++;
    return i;
}
static int symbol(dg_range *r, dg_model *m, unsigned previous, uint32_t *last_score, uint32_t params, unsigned *out)
{
    unsigned i, node = 1, value = 0, bit, index = weight_index(*last_score), leaf = 256 | previous;
    unsigned nodes[8], bits[8];
    uint32_t saved[8], mass = 8192;
    unsigned global_rate = (params >> 4) & 15, context_rate = (params >> 8) & 15;
    unsigned reverse_rate = (params >> 12) & 15, mix_rate = (params >> 16) & 15;
    uint32_t *row = m->context[previous], weight = m->weights[index];
    uint32_t score_global = UINT32_C(1) << 19, score_context = UINT32_C(1) << 19;
    int flat = (params & 2) != 0;
    if (flat) context_rate = reverse_rate;
    if (!flat)
        for (i = 0; i < 8; i++) {
            uint32_t p, amount;
            bit = leaf & 1;
            leaf >>= 1;
            p = m->global[leaf];
            nodes[i] = leaf;
            bits[i] = bit;
            saved[i] = p;
            amount = ((bit ? 4096 - p : p) * mass) >> 13;
            m->global[leaf] = bit ? p + amount : p - amount;
            mass = amount;
        }
    for (i = 0; i < 8; i++) {
        uint32_t pg = m->global[node], pc = row[node], prob;
        prob = (pg * weight + pc * (4096 - weight)) >> 12;
        if (!dg_range_bit(r, prob, &bit)) return 0;
        score_global = (score_global * (bit ? 4096 - pg : pg)) >> 12;
        score_context = (score_context * (bit ? 4096 - pc : pc)) >> 12;
        row[node] = update(pc, bit, context_rate);
        if (flat) m->global[node] = update(pg, bit, global_rate);
        value = value * 2 + bit;
        node = node * 2 + bit;
    }
    if (!flat)
        for (i = 0; i < 8; i++) {
            m->global[nodes[i]] = update(saved[i], bits[i], global_rate);
            m->context[value][nodes[i]] = update(m->context[value][nodes[i]], bits[i], reverse_rate);
        }
    *last_score = (weight * score_global + (4096 - weight) * score_context) >> 12;
    if (score_global > score_context) m->weights[index] = weight + ((4096 - weight) >> mix_rate);
    else if (score_global < score_context) m->weights[index] = weight - (weight >> mix_rate);
    *out = value;
    return 1;
}
dg_status dg_entropy_decode(const dg_callbacks *cb, const unsigned char *data, size_t size, unsigned char *output, size_t count, uint32_t params)
{
    dg_model *m;
    dg_range range;
    size_t i, pos = 0;
    uint32_t *probs, last_score = 0;
    unsigned previous = params & 1 ? 255 : 0, rate = (params >> 20) & 15;
    dg_status status = DG_OK;
    if (!cb || !cb->allocate || !cb->release || (count && !output) || !dg_range_init(&range, data, size)) return DG_FORMAT;
    if (stopped(cb)) return DG_CANCELLED;
    if (!count) return DG_OK;
    m = (dg_model *)cb->allocate(cb->opaque, sizeof(*m));
    if (!m) return DG_MEMORY;
    probs = (uint32_t *)m;
    for (i = 0; i < sizeof(*m) / sizeof(*probs); i++) {
        if (!(i & 4095) && stopped(cb)) {
            status = DG_CANCELLED;
            goto done;
        }
        probs[i] = 2048;
    }
    while (pos < count) {
        unsigned value;
        uint32_t extra = 0;
        size_t run;
        if (stopped(cb)) {
            status = DG_CANCELLED;
            break;
        }
        if (!symbol(&range, m, previous, &last_score, params, &value)) {
            status = DG_FORMAT;
            break;
        }
        previous = value;
        if (!(params & 2) && !dg_range_integer(&range, m->runs[value], rate, &extra)) {
            status = DG_FORMAT;
            break;
        }
        if (extra >= count - pos) {
            status = DG_FORMAT;
            break;
        }
        run = (size_t)extra + 1;
        while (run) {
            size_t n = run > 4096 ? 4096 : run;
            if (stopped(cb)) {
                status = DG_CANCELLED;
                goto done;
            }
            memset(output + pos, value, n);
            pos += n;
            run -= n;
        }
    }
done:
    cb->release(cb->opaque, m);
    return status;
}
