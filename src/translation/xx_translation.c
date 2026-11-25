/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/translation/xx_translation.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#define MAX_FORMS 16
#define MAX_NODES 256
#define MAX_DEPTH 64

typedef struct expression {
    int op, a, b, c, height;
    uint64_t value;
} expression;
typedef struct entry {
    char *context, *id, *plural, *forms[MAX_FORMS];
    size_t line;
    unsigned int seen;
    int fuzzy, indexed;
    struct entry *next;
} entry;
struct xx_translation {
    entry **buckets;
    size_t capacity, count;
    unsigned int nplurals;
    expression nodes[MAX_NODES];
    int node_count, root;
};
typedef struct expr_parser {
    const char *p;
    xx_translation *catalog;
    int failed, depth;
} expr_parser;
enum {
    OP_NUMBER = 256,
    OP_N,
    OP_EQ,
    OP_NE,
    OP_LE,
    OP_GE,
    OP_AND,
    OP_OR
};

static int space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}
static void skip(expr_parser *p)
{
    while (space(*p->p)) ++p->p;
}
static int node(expr_parser *p, int op, int a, int b, int c, uint64_t value)
{
    int i = p->catalog->node_count;
    if (i == MAX_NODES) {
        p->failed = 1;
        return 0;
    }
    {
        int height = a >= 0 ? p->catalog->nodes[a].height + 1 : 1;
        if (b >= 0 && p->catalog->nodes[b].height >= height) height = p->catalog->nodes[b].height + 1;
        if (c >= 0 && p->catalog->nodes[c].height >= height) height = p->catalog->nodes[c].height + 1;
        if (height > MAX_DEPTH) {
            p->failed = 1;
            return 0;
        }
        p->catalog->nodes[i].height = height;
    }
    p->catalog->nodes[i].op = op;
    p->catalog->nodes[i].a = a;
    p->catalog->nodes[i].b = b;
    p->catalog->nodes[i].c = c;
    p->catalog->nodes[i].value = value;
    ++p->catalog->node_count;
    return i;
}
static int conditional(expr_parser *p);
static int primary(expr_parser *p)
{
    int result = 0;
    skip(p);
    if (++p->depth > MAX_DEPTH) {
        p->failed = 1;
        --p->depth;
        return 0;
    }
    if (*p->p == '(') {
        ++p->p;
        result = conditional(p);
        skip(p);
        if (*p->p != ')') p->failed = 1;
        else ++p->p;
    } else if (*p->p == '!' || *p->p == '-' || *p->p == '+') {
        int op = *p->p++;
        int a = primary(p);
        result = node(p, op, a, -1, -1, 0);
    } else if (*p->p == 'n') {
        ++p->p;
        result = node(p, OP_N, -1, -1, -1, 0);
    } else if (*p->p >= '0' && *p->p <= '9') {
        uint64_t n = 0;
        do {
            unsigned int digit = (unsigned int)(*p->p++ - '0');
            if (n > (UINT64_MAX - digit) / 10) p->failed = 1;
            n = n * 10 + digit;
        } while (*p->p >= '0' && *p->p <= '9');
        result = node(p, OP_NUMBER, -1, -1, -1, n);
    } else p->failed = 1;
    --p->depth;
    return result;
}
static int operator_at(expr_parser *p, int *length, int *precedence)
{
    char a, b;
    skip(p);
    a = p->p[0];
    b = a ? p->p[1] : 0;
    *length = 2;
    if (a == '|' && b == '|') {
        *precedence = 1;
        return OP_OR;
    }
    if (a == '&' && b == '&') {
        *precedence = 2;
        return OP_AND;
    }
    if (a == '=' && b == '=') {
        *precedence = 3;
        return OP_EQ;
    }
    if (a == '!' && b == '=') {
        *precedence = 3;
        return OP_NE;
    }
    if (a == '<' && b == '=') {
        *precedence = 4;
        return OP_LE;
    }
    if (a == '>' && b == '=') {
        *precedence = 4;
        return OP_GE;
    }
    *length = 1;
    if (a == '<' || a == '>') {
        *precedence = 4;
        return a;
    }
    if (a == '+' || a == '-') {
        *precedence = 5;
        return a;
    }
    if (a == '*' || a == '/' || a == '%') {
        *precedence = 6;
        return a;
    }
    *precedence = 0;
    return 0;
}
static int binary(expr_parser *p, int minimum)
{
    int a = primary(p);
    while (!p->failed) {
        int length, precedence, op = operator_at(p, &length, &precedence), b;
        if (!op || precedence < minimum) break;
        p->p += length;
        b = binary(p, precedence + 1);
        a = node(p, op, a, b, -1, 0);
    }
    return a;
}
static int conditional(expr_parser *p)
{
    int a, b, c;
    if (++p->depth > MAX_DEPTH) {
        p->failed = 1;
        --p->depth;
        return 0;
    }
    a = binary(p, 1);
    skip(p);
    if (!p->failed && *p->p == '?') {
        ++p->p;
        b = conditional(p);
        skip(p);
        if (*p->p != ':') p->failed = 1;
        else ++p->p;
        c = p->failed ? 0 : conditional(p);
        a = node(p, '?', a, b, c, 0);
    }
    --p->depth;
    return a;
}
static uint64_t evaluate(const xx_translation *catalog, int i, uint64_t n, int depth, int *valid)
{
    const expression *e;
    uint64_t a, b;
    if (depth > MAX_DEPTH || i < 0 || i >= catalog->node_count) {
        *valid = 0;
        return 0;
    }
    e = &catalog->nodes[i];
    if (e->op == OP_NUMBER) return e->value;
    if (e->op == OP_N) return n;
    a = evaluate(catalog, e->a, n, depth + 1, valid);
    if (!*valid) return 0;
    if (e->op == '?') return evaluate(catalog, a ? e->b : e->c, n, depth + 1, valid);
    if (e->op == OP_AND && !a) return 0;
    if (e->op == OP_OR && a) return 1;
    if (e->b < 0) return e->op == '!' ? !a : e->op == '-' ? (uint64_t)0 - a : a;
    b = evaluate(catalog, e->b, n, depth + 1, valid);
    switch (e->op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/':
            if (!b) {
                *valid = 0;
                return 0;
            }
            return a / b;
        case '%':
            if (!b) {
                *valid = 0;
                return 0;
            }
            return a % b;
        case '<': return a < b;
        case '>': return a > b;
        case OP_LE: return a <= b;
        case OP_GE: return a >= b;
        case OP_EQ: return a == b;
        case OP_NE: return a != b;
        case OP_AND: return !!b;
        case OP_OR: return !!b;
        default: *valid = 0; return 0;
    }
}
static int equal(const char *a, const char *b)
{
    return a && b ? !xx_rt_strcmp(a, b) : a == b;
}
static uint64_t hash(const char *context, const char *id)
{
    uint64_t h = UINT64_C(14695981039346656037);
    const unsigned char *p;
    if (context)
        for (p = (const unsigned char *)context; *p; ++p) h = (h ^ *p) * UINT64_C(1099511628211);
    h = (h ^ (context ? 1u : 0u)) * UINT64_C(1099511628211);
    for (p = (const unsigned char *)id; *p; ++p) h = (h ^ *p) * UINT64_C(1099511628211);
    return h;
}
static entry *find(const xx_translation *catalog, const char *context, const char *id)
{
    entry *e;
    if (!catalog || !id || !catalog->capacity) return NULL;
    for (e = catalog->buckets[(size_t)hash(context, id) & (catalog->capacity - 1)]; e; e = e->next)
        if (equal(e->context, context) && equal(e->id, id)) return e;
    return NULL;
}
static void free_entry(entry *e)
{
    unsigned int i;
    xx_mem_free(e->context);
    xx_mem_free(e->id);
    xx_mem_free(e->plural);
    for (i = 0; i < MAX_FORMS; ++i) xx_mem_free(e->forms[i]);
    xx_rt_memset(e, 0, sizeof(*e));
}
void xx_translation_destroy(xx_translation *catalog)
{
    size_t i;
    if (!catalog) return;
    for (i = 0; i < catalog->capacity; ++i) {
        entry *e = catalog->buckets[i];
        while (e) {
            entry *next = e->next;
            free_entry(e);
            xx_mem_free(e);
            e = next;
        }
    }
    xx_mem_free(catalog->buckets);
    xx_mem_free(catalog);
}
static int utf8(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        unsigned int cp, min, count;
        if (*p < 0x80) {
            ++p;
            continue;
        }
        if (*p >= 0xc2 && *p <= 0xdf) {
            cp = *p++ & 31;
            min = 0x80;
            count = 1;
        } else if (*p >= 0xe0 && *p <= 0xef) {
            cp = *p++ & 15;
            min = 0x800;
            count = 2;
        } else if (*p >= 0xf0 && *p <= 0xf4) {
            cp = *p++ & 7;
            min = 0x10000;
            count = 3;
        } else return 0;
        while (count--) {
            if ((*p & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | (*p++ & 63);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    }
    return 1;
}
static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') : c;
}
static int prefix(const char *p, const char *s)
{
    while (*s)
        if (lower(*p++) != lower(*s++)) return 0;
    return 1;
}
static xx_translation_status header(xx_translation *catalog, const char *text)
{
    const char *p = text;
    while (*p) {
        if (prefix(p, "Content-Type:")) {
            const char *q = p;
            while (*q && *q != '\n') {
                if (prefix(q, "charset=")) {
                    const char *start;
                    size_t length;
                    q += 8;
                    if (*q == '"') ++q;
                    start = q;
                    while (*q && *q != '\n' && *q != '"' && *q != ';' && !space(*q)) ++q;
                    length = (size_t)(q - start);
                    if (!((length == 5 && prefix(start, "UTF-8")) || (length == 5 && prefix(start, "ASCII")) || (length == 8 && prefix(start, "US-ASCII"))))
                        return XX_TRANSLATION_UNSUPPORTED_CHARSET;
                    break;
                }
                ++q;
            }
        } else if (prefix(p, "Plural-Forms:")) {
            expr_parser parser;
            uint64_t forms = 0;
            const char *q = p + 13;
            while (space(*q)) ++q;
            if (!prefix(q, "nplurals")) return XX_TRANSLATION_INVALID_PO;
            q += 8;
            while (space(*q)) ++q;
            if (*q++ != '=') return XX_TRANSLATION_INVALID_PO;
            while (space(*q)) ++q;
            if (*q < '0' || *q > '9') return XX_TRANSLATION_INVALID_PO;
            while (*q >= '0' && *q <= '9') {
                forms = forms * 10 + (unsigned int)(*q++ - '0');
                if (forms > MAX_FORMS) return XX_TRANSLATION_INVALID_PO;
            }
            while (space(*q)) ++q;
            if (!forms || *q++ != ';') return XX_TRANSLATION_INVALID_PO;
            while (space(*q)) ++q;
            if (!prefix(q, "plural")) return XX_TRANSLATION_INVALID_PO;
            q += 6;
            while (space(*q)) ++q;
            if (*q++ != '=') return XX_TRANSLATION_INVALID_PO;
            parser.p = q;
            parser.catalog = catalog;
            parser.failed = parser.depth = 0;
            catalog->node_count = 0;
            catalog->root = conditional(&parser);
            skip(&parser);
            if (parser.failed || *parser.p++ != ';') return XX_TRANSLATION_INVALID_PO;
            skip(&parser);
            if (*parser.p && *parser.p != '\n') return XX_TRANSLATION_INVALID_PO;
            catalog->nplurals = (unsigned int)forms;
        }
        while (*p && *p != '\n') ++p;
        if (*p) ++p;
    }
    return XX_TRANSLATION_OK;
}
static xx_translation_status publish(xx_translation *catalog, entry *current)
{
    size_t slot;
    unsigned int i;
    entry *e;
    if (!current->id) {
        if (current->context) return XX_TRANSLATION_INVALID_PO;
        current->fuzzy = 0;
        return XX_TRANSLATION_OK;
    }
    if (!current->seen || (current->plural ? !current->indexed : current->indexed)) return XX_TRANSLATION_INVALID_PO;
    if (!utf8(current->id) || (current->context && !utf8(current->context)) || (current->plural && !utf8(current->plural))) return XX_TRANSLATION_INVALID_PO;
    for (i = 0; i < MAX_FORMS; ++i)
        if (current->forms[i] && !utf8(current->forms[i])) return XX_TRANSLATION_INVALID_PO;
    if (find(catalog, current->context, current->id)) return XX_TRANSLATION_INVALID_PO;
    if (!current->context && !current->id[0]) {
        xx_translation_status status;
        if (current->plural) return XX_TRANSLATION_INVALID_PO;
        status = header(catalog, current->forms[0]);
        if (status != XX_TRANSLATION_OK) return status;
    }
    if (catalog->count >= catalog->capacity) {
        size_t capacity = catalog->capacity ? catalog->capacity * 2 : 64, j;
        entry **buckets;
        if (capacity < catalog->capacity || capacity > SIZE_MAX / sizeof(*buckets)) return XX_TRANSLATION_OUT_OF_MEMORY;
        buckets = (entry **)xx_mem_calloc(capacity, sizeof(*buckets));
        if (!buckets) return XX_TRANSLATION_OUT_OF_MEMORY;
        for (j = 0; j < catalog->capacity; ++j) {
            e = catalog->buckets[j];
            while (e) {
                entry *next = e->next;
                slot = (size_t)hash(e->context, e->id) & (capacity - 1);
                e->next = buckets[slot];
                buckets[slot] = e;
                e = next;
            }
        }
        xx_mem_free(catalog->buckets);
        catalog->buckets = buckets;
        catalog->capacity = capacity;
    }
    e = (entry *)xx_mem_alloc(sizeof(*e));
    if (!e) return XX_TRANSLATION_OUT_OF_MEMORY;
    *e = *current;
    slot = (size_t)hash(e->context, e->id) & (catalog->capacity - 1);
    e->next = catalog->buckets[slot];
    catalog->buckets[slot] = e;
    ++catalog->count;
    xx_rt_memset(current, 0, sizeof(*current));
    return XX_TRANSLATION_OK;
}
static int hex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
/* Append one quoted physical line in place. Capacity is grown geometrically
 * across continuations, so large wrapped PO messages remain linear to parse. */
static xx_translation_status quoted(char **field, size_t *length, size_t *capacity, const char *p, const char *end)
{
    size_t needed, at = *length;
    char *buffer;
    if (p == end || *p++ != '"') return XX_TRANSLATION_INVALID_PO;
    if ((size_t)(end - p) > SIZE_MAX - at - 1) return XX_TRANSLATION_OUT_OF_MEMORY;
    needed = at + (size_t)(end - p) + 1;
    if (needed > *capacity) {
        size_t grown = *capacity <= SIZE_MAX / 2 ? *capacity * 2 : needed;
        if (grown < needed) grown = needed;
        buffer = (char *)xx_mem_realloc(*field, grown);
        if (!buffer) return XX_TRANSLATION_OUT_OF_MEMORY;
        *field = buffer;
        *capacity = grown;
    }
    buffer = *field;
    while (p < end && *p != '"') {
        unsigned int c = (unsigned char)*p++;
        if (c == '\\') {
            if (p == end) return XX_TRANSLATION_INVALID_PO;
            c = (unsigned char)*p++;
            switch (c) {
                case 'a': c = 7; break;
                case 'b': c = 8; break;
                case 'f': c = 12; break;
                case 'n': c = 10; break;
                case 'r': c = 13; break;
                case 't': c = 9; break;
                case 'v': c = 11; break;
                case '\\':
                case '"':
                case '?':
                case '\'': break;
                case 'x': {
                    unsigned int digits = 0;
                    c = 0;
                    while (p < end && hex(*p) >= 0) {
                        if (c > 15) return XX_TRANSLATION_INVALID_PO;
                        c = c * 16 + (unsigned int)hex(*p++);
                        ++digits;
                    }
                    if (!digits) return XX_TRANSLATION_INVALID_PO;
                    break;
                }
                default:
                    if (c >= '0' && c <= '7') {
                        int digits = 1;
                        c -= '0';
                        while (digits < 3 && p < end && *p >= '0' && *p <= '7') {
                            c = c * 8 + (unsigned int)(*p++ - '0');
                            ++digits;
                        }
                    } else return XX_TRANSLATION_INVALID_PO;
            }
        }
        if (!c || c > 255) return XX_TRANSLATION_INVALID_PO;
        buffer[at++] = (char)c;
    }
    if (p == end || *p++ != '"') return XX_TRANSLATION_INVALID_PO;
    while (p < end && space(*p)) ++p;
    if (p != end) return XX_TRANSLATION_INVALID_PO;
    buffer[at] = 0;
    *length = at;
    return XX_TRANSLATION_OK;
}
static int keyword(const char **p, const char *end, const char *word)
{
    const char *q = *p;
    while (*word) {
        if (q == end || *q++ != *word++) return 0;
    }
    if (q < end && !space(*q) && *q != '[') return 0;
    *p = q;
    return 1;
}
xx_translation_status xx_translation_load_po(const void *data, size_t size, xx_translation **out, size_t *error_line)
{
    xx_translation *catalog;
    entry current = {0};
    const char *p = (const char *)data, *end;
    char **field = NULL;
    size_t line = 1, length = 0, capacity = 0, i;
    xx_translation_status status = XX_TRANSLATION_OK;
    if (error_line) *error_line = 0;
    if (!out) return XX_TRANSLATION_INVALID_ARGUMENT;
    *out = NULL;
    if ((!data && size) || size > PTRDIFF_MAX) return XX_TRANSLATION_INVALID_ARGUMENT;
    if (!p) p = "";
    end = p + size;
    catalog = (xx_translation *)xx_mem_calloc(1, sizeof(*catalog));
    if (!catalog) return XX_TRANSLATION_OUT_OF_MEMORY;
    catalog->nplurals = 2;
    catalog->root = -1; /* Default English rule: n != 1. */
    if (size >= 3 && (unsigned char)p[0] == 0xef && (unsigned char)p[1] == 0xbb && (unsigned char)p[2] == 0xbf) p += 3;
    while (p < end) {
        const char *start = p, *stop, *q;
        while (p < end && *p != '\n') {
            if (!*p) {
                status = XX_TRANSLATION_INVALID_PO;
                goto failed;
            }
            ++p;
        }
        stop = p;
        if (p < end) ++p;
        while (start < stop && space(*start)) ++start;
        while (stop > start && space(stop[-1])) --stop;
        if (start == stop || *start == '#') {
            if (current.seen || start == stop) {
                status = publish(catalog, &current);
                if (status != XX_TRANSLATION_OK) {
                    line = current.line ? current.line : line;
                    goto failed;
                }
                field = NULL;
            }
            if (stop - start >= 2 && start[0] == '#' && start[1] == ',') {
                q = start + 2;
                while (q < stop) {
                    const char *flag;
                    while (q < stop && (space(*q) || *q == ',')) ++q;
                    flag = q;
                    while (q < stop && !space(*q) && *q != ',') ++q;
                    if (q - flag == 5 && flag[0] == 'f' && flag[1] == 'u' && flag[2] == 'z' && flag[3] == 'z' && flag[4] == 'y') current.fuzzy = 1;
                }
            }
        } else if (*start == '"') {
            if (!field) {
                status = XX_TRANSLATION_INVALID_PO;
                goto failed;
            }
            status = quoted(field, &length, &capacity, start, stop);
            if (status != XX_TRANSLATION_OK) goto failed;
        } else {
            enum {
                CONTEXT,
                ID,
                PLURAL,
                FORM
            } kind;
            unsigned int index = 0;
            int indexed = 0;
            q = start;
            if (keyword(&q, stop, "msgctxt")) kind = CONTEXT;
            else if (keyword(&q, stop, "msgid_plural")) kind = PLURAL;
            else if (keyword(&q, stop, "msgid")) kind = ID;
            else if (keyword(&q, stop, "msgstr")) {
                kind = FORM;
                if (q < stop && *q == '[') {
                    indexed = 1;
                    ++q;
                    if (q == stop || *q < '0' || *q > '9') {
                        status = XX_TRANSLATION_INVALID_PO;
                        goto failed;
                    }
                    while (q < stop && *q >= '0' && *q <= '9') {
                        index = index * 10 + (unsigned int)(*q++ - '0');
                        if (index >= MAX_FORMS) {
                            status = XX_TRANSLATION_INVALID_PO;
                            goto failed;
                        }
                    }
                    if (q == stop || *q++ != ']') {
                        status = XX_TRANSLATION_INVALID_PO;
                        goto failed;
                    }
                }
            } else {
                status = XX_TRANSLATION_INVALID_PO;
                goto failed;
            }
            if ((kind == CONTEXT || kind == ID) && current.seen) {
                status = publish(catalog, &current);
                if (status != XX_TRANSLATION_OK) {
                    line = current.line ? current.line : line;
                    goto failed;
                }
            }
            if (kind == CONTEXT) {
                if (current.context || current.id) {
                    status = XX_TRANSLATION_INVALID_PO;
                    goto failed;
                }
                field = &current.context;
                current.line = line;
            } else if (kind == ID) {
                if (current.id) {
                    status = XX_TRANSLATION_INVALID_PO;
                    goto failed;
                }
                field = &current.id;
                if (!current.context) current.line = line;
            } else if (kind == PLURAL) {
                if (!current.id || current.plural || current.seen) {
                    status = XX_TRANSLATION_INVALID_PO;
                    goto failed;
                }
                field = &current.plural;
            } else {
                if (!current.id || current.forms[index] || indexed != (current.plural != NULL)) {
                    status = XX_TRANSLATION_INVALID_PO;
                    goto failed;
                }
                current.seen |= 1u << index;
                current.indexed = indexed;
                field = &current.forms[index];
            }
            length = capacity = 0;
            while (q < stop && space(*q)) ++q;
            status = quoted(field, &length, &capacity, q, stop);
            if (status != XX_TRANSLATION_OK) goto failed;
        }
        ++line;
    }
    status = publish(catalog, &current);
    if (status != XX_TRANSLATION_OK) {
        line = current.line ? current.line : line;
        goto failed;
    }
    /* Validate plural shape after all entries, including headers placed last. */
    for (i = 0; i < catalog->capacity; ++i) {
        entry *e;
        for (e = catalog->buckets[i]; e; e = e->next)
            if (e->plural && e->seen != (1u << catalog->nplurals) - 1u) {
                line = e->line;
                status = XX_TRANSLATION_INVALID_PO;
                goto failed;
            }
    }
    *out = catalog;
    return XX_TRANSLATION_OK;
failed:
    if (error_line) *error_line = line;
    free_entry(&current);
    xx_translation_destroy(catalog);
    return status;
}
const char *xx_translation_pgettext(const xx_translation *catalog, const char *context, const char *msgid)
{
    entry *e;
    if (!msgid) return "";
    e = find(catalog, context, msgid);
    /* The metadata header is never a user-visible message. */
    return e && (msgid[0] || context) && !e->fuzzy && e->forms[0] && e->forms[0][0] ? e->forms[0] : msgid;
}
const char *xx_translation_gettext(const xx_translation *catalog, const char *msgid)
{
    return xx_translation_pgettext(catalog, NULL, msgid);
}
const char *xx_translation_npgettext(const xx_translation *catalog, const char *context, const char *singular, const char *plural, uint64_t n)
{
    entry *e;
    uint64_t index = n != 1;
    int valid = 1;
    const char *fallback = n == 1 ? singular : plural;
    if (!singular) return fallback ? fallback : "";
    e = find(catalog, context, singular);
    if (catalog && catalog->root >= 0) index = evaluate(catalog, catalog->root, n, 0, &valid);
    if (e && e->plural && !e->fuzzy && valid && index < catalog->nplurals && e->forms[index] && e->forms[index][0]) return e->forms[index];
    return fallback ? fallback : "";
}
const char *xx_translation_ngettext(const xx_translation *catalog, const char *singular, const char *plural, uint64_t n)
{
    return xx_translation_npgettext(catalog, NULL, singular, plural, n);
}
