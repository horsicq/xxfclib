/* SPDX-License-Identifier: MIT. Independently authored FEAD action inversion.
 * Source installers and bundled compressor DLLs are treated only as data. */
#include "fead_restore.h"
#include "fead_zlib_encoder.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/data/xx_pd.h"
#include <limits.h>

#define FEAD_PNG_TAG 0x504e4700U
#define FEAD_ZLIB_TAG 0x5a4c4942U
#define FEAD_CAB_TAG 0x43414200U

static uint16_t fr_u16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8); }
static uint32_t fr_u32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static bool fr_live(const fead_restore_context *c)
{ return !xx_pd_is_stopped(c->pd); }
static const fead_resource *fr_resource(const fead_restore_context *c, uint32_t tag, uint32_t id)
{
 const fead_resource *result = NULL;
 size_t i;
 for (i = 0; i < c->resource_count; ++i) {
  if (c->resources[i].tag == tag && c->resources[i].id == id) {
   if (result) return NULL;
   result = &c->resources[i];
  }
 }
 return result;
}
static void *fr_alloc(const fead_restore_context *c, size_t n)
{
 void *p;
 uint64_t used = *c->memory_used;
 if (!n || used > c->memory_limit || n > c->memory_limit - used || !fr_live(c)) return NULL;
 p = xx_mem_alloc(n);
 if (p) *c->memory_used += n;
 return p;
}
static void fr_free(const fead_restore_context *c, void *p, size_t n)
{ if (p) { xx_mem_free(p); *c->memory_used -= n; } }
static bool fr_copy(const fead_restore_context *c, uint8_t *out, size_t capacity, size_t *written,
                    const uint8_t *in, size_t n)
{
 size_t copied = 0;
 if (*written > capacity || n > capacity - *written) return false;
 while (copied < n) {
  size_t take = n - copied;
  if (!fr_live(c)) return false;
  if (take > 65536U) take = 65536U;
  xx_mem_copy(out + *written + copied, in + copied, take);
  copied += take;
 }
 *written += n; return true;
}
static bool fr_word(const fead_resource *r, size_t *p, uint32_t *v)
{
 if (*p > r->size || r->size - *p < 4) return false;
 *v = fr_u32(r->data + *p); *p += 4; return true;
}

static bool fr_png(const fead_restore_context *c, const uint8_t *in, size_t n,
                   const fead_resource *r, uint8_t *out, size_t cap, size_t *written)
{
 size_t p = 0, used = 0, produced = 0;
 uint32_t logical_end, logical_start, at, literal, skip, tail;
 if (!r || !r->data || !fr_word(r, &p, &logical_end) ||
     !fr_word(r, &p, &logical_start) || logical_end < logical_start ||
     (uint64_t)logical_end - logical_start != n) return false;
 for (;;) {
  size_t raw;
  if (!fr_live(c) || !fr_word(r, &p, &at) || at < logical_start ||
      (uint64_t)at - logical_start < produced) return false;
  raw = (size_t)(at - logical_start) - produced;
  if (used > n || raw > n - used || !fr_copy(c, out, cap, &produced, in + used, raw)) return false;
  used += raw;
  if (!fr_word(r, &p, &literal)) return false;
  if (!literal) break;
  if (p > r->size || literal > r->size - p ||
      !fr_copy(c, out, cap, &produced, r->data + p, literal)) return false;
  p += literal;
 }
 if (!fr_word(r, &p, &skip) || !fr_word(r, &p, &tail) || skip > n - used) return false;
 used += skip;
 if (tail != n - used || !fr_copy(c, out, cap, &produced, in + used, tail) ||
     produced != n || p != r->size || !fr_live(c)) return false;
 *written = produced; return true;
}
static bool fr_zlib(const fead_restore_context *c, const uint8_t *in, size_t n,
                    const fead_resource *r, uint8_t *out, size_t cap, size_t *written)
{
 size_t p = 0, used = 0, produced = 0;
 if (!r || !r->data || !r->size) return false;
 while (p < r->size) {
  uint32_t gap, plain;
  uint16_t params;
  uint8_t patched;
  int delta = 0;
  const uint8_t *footer = NULL;
  size_t packed, adjusted;
  uint8_t checksum[4];
  if (!fr_live(c) || r->size - p < 13) return false;
  /* The leading u16 is an opaque encoder ID, unused by the original inverse. */
  gap = fr_u32(r->data + p + 2); plain = fr_u32(r->data + p + 6);
  params = fr_u16(r->data + p + 10); patched = r->data[p + 12]; p += 13;
  if (patched > 1) return false;
  if (patched) {
   if (r->size - p < 11) return false;
   delta = (int)(int8_t)r->data[p]; footer = r->data + p + 1; p += 11;
  }
  if (used > n || gap > n - used || !fr_copy(c, out, cap, &produced, in + used, gap)) return false;
  used += gap;
  if (plain > n - used || produced > cap ||
      !fead_zlib_encode_bounded(c, in + used, plain, out + produced, cap - produced,
       (params >> 12) & 15U, (params >> 8) & 15U, (params >> 4) & 15U,
       params & 15U, &packed)) return false;
  used += plain; adjusted = packed;
  if (patched) {
   if (packed < 4) return false;
   xx_mem_copy(checksum, out + produced + packed - 4, 4);
   if (delta >= 0) {
    if ((unsigned)delta > packed) return false;
    adjusted = packed - (unsigned)delta;
   } else {
    if ((size_t)(-delta) > cap - produced - packed) return false;
    adjusted = packed + (unsigned)(-delta);
   }
   if (adjusted < 14 || adjusted > (uint64_t)plain + 100U || adjusted > cap - produced) return false;
   xx_mem_copy(out + produced + adjusted - 14, footer, 10);
   xx_mem_copy(out + produced + adjusted - 4, checksum, 4);
  }
  produced += adjusted;
 }
 if (!fr_copy(c, out, cap, &produced, in + used, n - used) || !fr_live(c)) return false;
 *written = produced; return true;
}

