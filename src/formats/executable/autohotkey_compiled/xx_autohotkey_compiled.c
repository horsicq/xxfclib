/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * AutoHotkey compiled scripts (Ahk2Exe, AutoHotkey 1.1 and 2). The
 * interpreter stub keeps the script and every file the script FileInstall's
 * as RCDATA (type 10) resources, measured on the EnglishizeCmd 2.0 tool
 * (Ahk2Exe 1.1.32):
 *
 *   RCDATA ">AUTOHOTKEY SCRIPT<"   the script text, "; <COMPILER: v...>" first
 *   RCDATA "C:\PATH\TO\FILE.EXT"   one per FileInstall'd file, named by the
 *                                  upper-cased source path, stored as is
 *   RCDATA ">AHK WITH ICON<"       the script of an AutoHotkey_H build
 *   RCDATA 1                       the script in the AutoHotkey 2 layout (the
 *                                  numbered resource FindResource calls "#1")
 *
 * The file resources sit two levels deep (type, name, data) rather than
 * three, which the shared resource-archive helper does not walk, so the
 * directory is walked with the resource-walk helper. Members: the script
 * as script.ahk and each file under its own file name; numbered RCDATA
 * resources are the stub's, except number 1 when it starts with the
 * compiler line. The name alone is not taken as proof: a tagged resource
 * must hold text. Nothing is executed.
 */
#include "xxfclib/formats/executable/autohotkey_compiled/xx_autohotkey_compiled.h"
#include "../../common/xx_executable_carrier.h"
#include "../../common/xx_pe_resource_walk.h"

#define AH_RCDATA 10U
#define AH_MAX_FILES 512U

static const char ah_script_tag[] = ">AUTOHOTKEY SCRIPT<";
static const char ah_icon_tag[] = ">AHK WITH ICON<";
static const char ah_compiler_tag[] = "; <COMPILER:";

/* Is this RCDATA resource the script? A NULL name is the numbered resource
 * `number`. */
static bool ah_is_script(Abstractformat *f, const char *name, uint32_t number, int64_t at, uint32_t bytes)
{
    uint8_t head[12];
    size_t i, length = name ? xx_rt_strlen(name) : 0;
    if (bytes < sizeof(head) || !pm_read(f, at, head, sizeof(head))) return false;
    if (!name) return number == 1U && xx_rt_memcmp(head, ah_compiler_tag, sizeof(head)) == 0;
    if ((length == sizeof(ah_script_tag) - 1U && xx_rt_memcmp(name, ah_script_tag, length) == 0) ||
        (length == sizeof(ah_icon_tag) - 1U && xx_rt_memcmp(name, ah_icon_tag, length) == 0)) {
        for (i = 0; i < sizeof(head); ++i) {
            if (head[i] != 0x09U && head[i] != 0x0AU && head[i] != 0x0DU && (head[i] < 0x20U || head[i] > 0x7EU)) return false;
        }
        return true;
    }
    return length == 2 && name[0] == '#' && name[1] == '1' && xx_rt_memcmp(head, ah_compiler_tag, sizeof(head)) == 0;
}

static bool ah_visit(Abstractformat *f, pm_stream *s, const rw_leaf_info *leaf, void *context, xx_pd_struct *pd)
{
    bool *script = (bool *)context;
    const char *label;
    size_t length, k;
    (void)pd;
    if (leaf->type_name || leaf->type_id != AH_RCDATA) return true;
    if (ah_is_script(f, leaf->name, leaf->name_id, leaf->at, leaf->bytes)) {
        label = "script.ahk";
        *script = true;
    } else if (!leaf->name) {
        return true; /* numbered RCDATA: the stub's own */
    } else {
        /* A FileInstall'd file is named by its source path: keep the file name. */
        bool path = false;
        length = xx_rt_strlen(leaf->name);
        label = leaf->name;
        for (k = 0; k < length; ++k) {
            if (leaf->name[k] == '\\' || leaf->name[k] == '/') {
                label = leaf->name + k + 1;
                path = true;
            } else if (leaf->name[k] == '.') {
                path = true;
            }
        }
        if (!path || !*label) return true;
    }
    return s->count < AH_MAX_FILES && executable_carrier_component(f, s, leaf->at, (int64_t)leaf->bytes, label);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    rw_walk w;
    bool script = false;
    if (!rw_open(f, &w, pd) || !rw_walk_leaves(f, s, &w, ah_visit, &script, pd)) return false;
    if (s->size < w.image_end) s->size = w.image_end;
    return script && !carrier_stop(pd) && carrier_members(s, pd);
}

void xx_autohotkey_compiled_init(xx_autohotkey_compiled *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_AUTOHOTKEY_COMPILED, "exe");
    }
}
xx_autohotkey_compiled *xx_autohotkey_compiled_create(xx_io_device *d, int64_t b)
{
    xx_autohotkey_compiled *r = (xx_autohotkey_compiled *)xx_mem_alloc(sizeof(*r));
    if (r) xx_autohotkey_compiled_init(r, d, b);
    return r;
}
void xx_autohotkey_compiled_destroy(xx_autohotkey_compiled *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_autohotkey_compiled_free(xx_autohotkey_compiled *r)
{
    if (r) {
        xx_autohotkey_compiled_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_autohotkey_compiled_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_autohotkey_compiled_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
