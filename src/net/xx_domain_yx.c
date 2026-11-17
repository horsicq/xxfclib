/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <xxfclib/net/xx_domain_yx.h>

#include <stdlib.h>
#include <string.h>

/* EnrichedIP.in_range (vt/mod.rs): the address is parsed like Rust's
 * IpAddr::from_str (core::net::parser), the network like ipnet's
 * IpNet::from_str. ipnet's parser is a copy of the older std parser: IPv4
 * octets may have leading zeros there, and `::` may stand for no group. */
typedef struct {
  bool v6;
  uint8_t bytes[16];
} xx_domain_yx_ip;

static int xx_domain_yx_digit(char c, unsigned radix) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (radix == 16 && c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (radix == 16 && c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

/* read_number(radix, Some(max_digits), allow_zero_prefix) of core::net,
 * for a value up to `maximum`. The position only moves on success. */
static bool xx_domain_yx_std_number(const char **at, const char *end, unsigned radix,
                             unsigned max_digits, bool allow_zero_prefix,
                             uint32_t maximum, uint32_t *value) {
  const char *p = *at;
  bool leading_zero = p < end && *p == '0';
  uint32_t result = 0;
  unsigned count = 0;
  int digit;
  while (p < end && (digit = xx_domain_yx_digit(*p, radix)) >= 0) {
    result = result * radix + (uint32_t)digit;
    ++p;
    if (++count > max_digits)
      return false;
  }
  if (!count || result > maximum ||
      (!allow_zero_prefix && leading_zero && count > 1))
    return false;
  *at = p;
  *value = result;
  return true;
}

/* read_number(radix, max_digits, upto) of ipnet's parser. */
static bool xx_domain_yx_net_number(const char **at, const char *end, unsigned radix,
                             unsigned max_digits, uint32_t upto,
                             uint32_t *value) {
  const char *p = *at;
  uint32_t result = 0;
  unsigned count = 0;
  int digit;
  while (p < end && (digit = xx_domain_yx_digit(*p, radix)) >= 0) {
    result = result * radix + (uint32_t)digit;
    ++p;
    if (++count > max_digits || result >= upto)
      return false;
  }
  if (!count)
    return false;
  *at = p;
  *value = result;
  return true;
}

static bool xx_domain_yx_ipv4(const char **at, const char *end, bool net,
                       uint8_t octets[4]) {
  const char *p = *at;
  for (unsigned i = 0; i < 4; ++i) {
    uint32_t octet;
    if (i) {
      if (p >= end || *p != '.')
        return false;
      ++p;
    }
    if (net ? !xx_domain_yx_net_number(&p, end, 10, 3, 0x100, &octet) :
              !xx_domain_yx_std_number(&p, end, 10, 3, false, 255, &octet))
      return false;
    octets[i] = (uint8_t)octet;
  }
  *at = p;
  return true;
}

/* read_groups: colon-separated groups and an optional embedded IPv4
 * address as the last two; returns how many groups were read. */
static size_t xx_domain_yx_ipv6_groups(const char **at, const char *end, bool net,
                                uint16_t *groups, size_t limit, bool *ipv4) {
  const char *p = *at;
  *ipv4 = false;
  for (size_t i = 0; i < limit; ++i) {
    const char *q = p;
    uint32_t group;
    bool separated = !i || (q < end && *q == ':');
    if (i && separated)
      ++q;
    if (separated && i < limit - 1) {
      const char *r = q;
      uint8_t octets[4];
      if (xx_domain_yx_ipv4(&r, end, net, octets)) {
        groups[i] = (uint16_t)((octets[0] << 8) | octets[1]);
        groups[i + 1] = (uint16_t)((octets[2] << 8) | octets[3]);
        *ipv4 = true;
        *at = r;
        return i + 2;
      }
    }
    if (!separated ||
        (net ? !xx_domain_yx_net_number(&q, end, 16, 4, 0x10000, &group) :
               !xx_domain_yx_std_number(&q, end, 16, 4, true, 0xffff, &group))) {
      *at = p;
      return i;
    }
    groups[i] = (uint16_t)group;
    p = q;
  }
  *at = p;
  return limit;
}

static bool xx_domain_yx_ipv6(const char **at, const char *end, bool net,
                       uint8_t bytes[16]) {
  const char *p = *at;
  uint16_t head[8], tail[8], groups[8] = {0};
  bool head_ipv4, tail_ipv4;
  size_t head_size = xx_domain_yx_ipv6_groups(&p, end, net, head, 8, &head_ipv4);
  size_t tail_size = 0;
  if (head_size == 8) {
    memcpy(groups, head, sizeof(groups));
  } else {
    if (head_ipv4 || end - p < 2 || p[0] != ':' || p[1] != ':')
      return false;
    p += 2;
    /* The std parser requires `::` to stand for at least one group. */
    tail_size = xx_domain_yx_ipv6_groups(&p, end, net, tail,
                                  net ? 8 - head_size : 8 - (head_size + 1),
                                  &tail_ipv4);
    memcpy(groups, head, head_size * sizeof(*head));
    memcpy(groups + 8 - tail_size, tail, tail_size * sizeof(*tail));
  }
  for (unsigned i = 0; i < 8; ++i) {
    bytes[i * 2] = (uint8_t)(groups[i] >> 8);
    bytes[i * 2 + 1] = (uint8_t)groups[i];
  }
  *at = p;
  return true;
}

/* IpAddr::from_str: an IPv4 address, else an IPv6 one, then end of input. */
static bool xx_domain_yx_parse_ip(const char *text, size_t length, xx_domain_yx_ip *ip) {
  const char *at = text, *end = text + length;
  memset(ip, 0, sizeof(*ip));
  if (!xx_domain_yx_ipv4(&at, end, false, ip->bytes)) {
    if (!xx_domain_yx_ipv6(&at, end, false, ip->bytes))
      return false;
    ip->v6 = true;
  }
  return at == end;
}

/* IpNet::from_str: `address/prefix` (at most 2 digits below 33 for IPv4, 3
 * digits below 129 for IPv6), then end of input. */
static bool xx_domain_yx_parse_net(const char *text, size_t length, xx_domain_yx_ip *net,
                            unsigned *prefix) {
  const char *end = text + length;
  for (unsigned family = 0; family < 2; ++family) {
    const char *at = text;
    uint32_t bits;
    memset(net, 0, sizeof(*net));
    net->v6 = family == 1;
    if (!(family ? xx_domain_yx_ipv6(&at, end, true, net->bytes) :
                   xx_domain_yx_ipv4(&at, end, true, net->bytes)) ||
        at >= end || *at != '/')
      continue;
    ++at;
    if (!(family ? xx_domain_yx_net_number(&at, end, 10, 3, 129, &bits) :
                   xx_domain_yx_net_number(&at, end, 10, 2, 33, &bits)))
      continue;
    *prefix = bits;
    /* read_or keeps the first network that parses; EOF is checked after. */
    return at == end;
  }
  return false;
}

XXFC_API bool xx_domain_yx_ip_in_range(const char *ip, size_t ip_length, const char *cidr,
                       size_t cidr_length) {
  xx_domain_yx_ip address, network;
  unsigned prefix = 0;
  if (!ip || !cidr || !xx_domain_yx_parse_net(cidr, cidr_length, &network, &prefix) ||
      !xx_domain_yx_parse_ip(ip, ip_length, &address) || address.v6 != network.v6)
    return false;
  for (unsigned i = 0; prefix && i < (address.v6 ? 16u : 4u); ++i) {
    unsigned bits = prefix < 8 ? prefix : 8;
    uint8_t mask = (uint8_t)(0xff00u >> bits);
    if ((address.bytes[i] & mask) != (network.bytes[i] & mask))
      return false;
    prefix -= bits;
  }
  return true;
}

/* UTF-8 validity over the strict Rust str rules (same table the vt
 * module-data decoder uses); domains must be valid UTF-8 before the psl
 * lookup may see them. */
static bool xx_domain_yx_utf8(const uint8_t *s, size_t n)
{
  for (size_t i = 0; i < n;) {
    unsigned c = s[i++], extra, code, minimum;
    if (c < 128)
      continue;
    if (c >= 0xc2 && c <= 0xdf) {
      extra = 1;
      code = c & 31;
      minimum = 128;
    } else if (c >= 0xe0 && c <= 0xef) {
      extra = 2;
      code = c & 15;
      minimum = 2048;
    } else if (c >= 0xf0 && c <= 0xf4) {
      extra = 3;
      code = c & 7;
      minimum = 65536;
    } else
      return false;
    if (extra > n - i)
      return false;
    while (extra--) {
      c = s[i++];
      if ((c & 0xc0) != 0x80)
        return false;
      code = (code << 6) | (c & 63);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
      return false;
  }
  return true;
}

/* psl::suffix (crate psl 2.1.243 with psl-types 2.0.11): the generated
 * lookup over the labels from the right, see tools/gen_psl_table.py. */
#include "xx_psl_yx_data.h"

enum {
  XX_PSL_LEAF = 0,
  XX_PSL_EXCEPTION = 1,
  XX_PSL_RULE_NODE = 2,
  XX_PSL_PLAIN_NODE = 3
};

static const xx_psl_node *xx_domain_yx_psl_child(const xx_psl_node *node,
                                          const uint8_t *label,
                                          size_t length) {
  size_t low = 0, high = node->child_count;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    const xx_psl_node *child = &xx_psl_nodes[node->first_child + middle];
    size_t common = child->label_length < length ? child->label_length : length;
    int order = memcmp(xx_psl_labels + child->label, label, common);
    if (!order)
      order = (child->label_length > length) - (child->label_length < length);
    if (!order)
      return child;
    if (order < 0)
      low = middle + 1;
    else
      high = middle;
  }
  return NULL;
}

/* list::lookup over the labels of name[0..end). Returns Info::len. */
static size_t xx_domain_yx_psl_lookup(const uint8_t *name, size_t end) {
  const xx_psl_node *node = &xx_psl_nodes[0];
  size_t info = 0, acc = 0, label_end = end;
  unsigned depth = 0;
  for (;;) {
    size_t label_start = label_end, length, child_acc;
    const xx_psl_node *child;
    while (label_start && name[label_start - 1] != '.')
      --label_start;
    length = label_end - label_start;
    child = xx_domain_yx_psl_child(node, name + label_start, length);
    if (!child) {
      if ((node->info >> 4) & 3) /* wildcard rule */
        return acc + 1 + length;
      /* The implicit `*` rule: an unknown top-level label is a suffix. */
      return depth ? info : length;
    }
    child_acc = depth ? acc + 1 + length : length;
    switch ((child->info >> 2) & 3) {
    case XX_PSL_LEAF:
      return child_acc;
    case XX_PSL_EXCEPTION:
      return acc;
    case XX_PSL_RULE_NODE:
      info = child_acc;
      break;
    default:
      if (!depth)
        info = child_acc;
      break;
    }
    if (!label_start)
      return info;
    acc = child_acc;
    node = child;
    ++depth;
    label_end = label_start - 1;
  }
}

XXFC_API size_t xx_domain_yx_public_suffix_length(const char *name, size_t length) {
  const uint8_t *bytes = (const uint8_t *)name;
  /* A fully qualified name skips its last, empty label and counts the
   * trailing dot as part of the suffix. */
  bool fqdn = length && bytes[length - 1] == '.';
  size_t suffix = xx_domain_yx_psl_lookup(bytes, fqdn ? length - 1 : length);
  if (fqdn)
    ++suffix;
  return suffix;
}

static bool xx_domain_yx_part_is(const char *part, size_t length,
                          const char *expected) {
  return part && length == strlen(expected) && !memcmp(part, expected, length);
}

XXFC_API bool xx_domain_yx_parse_domain(const char *name, size_t length,
                        xx_domain_yx_parts *parts) {
  size_t suffix = xx_domain_yx_public_suffix_length(name, length), prefix;
  memset(parts, 0, sizeof(*parts));
  if (!suffix || suffix > length ||
      !xx_domain_yx_utf8((const uint8_t *)name + length - suffix, suffix))
    return false;
  parts->tld = name + length - suffix;
  parts->tld_length = suffix;
  if (length <= suffix + 1)
    return true;
  prefix = length - suffix - 1;
  if (!xx_domain_yx_utf8((const uint8_t *)name, prefix))
    return false;
  parts->prefix = name;
  parts->prefix_length = prefix;
  parts->domain = name;
  parts->domain_length = prefix;
  for (size_t i = prefix; i > 0; --i)
    if (name[i - 1] == '.') {
      parts->subdomain = name;
      parts->subdomain_length = i - 1;
      parts->domain = name + i;
      parts->domain_length = prefix - i;
      break;
    }
  /* psl ranks "www.gov.uk" as domain "www" of suffix "gov.uk"; the
   * reference moves such common host names to the subdomain. */
  if (xx_domain_yx_part_is(parts->domain, parts->domain_length, "www") ||
      xx_domain_yx_part_is(parts->domain, parts->domain_length, "ftp") ||
      xx_domain_yx_part_is(parts->domain, parts->domain_length, "m") ||
      xx_domain_yx_part_is(parts->domain, parts->domain_length, "mail") ||
      xx_domain_yx_part_is(parts->domain, parts->domain_length, "webmail") ||
      xx_domain_yx_part_is(parts->domain, parts->domain_length, "ns1") ||
      xx_domain_yx_part_is(parts->domain, parts->domain_length, "ns2")) {
    parts->subdomain = parts->domain;
    parts->subdomain_length = parts->domain_length;
    parts->domain = NULL;
    parts->domain_length = 0;
  }
  return true;
}

/* The permutation engines compare Unicode scalar values (Rust chars). */
typedef struct {
  uint32_t *items;
  size_t count;
  size_t at;
} xx_domain_yx_chars;

static bool xx_domain_yx_chars_init(xx_domain_yx_chars *chars, const char *text,
                             size_t length) {
  const uint8_t *bytes = (const uint8_t *)text;
  memset(chars, 0, sizeof(*chars));
  /* One decoded scalar per input byte at most; reject sizes whose byte
   * count would overflow the allocation multiplier (32-bit targets). */
  if (length > SIZE_MAX / sizeof(uint32_t))
    return false;
  chars->items = (uint32_t *)malloc((length ? length : 1) * sizeof(uint32_t));
  if (!chars->items)
    return false;
  for (size_t i = 0; i < length;) {
    uint32_t c = bytes[i++];
    unsigned extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    if (extra)
      c &= 0x3f >> extra;
    while (extra-- && i < length)
      c = (c << 6) | (bytes[i++] & 0x3f);
    chars->items[chars->count++] = c;
  }
  return true;
}

/* Iterator::next for Chars: 0 (no char) once exhausted. */
static bool xx_domain_yx_next(xx_domain_yx_chars *chars, uint32_t *c) {
  if (chars->at >= chars->count)
    return false;
  *c = chars->items[chars->at++];
  return true;
}

/* a_chars.eq(b_chars) over what remains of both. */
static bool xx_domain_yx_rest_equal(const xx_domain_yx_chars *a, const xx_domain_yx_chars *b) {
  size_t left = a->count - a->at, right = b->count - b->at;
  return left == right &&
         (!left || !memcmp(a->items + a->at, b->items + b->at,
                           left * sizeof(uint32_t)));
}

static bool xx_domain_yx_qwerty_neighbours(uint32_t a, uint32_t b) {
  static const char *const rows[][2] = {
    {"1", "2q"},     {"2", "13qw"},   {"3", "24we"},   {"4", "35er"},
    {"5", "46rt"},   {"6", "57ty"},   {"7", "68yu"},   {"8", "79ui"},
    {"9", "80io"},   {"0", "9op"},    {"q", "12wa"},   {"w", "qeas23"},
    {"e", "wrsd34"}, {"r", "etdf45"}, {"t", "ryfg56"}, {"y", "tugh67"},
    {"u", "yihj78"}, {"i", "uojk89"}, {"o", "ipkl90"}, {"p", "ol0"},
    {"a", "qwsz"},   {"s", "awedzx"}, {"d", "serfxc"}, {"f", "drtgcv"},
    {"g", "ftyhvb"}, {"h", "gyujbn"}, {"j", "huiknm"}, {"k", "jiolm"},
    {"l", "kop"},    {"z", "asx"},    {"x", "zsdc"},   {"c", "xdfv"},
    {"v", "cfgb"},   {"b", "vghn"},   {"n", "bhjm"},   {"m", "njk"}
  };
  if (a > 0x7f || b > 0x7f || !b)
    return false;
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
    if ((uint32_t)(unsigned char)rows[i][0][0] == a)
      return strchr(rows[i][1], (int)b) != NULL;
  return false;
}

/* typos.rs insertion: one character inserted in `b`, next to its
 * neighbours on a QWERTY keyboard. */
static bool xx_domain_yx_insertion(xx_domain_yx_chars *a, xx_domain_yx_chars *b) {
  bool has_prev = false;
  uint32_t prev = 0, x, y, skipped;
  for (;;) {
    bool has_a = xx_domain_yx_next(a, &x), has_b = xx_domain_yx_next(b, &y);
    if (has_a && has_b && x == y) {
      prev = x;
      has_prev = true;
    } else if ((has_a && has_b && xx_domain_yx_qwerty_neighbours(x, y)) ||
               (has_prev && has_b && xx_domain_yx_qwerty_neighbours(prev, y))) {
      (void)xx_domain_yx_next(b, &skipped);
      return xx_domain_yx_rest_equal(a, b);
    } else
      return false;
  }
}

/* typos.rs omission: one character of `a` missing in `b`. */
static bool xx_domain_yx_omission(xx_domain_yx_chars *a, xx_domain_yx_chars *b) {
  uint32_t x, y, z;
  for (;;) {
    bool has_a = xx_domain_yx_next(a, &x), has_b = xx_domain_yx_next(b, &y);
    if (has_a && has_b) {
      if (x != y)
        return xx_domain_yx_next(a, &z) && z == y && xx_domain_yx_rest_equal(a, b);
    } else if (has_a)
      return !xx_domain_yx_next(a, &z);
    else
      return false;
  }
}

/* typos.rs replacement: one character replaced by a QWERTY neighbour. */
static bool xx_domain_yx_replacement(xx_domain_yx_chars *a, xx_domain_yx_chars *b) {
  uint32_t x, y;
  for (;;) {
    bool has_a = xx_domain_yx_next(a, &x), has_b = xx_domain_yx_next(b, &y);
    if (!has_a || !has_b)
      return false;
    if (x != y)
      return xx_domain_yx_qwerty_neighbours(x, y) && xx_domain_yx_rest_equal(a, b);
  }
}

static bool xx_domain_yx_is_vowel(uint32_t c) {
  return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

/* typos.rs vowel_swap: one vowel replaced by another vowel. */
static bool xx_domain_yx_vowel_swap(xx_domain_yx_chars *a, xx_domain_yx_chars *b) {
  uint32_t x, y;
  for (;;) {
    bool has_a = xx_domain_yx_next(a, &x), has_b = xx_domain_yx_next(b, &y);
    if (!has_a || !has_b)
      return false;
    if (x != y)
      return xx_domain_yx_is_vowel(x) && xx_domain_yx_is_vowel(y) && xx_domain_yx_rest_equal(a, b);
  }
}

/* typos.rs doubling: one character of `a` doubled in `b`. */
static bool xx_domain_yx_doubling(xx_domain_yx_chars *a, xx_domain_yx_chars *b) {
  bool has_prev = false;
  uint32_t prev = 0, x, y, skipped;
  for (;;) {
    bool has_a = xx_domain_yx_next(a, &x), has_b = xx_domain_yx_next(b, &y);
    if (has_a && has_b && x == y) {
      prev = x;
      has_prev = true;
    } else if (has_prev && has_a && has_b) {
      if (prev != y)
        return false;
      (void)xx_domain_yx_next(b, &skipped);
      return xx_domain_yx_rest_equal(a, b);
    } else if (has_prev && !has_a && has_b)
      return prev == y && xx_domain_yx_rest_equal(a, b);
    else
      return false;
  }
}

/* typos.rs swap: exactly one pair of adjacent characters swapped. */
static bool xx_domain_yx_swap(xx_domain_yx_chars *a, xx_domain_yx_chars *b) {
  bool has_prev = false;
  uint32_t prev_a = 0, prev_b = 0, x, y;
  unsigned swaps = 0;
  for (;;) {
    bool has_a = xx_domain_yx_next(a, &x), has_b = xx_domain_yx_next(b, &y);
    if (has_a && has_b && (x == y || !has_prev)) {
      prev_a = x;
      prev_b = y;
      has_prev = true;
    } else if (has_prev && has_a && has_b && x == prev_b && y == prev_a) {
      ++swaps;
      has_prev = false;
    } else if (!has_a && !has_b)
      return swaps == 1 && (!has_prev || prev_a == prev_b);
    else
      return false;
  }
}

static const struct {
  uint32_t base;
  uint32_t glyphs[17];
} xx_domain_yx_homoglyph_table[] = {
  {'a', {0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x0251, 0x1EA1, 0x01CE, 0x0103, 0x0227, 0x0105}},
  {'b', {0x0064, 0x0299, 0x0253, 0x1E03, 0x1E05, 0x1E07, 0x0185}},
  {'c', {0x0065, 0x0188, 0x010B, 0x0107, 0x00E7, 0x010D, 0x0109, 0x006F}},
  {'d', {0x0062, 0x0257, 0x0111, 0x010F, 0x0256, 0x1E11, 0x1E0B, 0x1E0D, 0x1E0F, 0x1E13}},
  {'e', {0x0063, 0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x0113, 0x0115, 0x011B, 0x0117, 0x1EB9, 0x0119, 0x0229, 0x0247, 0x1E1B}},
  {'f', {0x0192, 0x1E1F}},
  {'g', {0x0071, 0x0262, 0x0261, 0x0121, 0x011F, 0x01F5, 0x0123, 0x011D, 0x01E7, 0x01E5}},
  {'h', {0x0125, 0x021F, 0x0127, 0x0266, 0x1E27, 0x1E29, 0x2C68, 0x1E23, 0x1E25, 0x1E2B, 0x1E96}},
  {'i', {0x0031, 0x006C, 0x00ED, 0x00EC, 0x00EF, 0x0131, 0x0269, 0x01D0, 0x012D, 0x1EC9, 0x1ECB, 0x0268, 0x020B, 0x012B}},
  {'j', {0x029D, 0x0249}},
  {'k', {0x1E33, 0x1E35, 0x2C6A, 0x0137}},
  {'l', {0x0031, 0x0069, 0x026B, 0x0142}},
  {'m', {0x006E, 0x1E41, 0x1E43, 0x1D0D, 0x0271, 0x1E3F}},
  {'n', {0x006D, 0x0072, 0x0144, 0x1E45, 0x1E47, 0x1E49, 0x00F1, 0x0146, 0x01F9, 0x0148, 0xA791}},
  {'o', {0x0030, 0x022F, 0x1ECD, 0x1ECF, 0x01A1, 0x00F3, 0x00F6, 0x0151}},
  {'p', {0x01BF, 0x01A5, 0x1E55, 0x1E57}},
  {'q', {0x0067, 0x02A0}},
  {'r', {0x0280, 0x027C, 0x027D, 0x0155, 0x0157, 0x0159, 0x024D, 0x027E, 0x0213, 0x0211, 0x1E59, 0x1E5B, 0x1E5F}},
  {'s', {0x0282, 0x015B, 0x1E63, 0x1E61, 0x0219, 0x015D, 0x0161}},
  {'t', {0x0163, 0x0167, 0x1E6B, 0x1E6D, 0x021B, 0x01AB}},
  {'u', {0x1D1C, 0x01D4, 0x016D, 0x00FC, 0x0289, 0x00F9, 0x00FA, 0x00FB, 0x0169, 0x016B, 0x0173, 0x01B0, 0x016F, 0x0171, 0x0215, 0x0217, 0x1EE5}},
  {'v', {0x1E7F, 0x2C71, 0x1D8C, 0x1E7D, 0x2C74}},
  {'w', {0x0175, 0x1E81, 0x1E83, 0x1E85, 0x2C73, 0x1E87, 0x1E89, 0x1E98}},
  {'y', {0x028F, 0x00FD, 0x00FF, 0x0177, 0x01B4, 0x0233, 0x024F, 0x1EFF, 0x1E8F, 0x1EF5}},
  {'z', {0x0290, 0x017C, 0x017A, 0x1D22, 0x01B6, 0x1E93, 0x1E95, 0x2C6C}}
};

/* homoglyphs.rs is_homoglyph_char: `b` looks like `a`. */
static bool xx_domain_yx_homoglyph_char(uint32_t a, uint32_t b) {
  for (size_t i = 0; i < sizeof(xx_domain_yx_homoglyph_table) /
                         sizeof(xx_domain_yx_homoglyph_table[0]); ++i) {
    if (xx_domain_yx_homoglyph_table[i].base != a)
      continue;
    for (size_t j = 0; j < 17 && xx_domain_yx_homoglyph_table[i].glyphs[j]; ++j)
      if (xx_domain_yx_homoglyph_table[i].glyphs[j] == b)
        return true;
    return false;
  }
  return false;
}

/* homoglyphs.rs is_homoglyph: same length, at least one character pair
 * that are homoglyphs in either direction, the others equal. */
static bool xx_domain_yx_is_homoglyph(xx_domain_yx_chars *a, xx_domain_yx_chars *b) {
  bool homoglyph = false;
  uint32_t x, y;
  for (;;) {
    bool has_a = xx_domain_yx_next(a, &x), has_b = xx_domain_yx_next(b, &y);
    if (has_a && has_b) {
      if (x == y)
        continue;
      if (!xx_domain_yx_homoglyph_char(x, y) && !xx_domain_yx_homoglyph_char(y, x))
        return false;
      homoglyph = true;
    } else
      return !has_a && !has_b && homoglyph;
  }
}

/* bitsquatting.rs: the first differing ASCII characters differ in one bit
 * and the rest is equal. */
static bool xx_domain_yx_bitsquatting(xx_domain_yx_chars *a, xx_domain_yx_chars *b) {
  uint32_t x, y;
  for (;;) {
    bool has_a = xx_domain_yx_next(a, &x), has_b = xx_domain_yx_next(b, &y);
    if (!has_a || !has_b)
      return false;
    if (x != y) {
      uint32_t bits;
      if (x > 0x7f || y > 0x7f)
        return false;
      bits = x ^ y;
      return bits && !(bits & (bits - 1)) && xx_domain_yx_rest_equal(a, b);
    }
  }
}

/* interleaved.rs: `b` is `a` with `c` inserted between characters. */
static bool xx_domain_yx_interleaved(xx_domain_yx_chars *a, xx_domain_yx_chars *b, uint32_t c) {
  bool first = true, found = false;
  uint32_t x, y, z;
  for (;;) {
    bool has_a = xx_domain_yx_next(a, &x), has_b = xx_domain_yx_next(b, &y);
    if (has_a && has_b) {
      if (x != y) {
        if (first || y != c || !xx_domain_yx_next(b, &z) || z != x)
          return false;
        found = true;
      }
      first = false;
    } else
      return !has_a && !has_b && found;
  }
}

typedef bool (*xx_domain_yx_engine)(xx_domain_yx_chars *, xx_domain_yx_chars *);

static bool xx_domain_yx_run(xx_domain_yx_engine engine, xx_domain_yx_chars *a, xx_domain_yx_chars *b) {
  a->at = 0;
  b->at = 0;
  return engine(a, b);
}

static bool xx_domain_yx_parts_equal(const char *a, size_t a_length, const char *b,
                              size_t b_length) {
  if (!a || !b)
    return !a && !b;
  return a_length == b_length && !memcmp(a, b, a_length);
}

XXFC_API bool xx_domain_yx_permutation_of(const char *domain, size_t domain_length,
                                 const char *target, size_t target_length,
                                 int64_t kinds) {
  xx_domain_yx_parts scanned, legit;
  xx_domain_yx_chars a = {0}, b = {0};
  bool result = false;
  if (!domain || !target || !xx_domain_yx_parse_domain(domain, domain_length, &scanned) ||
      !xx_domain_yx_parse_domain(target, target_length, &legit))
    return false;
  /* A domain is not a permutation of itself. */
  if (xx_domain_yx_parts_equal(scanned.prefix, scanned.prefix_length, legit.prefix,
                        legit.prefix_length) &&
      xx_domain_yx_parts_equal(scanned.subdomain, scanned.subdomain_length,
                        legit.subdomain, legit.subdomain_length) &&
      xx_domain_yx_parts_equal(scanned.domain, scanned.domain_length, legit.domain,
                        legit.domain_length) &&
      xx_domain_yx_parts_equal(scanned.tld, scanned.tld_length, legit.tld,
                        legit.tld_length))
    return false;
  if (!xx_domain_yx_parts_equal(scanned.tld, scanned.tld_length, legit.tld,
                         legit.tld_length) ||
      !scanned.domain || !legit.domain)
    return false;
  if (!xx_domain_yx_chars_init(&a, legit.domain, legit.domain_length) ||
      !xx_domain_yx_chars_init(&b, scanned.domain, scanned.domain_length))
    goto done;
  /* vt.Domain.Permutation: TYPO 1, HOMOGLYPH 2, HYPHENATION 4,
   * SUBDOMAIN 8, BITSQUATTING 16. */
  if ((kinds & 1) &&
      (xx_domain_yx_run(xx_domain_yx_insertion, &a, &b) ||
       xx_domain_yx_run(xx_domain_yx_omission, &a, &b) ||
       xx_domain_yx_run(xx_domain_yx_replacement, &a, &b) ||
       xx_domain_yx_run(xx_domain_yx_doubling, &a, &b) ||
       xx_domain_yx_run(xx_domain_yx_swap, &a, &b) ||
       xx_domain_yx_run(xx_domain_yx_vowel_swap, &a, &b))) {
    result = true;
    goto done;
  }
  if ((kinds & 2) && xx_domain_yx_run(xx_domain_yx_is_homoglyph, &a, &b)) {
    result = true;
    goto done;
  }
  if ((kinds & 16) && xx_domain_yx_run(xx_domain_yx_bitsquatting, &a, &b)) {
    result = true;
    goto done;
  }
  if ((kinds & 8) && legit.prefix && scanned.prefix) {
    xx_domain_yx_chars legit_prefix, scanned_prefix;
    if (!xx_domain_yx_chars_init(&legit_prefix, legit.prefix, legit.prefix_length))
      goto done;
    if (!xx_domain_yx_chars_init(&scanned_prefix, scanned.prefix,
                          scanned.prefix_length)) {
      free(legit_prefix.items);
      goto done;
    }
    result = xx_domain_yx_interleaved(&legit_prefix, &scanned_prefix, '.');
    free(legit_prefix.items);
    free(scanned_prefix.items);
    if (result)
      goto done;
  }
  if (kinds & 4) {
    a.at = 0;
    b.at = 0;
    result = xx_domain_yx_interleaved(&a, &b, '-');
  }
done:
  free(a.items);
  free(b.items);
  return result;
}
