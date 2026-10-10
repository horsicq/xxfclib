/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/python/cpython/blob/3.13/Lib/pickletools.py */
#include "xxfclib/formats/python_pickle/xx_python_pickle.h"
#include "../common/xx_phylogenetic_text.h"
/* Kind1 hashable atom,2 list,3 dictionary,4 tuple,5 set,6 mark.
 * Memo entries preserve kinds, so an append cannot target a scalar reference. */
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    bool ok = false;
    uint64_t at = 2, frame = 0;
    uint8_t stack[4096], memo[4096];
    unsigned top = 0, memos = 0, ops = 0, proto, i;
    xx_mem_zero(memo, sizeof(memo));
    BLOB_NEED(blob_load(f, &b, pd) && b.n >= 4 && b.p[0] == 128 && b.p[1] >= 2 && b.p[1] <= 5);
    proto = b.p[1];
    while (at < b.n) {
        uint8_t op;
        uint64_t end, bytes = 0;
        uint32_t n = 0;
        unsigned mark;
        BLOB_NEED(++ops <= 262144 && blob_span(&b, at, 1));
        if (frame && at == frame) frame = 0;
        if (frame) BLOB_NEED(at < frame);
        op = b.p[(size_t)at++];
        end = frame ? frame : b.n;
        if (op == 0x95) {
            BLOB_NEED(proto >= 4 && !frame && blob_span(&b, at, 8));
            bytes = xx_data_get_u64(b.p + (size_t)at, 8, 0, false);
            at += 8;
            BLOB_NEED(bytes && record_span(at, bytes, b.n));
            frame = at + bytes;
            continue;
        }
        switch (op) {
            case '.':
                BLOB_NEED(top == 1 && stack[0] != 6 && (!frame || at == frame) && at == b.n);
                BLOB_NEED(blob_add(f, s, &b, "protocol", 0, 2) && blob_add(f, s, &b, "program", 2, b.n - 2));
                s->size = (int64_t)b.n;
                ok = true;
                goto done;
            case 'N':
            case 0x88:
            case 0x89:
                BLOB_NEED(top < 4096);
                stack[top++] = 1;
                break;
            case 'K': bytes = 1; goto atom;
            case 'M': bytes = 2; goto atom;
            case 'J': bytes = 4; goto atom;
            case 'G':
                bytes = 8;
                BLOB_NEED(record_span(at, 8, end) && blob_floats(&b, at, 8, 8, true));
                goto atom;
            case 0x8a:
                BLOB_NEED(record_span(at, 1, end));
                bytes = b.p[(size_t)at++];
                goto atom;
            case 0x8b:
                BLOB_NEED(record_span(at, 4, end));
                bytes = xx_data_get_u32(b.p + (size_t)at, 4, 0, false);
                at += 4;
                BLOB_NEED(bytes <= 4096);
                goto atom;
            case 'U':
            case 'C':
            case 0x8c: {
                BLOB_NEED((op != 'C' || proto >= 3) && (op != 0x8c || proto >= 4) && record_span(at, 1, end));
                bytes = b.p[(size_t)at++];
                if (op == 0x8c) BLOB_NEED(record_span(at, bytes, end) && bounded_utf8(b.p + (size_t)at, (size_t)bytes, pd));
                goto atom;
            }
            case 'T':
            case 'B':
            case 'X': {
                BLOB_NEED((op != 'B' || proto >= 3) && record_span(at, 4, end));
                bytes = xx_data_get_u32(b.p + (size_t)at, 4, 0, false);
                at += 4;
                if (op == 'X') BLOB_NEED(record_span(at, bytes, end) && bounded_utf8(b.p + (size_t)at, (size_t)bytes, pd));
                goto atom;
            }
            case 0x8d:
            case 0x8e:
            case 0x96: {
                BLOB_NEED(proto >= 4 && (op != 0x96 || proto >= 5) && record_span(at, 8, end));
                bytes = xx_data_get_u64(b.p + (size_t)at, 8, 0, false);
                at += 8;
                if (op == 0x8d) BLOB_NEED(record_span(at, bytes, end) && bytes <= 16777216 && bounded_utf8(b.p + (size_t)at, (size_t)bytes, pd));
                goto atom;
            }
            case ']':
            case '}':
            case ')':
            case 0x8f:
                BLOB_NEED(top < 4096 && (op != 0x8f || proto >= 4));
                stack[top++] = op == ']' ? 2 : op == '}' ? 3 : op == ')' ? 4 : 5;
                break;
            case '(':
                BLOB_NEED(top < 4096);
                stack[top++] = 6;
                break;
            case '0':
                BLOB_NEED(top && stack[top - 1] != 6);
                --top;
                break;
            case '1':
                for (mark = top; mark && stack[mark - 1] != 6; --mark) {
                }
                BLOB_NEED(mark);
                top = mark - 1;
                break;
            case '2':
                BLOB_NEED(top && top < 4096 && stack[top - 1] != 6);
                stack[top] = stack[top - 1];
                ++top;
                break;
            case 0x85:
            case 0x86:
            case 0x87: {
                uint8_t kind = 4;
                n = (uint32_t)(op - 0x84);
                BLOB_NEED(top >= n);
                for (i = top - n; i < top; ++i) {
                    BLOB_NEED(stack[i] != 6);
                    if (stack[i] != 1 && stack[i] != 4) kind = 7;
                }
                top -= n;
                stack[top++] = kind;
                break;
            }
            case 't':
            case 'l':
            case 'd':
            case 'e':
            case 'u':
            case 0x90:
            case 0x91:
                for (mark = top; mark && stack[mark - 1] != 6; --mark) {
                }
                BLOB_NEED(mark);
                n = top - mark;
                if (op == 'd' || op == 'u') {
                    BLOB_NEED(!(n & 1));
                    for (i = mark; i < top; i += 2) BLOB_NEED(stack[i] == 1 || stack[i] == 4);
                }
                if (op == 0x90 || op == 0x91) {
                    BLOB_NEED(proto >= 4);
                    for (i = mark; i < top; ++i) BLOB_NEED(stack[i] == 1 || stack[i] == 4);
                }
                if (op == 'e' || op == 'u' || op == 0x90) {
                    BLOB_NEED(mark >= 2 && stack[mark - 2] == (op == 'e' ? 2 : op == 'u' ? 3 : 5));
                    top = mark - 1;
                } else {
                    uint8_t kind = op == 't' || op == 0x91 ? 4 : op == 'l' ? 2 : 3;
                    if (op == 't')
                        for (i = mark; i < top; ++i)
                            if (stack[i] != 1 && stack[i] != 4) kind = 7;
                    top = mark - 1;
                    stack[top++] = kind;
                }
                break;
            case 'a':
                BLOB_NEED(top >= 2 && stack[top - 2] == 2 && stack[top - 1] != 6);
                --top;
                break;
            case 's':
                BLOB_NEED(top >= 3 && stack[top - 3] == 3 && (stack[top - 2] == 1 || stack[top - 2] == 4) && stack[top - 1] != 6);
                top -= 2;
                break;
            case 'q':
            case 'r':
            case 0x94:
                BLOB_NEED(top && stack[top - 1] != 6);
                if (op == 0x94) {
                    BLOB_NEED(proto >= 4);
                    n = memos;
                } else {
                    unsigned width = op == 'q' ? 1 : 4;
                    BLOB_NEED(record_span(at, width, end));
                    n = width == 1 ? b.p[(size_t)at] : xx_data_get_u32(b.p + (size_t)at, 4, 0, false);
                    at += width;
                }
                BLOB_NEED(n < 4096);
                if (!memo[n]) ++memos;
                memo[n] = stack[top - 1];
                break;
            case 'h':
            case 'j': {
                unsigned width = op == 'h' ? 1 : 4;
                BLOB_NEED(record_span(at, width, end));
                n = width == 1 ? b.p[(size_t)at] : xx_data_get_u32(b.p + (size_t)at, 4, 0, false);
                at += width;
                BLOB_NEED(n < 4096 && memo[n] && top < 4096);
                stack[top++] = memo[n];
                break;
            }
            default: goto done;
        }
        BLOB_NEED(at <= end);
        continue;
    atom:
        BLOB_NEED(bytes <= 16777216 && record_span(at, bytes, end) && blob_span(&b, at, bytes) && top < 4096);
        at += bytes;
        stack[top++] = op == 0x96 ? 7 : 1;
    }
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_python_pickle_init(xx_python_pickle *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_PYTHON_PICKLE, "python_pickle");
    }
}
xx_python_pickle *xx_python_pickle_create(xx_io_device *d, int64_t b)
{
    xx_python_pickle *r = (xx_python_pickle *)xx_mem_alloc(sizeof(*r));
    if (r) xx_python_pickle_init(r, d, b);
    return r;
}
void xx_python_pickle_destroy(xx_python_pickle *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_python_pickle_free(xx_python_pickle *r)
{
    if (r) {
        xx_python_pickle_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_python_pickle_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_python_pickle_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