static int64_t fr_civil_days(int year, unsigned month, unsigned day)
{
 int era;
 unsigned yoe, doy, doe;
 year -= month <= 2;
 era = (year >= 0 ? year : year - 399) / 400;
 yoe = (unsigned)(year - era * 400);
 doy = (153U * (month > 2 ? month - 3 : month + 9) + 2U) / 5U + day - 1U;
 doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
 return (int64_t)era * 146097 + doe - 719468;
}
static uint64_t fr_filetime(uint16_t date, uint16_t time)
{
 unsigned y = (date >> 9) + 1980U, m = (date >> 5) & 15U, d = date & 31U;
 unsigned h = time >> 11, minute = (time >> 5) & 63U, s = (time & 31U) * 2U;
 unsigned mdays;
 int64_t seconds;
 if (!date || !m || m > 12 || !d || h > 23 || minute > 59 || s > 59) return 0;
 mdays = m == 2 ? (y % 4U == 0 && (y % 100U != 0 || y % 400U == 0) ? 29U : 28U) :
          (m == 4 || m == 6 || m == 9 || m == 11 ? 30U : 31U);
 if (d > mdays) return 0;
 seconds = fr_civil_days((int)y, m, d) * 86400 + h * 3600U + minute * 60U + s;
 return (uint64_t)(seconds + 11644473600LL) * 10000000ULL;
}
static bool fr_emit_name(const fead_restore_context *c, const char *parent,
 const uint8_t *name, size_t length, const uint8_t *data, size_t size,
 uint64_t time, uint32_t flags, bool directory)
{
 size_t parent_size = xx_rt_strlen(parent), i, bytes;
 char *joined;
 bool ok;
 if (parent_size > 65535U || !length || length > 65535U ||
     parent_size > SIZE_MAX - length - 2U) return false;
 bytes = parent_size + length + 2U;
 joined = (char *)fr_alloc(c, bytes);
 if (!joined) return false;
 xx_mem_copy(joined, parent, parent_size); joined[parent_size] = '/';
 for (i = 0; i < length; ++i) joined[parent_size + 1U + i] = name[i] == '\\' ? '/' : (char)name[i];
 joined[bytes - 1] = '\0';
 ok = fr_live(c) && c->emit(c->user, joined, data, size, time, flags, directory);
 fr_free(c, joined, bytes);
 return ok && fr_live(c);
}

/* File names in this observed CAB grammar are ASCII or declared UTF-8. Other
 * codepages are rejected rather than silently changing names. The emitter
 * independently enforces destination path safety and uniqueness. */
