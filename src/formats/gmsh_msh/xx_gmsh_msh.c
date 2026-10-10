/* SPDX-License-Identifier: MIT
 * Primary reference: https://gmsh.info/doc/texinfo/#MSH-file-format-version-2-_0028Legacy_0029
 * Gmsh MSH2.2 ASCII: exact section/count framing, unique node/element IDs, finite coordinates, complete supported element topology/tags and bounded physical names/data.
 * Original mesh sections exported; binary/newer versions and unknown sections unsupported. Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/gmsh_msh/xx_gmsh_msh.h"
#include "../common/xx_component_text.h"

static bool graphics_text_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool graphics_text_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(graphics_text, 33554432, )
static bool graphics_text_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[11];
    return n >= 12 && pm_read(f, 0, b, 11) && component_tag(b, "$MeshFormat", 11);
}
static bool graphics_text_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    static const uint8_t nodes_per_type[32] = {0, 2, 3, 4, 4, 8, 6, 5, 3, 6, 9, 10, 27, 18, 14, 1, 8, 20, 15, 13, 9, 10, 12, 15, 15, 21, 4, 5, 6, 20, 35, 56};
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    component_id_set nodes = {0}, elements = {0};
    uint32_t seen = 0, nodecount = 0, elementcount = 0;
    uint64_t start;
    int32_t count, i, j, tag;
    bool result = false;
    char label[48];
#define GM(x)                \
    do {                     \
        if (!(x)) goto done; \
    } while (0)
    GM(component_utf8(b, n, false, pd) && component_text_next(&q) && component_text_word(&q, "$MeshFormat") && component_text_done(&q) && component_text_next(&q) &&
       component_text_word(&q, "2.2") && component_text_word(&q, "0") && component_text_word(&q, "8") && component_text_done(&q) && component_text_next(&q) &&
       component_text_word(&q, "$EndMeshFormat") && component_text_done(&q) && component_emit(f, s, "descriptor.msh", 0, q.p, n));
    while (q.p < n) {
        GM(!xx_component_parser_stopped(pd));
        start = q.p;
        if (!component_text_next(&q)) {
            GM(q.p == n);
            break;
        }
        if (component_text_word(&q, "$PhysicalNames")) {
            GM(!(seen & 1) && !(seen & 6) && component_text_done(&q) && component_text_next(&q) && component_text_integer(&q, &count) && count >= 0 && count <= 4096 &&
               component_text_done(&q));
            seen |= 1;
            for (i = 0; i < count; ++i) {
                int32_t dim;
                GM(component_text_next(&q) && component_text_integer(&q, &dim) && dim >= 0 && dim <= 3 && component_text_integer(&q, &tag) && tag > 0 &&
                   component_text_string(&q) && component_text_done(&q));
            }
            GM(component_text_next(&q) && component_text_word(&q, "$EndPhysicalNames") && component_text_done(&q));
            GM(component_emit(f, s, "physical-names.msh", start, q.p - start, n));
        } else if (component_text_word(&q, "$Nodes")) {
            GM(!(seen & 2) && !(seen & 4) && component_text_done(&q) && component_text_next(&q) && component_text_integer(&q, &count) && count >= 3 && count <= 1000000 &&
               component_text_done(&q) && component_ids_init(&nodes, (uint32_t)count));
            seen |= 2;
            nodecount = (uint32_t)count;
            for (i = 0; i < count; ++i) {
                GM(component_text_next(&q) && component_text_integer(&q, &tag) && tag > 0 && component_id(&nodes, (uint32_t)tag, true, pd) &&
                   component_text_numbers_36_digits(&q, 3));
            }
            GM(component_text_next(&q) && component_text_word(&q, "$EndNodes") && component_text_done(&q) && component_emit(f, s, "nodes.msh", start, q.p - start, n));
        } else if (component_text_word(&q, "$Elements")) {
            GM((seen & 2) && !(seen & 4) && component_text_done(&q) && component_text_next(&q) && component_text_integer(&q, &count) && count > 0 && count <= 1000000 &&
               component_text_done(&q) && component_ids_init(&elements, (uint32_t)count));
            seen |= 4;
            elementcount = (uint32_t)count;
            for (i = 0; i < count; ++i) {
                int32_t type, tags;
                GM(component_text_next(&q) && component_text_integer(&q, &tag) && tag > 0 && component_id(&elements, (uint32_t)tag, true, pd) &&
                   component_text_integer(&q, &type) && type > 0 && type < 32 && component_text_integer(&q, &tags) && tags >= 0 && tags <= 16);
                for (j = 0; j < tags; ++j) {
                    GM(component_text_integer(&q, &tag));
                }
                for (j = 0; j < nodes_per_type[type]; ++j) GM(component_text_integer(&q, &tag) && tag > 0 && component_id(&nodes, (uint32_t)tag, false, pd));
                GM(component_text_done(&q));
            }
            GM(component_text_next(&q) && component_text_word(&q, "$EndElements") && component_text_done(&q) &&
               component_emit(f, s, "elements.msh", start, q.p - start, n));
        } else if (component_text_word(&q, "$NodeData") || component_text_word(&q, "$ElementData")) {
            bool node = component_tag(b + q.t - 9, "$NodeData", 9);
            int32_t stringtags, realtags, inttags, components = 0, entries = 0, k;
            component_id_set data_ids = {0};
            uint32_t limit = node ? nodecount : elementcount;
            const char *end = node ? "$EndNodeData" : "$EndElementData";
            GM((seen & 6) == 6 && component_text_done(&q) && component_text_next(&q) && component_text_integer(&q, &stringtags) && stringtags > 0 && stringtags <= 16 &&
               component_text_done(&q));
            for (k = 0; k < stringtags; ++k) GM(component_text_next(&q) && component_text_string(&q) && component_text_done(&q));
            GM(component_text_next(&q) && component_text_integer(&q, &realtags) && realtags >= 0 && realtags <= 16 && component_text_done(&q));
            for (k = 0; k < realtags; ++k) GM(component_text_next(&q) && component_text_numbers_36_digits(&q, 1));
            GM(component_text_next(&q) && component_text_integer(&q, &inttags) && inttags >= 3 && inttags <= 16 && component_text_done(&q));
            for (k = 0; k < inttags; ++k) {
                GM(component_text_next(&q) && component_text_integer(&q, &tag) && component_text_done(&q));
                if (k == 1) components = tag;
                if (k == 2) entries = tag;
            }
            GM(components > 0 && components <= 16 && entries >= 0 && (uint32_t)entries <= limit && component_ids_init(&data_ids, (uint32_t)entries));
            for (k = 0; k < entries; ++k) {
                if (!component_text_next(&q) || !component_text_integer(&q, &tag) || tag <= 0 || !component_id(node ? &nodes : &elements, (uint32_t)tag, false, pd) ||
                    !component_id(&data_ids, (uint32_t)tag, true, pd) || !component_text_numbers_36_digits(&q, (unsigned)components)) {
                    xx_mem_free(data_ids.values);
                    goto done;
                }
            }
            xx_mem_free(data_ids.values);
            GM(component_text_next(&q) && component_text_word(&q, end) && component_text_done(&q));
            xx_rt_snprintf(label, sizeof(label), "%s-%u.msh", node ? "node-data" : "element-data", (unsigned)s->count);
            GM(component_emit(f, s, label, start, q.p - start, n));
        } else goto done;
    }
    GM((seen & 6) == 6);
    if (!component_cover(f, s, "comments.msh", n)) goto done;
    s->size = (int64_t)n;
    result = true;
done:
    xx_mem_free(nodes.values);
    xx_mem_free(elements.values);
    return result;
#undef GM
}

void xx_gmsh_msh_init(xx_gmsh_msh *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_GMSH_MSH, "msh");
    }
}
xx_gmsh_msh *xx_gmsh_msh_create(xx_io_device *d, int64_t at)
{
    xx_gmsh_msh *r = (xx_gmsh_msh *)xx_mem_alloc(sizeof(*r));
    if (r) xx_gmsh_msh_init(r, d, at);
    return r;
}
void xx_gmsh_msh_destroy(xx_gmsh_msh *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_gmsh_msh_free(xx_gmsh_msh *r)
{
    if (r) {
        xx_gmsh_msh_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_gmsh_msh_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_gmsh_msh_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
