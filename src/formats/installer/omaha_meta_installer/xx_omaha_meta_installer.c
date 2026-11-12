/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Google Update (Omaha) meta-installer: the small stub that installs Google
 * Update and hands it the product's installer, measured on the Picasa
 * 3.9.136 meta-installer (2013). The payload lives in named resources:
 *
 *   type "EXE", name PICASA2_ADMIN   the product installer (here an NSIS
 *                                    setup of 14.8 MB), stored as is
 *   type "XML", name PAYLOAD         the Omaha payload manifest (<payload>)
 *   type "GOOGLEUPDATE", id 1        a 4-byte tag that marks the layout
 *
 * The "EXE" resource lies at an unaligned file offset, which is why the
 * installer inside is missed by signature scans. Members: every "EXE"
 * resource that holds an image (<name>.exe) and every "XML" one (<name>.xml);
 * the "GOOGLEUPDATE" tag is required. Nothing is executed.
 */
#include "xxfclib/formats/installer/omaha_meta_installer/xx_omaha_meta_installer.h"
#include "../../common/xx_executable_carrier.h"
#include "../../common/xx_pe_resource_walk.h"
#include <stdio.h>

#define OM_MAX_FILES 64U

typedef struct om_state {
    bool image, tag;
} om_state;

static bool om_type(const rw_leaf_info *leaf, const char *type)
{
    return leaf->type_name && xx_rt_strlen(leaf->type_name) == xx_rt_strlen(type) && xx_rt_memcmp(leaf->type_name, type, xx_rt_strlen(type)) == 0;
}

static bool om_visit(Abstractformat *f, pm_stream *s, const rw_leaf_info *leaf, void *context, xx_pd_struct *pd)
{
    om_state *state = (om_state *)context;
    char label[RW_MAX_NAME + 8];
    uint8_t head[2];
    (void)pd;
    if (om_type(leaf, "GOOGLEUPDATE")) {
        state->tag = true;
        return true;
    }
    if (!leaf->name) return true;
    if (om_type(leaf, "EXE")) {
        if (leaf->bytes < 64 || !pm_read(f, leaf->at, head, sizeof(head)) || head[0] != 'M' || head[1] != 'Z') return true;
        snprintf(label, sizeof(label), "%s.exe", leaf->name);
        state->image = true;
    } else if (om_type(leaf, "XML")) {
        snprintf(label, sizeof(label), "%s.xml", leaf->name);
    } else {
        return true;
    }
    return s->count < OM_MAX_FILES && executable_carrier_component(f, s, leaf->at, (int64_t)leaf->bytes, label);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    rw_walk w;
    om_state state = {false, false};
    if (!rw_open(f, &w, pd) || !rw_walk_leaves(f, s, &w, om_visit, &state, pd)) return false;
    if (s->size < w.image_end) s->size = w.image_end;
    return state.image && state.tag && !carrier_stop(pd) && carrier_members(s, pd);
}

void xx_omaha_meta_installer_init(xx_omaha_meta_installer *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_OMAHA_META_INSTALLER, "exe");
    }
}
xx_omaha_meta_installer *xx_omaha_meta_installer_create(xx_io_device *d, int64_t b)
{
    xx_omaha_meta_installer *r = (xx_omaha_meta_installer *)xx_mem_alloc(sizeof(*r));
    if (r) xx_omaha_meta_installer_init(r, d, b);
    return r;
}
void xx_omaha_meta_installer_destroy(xx_omaha_meta_installer *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_omaha_meta_installer_free(xx_omaha_meta_installer *r)
{
    if (r) {
        xx_omaha_meta_installer_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_omaha_meta_installer_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_omaha_meta_installer_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