static bool fr_cab_name_ok(const uint8_t *name, size_t n, uint16_t attrs)
{
 size_t i = 0, component = 0;
 if (!n || n > 65535U) return false;
 while (i < n) {
  uint32_t cp;
  unsigned count, j;
  uint8_t first = name[i++];
  if (first < 0x80U) continue;
  if (!(attrs & 0x80U)) return false;
  if (first >= 0xc2U && first <= 0xdfU) { cp = first & 31U; count = 1; }
  else if (first >= 0xe0U && first <= 0xefU) { cp = first & 15U; count = 2; }
  else if (first >= 0xf0U && first <= 0xf4U) { cp = first & 7U; count = 3; }
  else return false;
  if (count > n - i) return false;
  for (j = 0; j < count; ++j) {
   if ((name[i] & 0xc0U) != 0x80U) return false;
   cp = (cp << 6) | (name[i++] & 63U);
  }
  if ((count == 1 && cp < 0x80U) || (count == 2 && cp < 0x800U) ||
      (count == 3 && cp < 0x10000U) || cp > 0x10ffffU ||
      (cp >= 0xd800U && cp <= 0xdfffU)) return false;
 }
 /* The common emitter handles ordinary DOS names; these two console device
  * names also need rejection before any output path is considered. */
 for (i = 0; i <= n; ++i) {
  if (i == n || name[i] == '/' || name[i] == '\\') {
   size_t stem = component, length;
   while (stem < i && name[stem] != '.') ++stem;
   length = stem - component;
   if (length == 6 || length == 7) {
    const char *reserved = length == 6 ? "conin$" : "conout$";
    size_t j;
    for (j = 0; j < length; ++j)
     if (xx_rt_ascii_tolower(name[component + j]) != (unsigned char)reserved[j]) break;
    if (j == length) return false;
   }
   component = i + 1;
  }
 }
 return true;
}
static bool fr_cab(const fead_restore_context *c, size_t first, size_t last,
 size_t start, const fead_outer_member *member, uint32_t *png_id, uint32_t *zlib_id)
{
 const uint8_t *source = c->data + start, *header;
 const fead_resource *meta;
 size_t input_end = c->actions[last - 1].end, input_n, prefix, header_n, p;
 size_t body_n = 0, workspace_n = 0, produced = 0, i, folder_bytes;
 uint16_t id, folders, files;
 uint32_t suffix, original_n;
 uint64_t *ends = NULL;
 uint8_t *body = NULL;
 bool ok = false;
 if (input_end < start) return false;
 input_n = input_end - start;
 if (input_n < 2 || !fr_live(c)) return false;
 id = fr_u16(source); meta = fr_resource(c, FEAD_CAB_TAG, id);
 /* Original CAB optimiser mode -1, one carrier skip, no extra lists. Mode0
  * uses the same raw folder layout and is accepted when the header agrees. */
 if (!meta || !meta->data || meta->size != 16 ||
     (fr_u32(meta->data) != UINT32_MAX && fr_u32(meta->data) != 0) ||
     fr_u32(meta->data + 4) != 1 || fr_u32(meta->data + 12) != 0) return false;
 prefix = fr_u32(meta->data + 8);
 if (prefix > input_n - 2 || input_n - 2 - prefix < 36) return false;
 header = source + 2 + prefix; header_n = fr_u32(header + 12);
 original_n = fr_u32(header + 8); suffix = fr_u32(header + 20);
 folders = fr_u16(header + 26); files = fr_u16(header + 28);
 if (xx_mem_compare(header, "_SCF", 4) || fr_u32(header + 4) != 3 ||
     header[24] != 3 || header[25] != 1 || fr_u16(header + 30) != 0 ||
     !folders || !files || header_n < 36U + (size_t)folders * 8U ||
     header_n > input_n - 2 - prefix || original_n < header_n ||
     (uint64_t)prefix + original_n + suffix != member->size ||
     fr_u32(header + 16) != 36U + (uint32_t)folders * 8U ||
     c->actions[first].end < start + 2 + prefix + header_n) return false;
 folder_bytes = (size_t)folders * sizeof(*ends);
 ends = (uint64_t *)fr_alloc(c, folder_bytes);
 if (!ends) return false;
 xx_mem_zero(ends, folder_bytes);
 p = fr_u32(header + 16);
 for (i = 0; i < files; ++i) {
  uint32_t n, off;
  uint16_t folder, attrs;
  size_t name;
  if (!fr_live(c) || p > header_n || header_n - p < 16) goto done;
  n = fr_u32(header + p); off = fr_u32(header + p + 4);
  folder = fr_u16(header + p + 8); attrs = fr_u16(header + p + 14); p += 16; name = p;
  while (p < header_n && header[p]) ++p;
  if (p == header_n || folder >= folders || !fr_cab_name_ok(header + name, p - name, attrs)) goto done;
  if ((uint64_t)off + n > ends[folder]) ends[folder] = (uint64_t)off + n;
  ++p;
 }
 if (p != header_n) goto done;
 for (i = 0; i < folders; ++i) {
  uint32_t packed_start = fr_u32(header + 36U + i * 8U);
  uint16_t blocks = fr_u16(header + 40U + i * 8U);
  uint16_t method = fr_u16(header + 42U + i * 8U);
  if (ends[i] > UINT32_MAX || packed_start < header_n || packed_start >= original_n ||
      (method & 15U) > 3U || (ends[i] + 32767U) / 32768U != blocks ||
      ends[i] > SIZE_MAX - body_n) goto done;
  body_n += (size_t)ends[i];
 }
 if (suffix > SIZE_MAX - body_n) goto done;
 workspace_n = body_n + suffix;
 body = (uint8_t *)fr_alloc(c, workspace_n ? workspace_n : 1);
 if (!body) goto done;
 p = start + 2 + prefix + header_n;
 for (i = first; i < last; ++i) {
  size_t end = c->actions[i].end, n, done_n = 0;
  uint32_t type = c->actions[i].type;
  if (end < p || end > input_end || produced > workspace_n || !fr_live(c)) goto done;
  n = end - p;
  if (i == first) { if (type != 2) goto done; type = 18; }
  if (type == 7) {
   meta = fr_resource(c, FEAD_PNG_TAG, *png_id);
   if (*png_id == UINT32_MAX || !fr_png(c, c->data + p, n, meta,
                                     body + produced, workspace_n - produced, &done_n)) goto done;
   ++*png_id;
  } else if (type == 5) {
   meta = fr_resource(c, FEAD_ZLIB_TAG, *zlib_id);
   if (*zlib_id == UINT32_MAX || !fr_zlib(c, c->data + p, n, meta,
                                      body + produced, workspace_n - produced, &done_n)) goto done;
   ++*zlib_id;
  } else if (type == 18) {
   if (!fr_copy(c, body, workspace_n, &produced, c->data + p, n)) goto done;
  } else goto done;
  if (type != 18) produced += done_n;
  p = end;
 }
 if (p != input_end || produced != workspace_n || !fr_live(c) ||
     !c->emit(c->user, member->name, NULL, 0, member->filetime, member->flags, true)) goto done;
 if (prefix && !fr_emit_name(c, member->name, (const uint8_t *)"__carrier_prefix.bin", 20,
                             source + 2, prefix, member->filetime, 0, false)) goto done;
 if (suffix && !fr_emit_name(c, member->name, (const uint8_t *)"__carrier_overlay.bin", 21,
                             body + body_n, suffix, member->filetime, 0, false)) goto done;
 /* Convert per-folder lengths to bases without an additional table. */
 { uint64_t base = 0;
  for (i = 0; i < folders; ++i) { uint64_t n = ends[i]; ends[i] = base; base += n; }
 }
 p = fr_u32(header + 16);
 for (i = 0; i < files; ++i) {
  uint32_t n = fr_u32(header + p), off = fr_u32(header + p + 4);
  uint16_t folder = fr_u16(header + p + 8), date = fr_u16(header + p + 10);
  uint16_t time = fr_u16(header + p + 12), attrs = fr_u16(header + p + 14);
  size_t name, offset;
  p += 16; name = p; while (p < header_n && header[p]) ++p;
  offset = (size_t)ends[folder] + off;
  if (offset > body_n || n > body_n - offset ||
      !fr_emit_name(c, member->name, header + name, p - name, body + offset, n,
                    fr_filetime(date, time), attrs & 0x27U, false)) goto done;
  ++p;
 }
 ok = fr_live(c);
done:
 fr_free(c, body, workspace_n ? workspace_n : 1);
 fr_free(c, ends, folder_bytes);
 return ok;
}

bool fead_restore_archive(const fead_restore_context *c)
{
 size_t i, start = 0, member = 0;
 uint32_t png_id = 0, zlib_id = 0;
 if (!c || !c->emit || !c->memory_used || *c->memory_used > c->memory_limit ||
     (!c->data && c->size) || !c->members || !c->member_count ||
     !c->actions || !c->action_count || (!c->resources && c->resource_count) ||
     !fr_live(c)) return false;
 /* Reject malformed ordering before publishing any records. */
 for (i = 0; i < c->action_count; ++i) {
  if (c->actions[i].end < start || c->actions[i].end > c->size ||
      (c->actions[i].type != 0 && c->actions[i].type != 2 &&
       c->actions[i].type != 5 && c->actions[i].type != 7 && c->actions[i].type != 18)) return false;
  start = c->actions[i].end;
 }
 if (start != c->size) return false;
 start = 0;
 for (i = 0; i < c->action_count; ++i) {
  size_t end = c->actions[i].end;
  uint32_t type = c->actions[i].type;
  if (!fr_live(c) || member > c->member_count) return false;
  if (type == 0) {
   if (end == start && i + 1 < c->action_count && c->actions[i + 1].type == 2) continue;
   if (member == c->member_count || !c->members[member].name ||
       end - start != c->members[member].size ||
       !c->emit(c->user, c->members[member].name, c->data + start, end - start,
                c->members[member].filetime, c->members[member].flags, false)) return false;
   ++member;
  } else if (type == 2) {
   size_t last = i + 1;
   while (last < c->action_count && c->actions[last].type != 0) ++last;
   if (member == c->member_count || !c->members[member].name ||
       !fr_cab(c, i, last, start, &c->members[member], &png_id, &zlib_id)) return false;
   ++member; end = c->actions[last - 1].end; i = last - 1;
  } else if (type != 18 || end != start) return false;
  start = end;
 }
 return member == c->member_count && start == c->size && fr_live(c);
}
