/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/assimp/assimp/master/code/AssetLib/LWS/LWSLoader.cpp
 * LightWave LWSC2 legacy scene subset: complete typed scene/render/view settings, object/light/camera motion channels with counted ordered finite keys and pre/post
 * behaviors. Original descriptor/item motion/settings records exported; known built-in empty DistantLight/Perspective plugin blocks accepted; geometry filenames remain
 * metadata, no sidecars loaded; unknown plugins/commands declined. Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/lightwave_scene/xx_lightwave_scene.h"
#include "../common/xx_component_text.h"

static bool scene_bitmap_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool scene_bitmap_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(scene_bitmap, 33554432, if (ok) s->size = available;)
static bool scene_bitmap_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[4];
    return n >= 8 && pm_read(f, 0, b, 4) && component_tag(b, "LWSC", 4);
}
typedef struct scene_bitmap_lprop {
    const char *name;
    unsigned count, kind;
} scene_bitmap_lprop;
static const scene_bitmap_lprop scene_bitmap_lprops[] = {{"MeshBackgroundGroup", 1, 1},
                                                         {"RenderRangeType", 1, 1},
                                                         {"FirstFrame", 1, 1},
                                                         {"LastFrame", 1, 1},
                                                         {"FrameStep", 1, 2},
                                                         {"RenderRangeObject", 1, 1},
                                                         {"PreviewFirstFrame", 1, 1},
                                                         {"PreviewLastFrame", 1, 1},
                                                         {"PreviewFrameStep", 1, 2},
                                                         {"FramesPerSecond", 1, 3},
                                                         {"ShowObject", 5, 4},
                                                         {"ShowLight", 5, 4},
                                                         {"ShowCamera", 5, 4},
                                                         {"Group", 1, 1},
                                                         {"PathAlignLookAhead", 1, 0},
                                                         {"PathAlignMaxLookSteps", 1, 2},
                                                         {"PathAlignReliableDist", 1, 0},
                                                         {"IKInitialState", 1, 1},
                                                         {"SubPatchLevel", 2, 1},
                                                         {"AmbientColor", 3, 5},
                                                         {"AmbIntensity", 1, 0},
                                                         {"DoubleSidedAreaLights", 1, 6},
                                                         {"LightColor", 3, 5},
                                                         {"LgtIntensity", 1, 0},
                                                         {"LightType", 1, 1},
                                                         {"LensFlare", 1, 6},
                                                         {"FlareIntensity", 1, 0},
                                                         {"FlareDissolve", 1, 0},
                                                         {"LensFlareFade", 1, 1},
                                                         {"LensFlareOptions", 1, 1},
                                                         {"FlareRingColor", 3, 5},
                                                         {"FlareRingSize", 1, 0},
                                                         {"FlareRandStreakInt", 1, 0},
                                                         {"FlareRandStreakDens", 1, 0},
                                                         {"FlareRandStreakSharp", 1, 0},
                                                         {"ShadowType", 1, 1},
                                                         {"ShadowColor", 3, 5},
                                                         {"ZoomFactor", 1, 3},
                                                         {"ZoomType", 1, 1},
                                                         {"Resolution", 1, 1},
                                                         {"CustomSize", 2, 2},
                                                         {"PixelAspectRatio", 1, 0},
                                                         {"CustomPixelRatio", 1, 3},
                                                         {"MaskPosition", 4, 1},
                                                         {"MotionBlur", 1, 6},
                                                         {"MotionBlurPasses", 1, 2},
                                                         {"ShutterEfficiency", 1, 0},
                                                         {"Oversampling", 1, 0},
                                                         {"FieldRendering", 1, 6},
                                                         {"ApertureHeight", 1, 3},
                                                         {"DepthOfField", 1, 6},
                                                         {"FocalDistance", 1, 0},
                                                         {"LensFStop", 1, 3},
                                                         {"DiaphragmSides", 1, 1},
                                                         {"DiaphragmRotation", 1, 0},
                                                         {"AASamples", 1, 2},
                                                         {"Sampler", 1, 1},
                                                         {"SegmentMemory", 1, 2},
                                                         {"Antialiasing", 1, 1},
                                                         {"AntiAliasingLevel", 1, 1},
                                                         {"ReconstructionFilter", 1, 1},
                                                         {"AdaptiveSampling", 1, 6},
                                                         {"SolidBackdrop", 1, 6},
                                                         {"BackdropColor", 3, 5},
                                                         {"ZenithColor", 3, 5},
                                                         {"SkyColor", 3, 5},
                                                         {"GroundColor", 3, 5},
                                                         {"NadirColor", 3, 5},
                                                         {"FogType", 1, 1},
                                                         {"FogMinDistance", 1, 0},
                                                         {"FogMaxDistance", 1, 0},
                                                         {"FogMinAmount", 1, 0},
                                                         {"FogMaxAmount", 1, 0},
                                                         {"FogColor", 3, 5},
                                                         {"BackdropFog", 1, 6},
                                                         {"VolumeClipDiscance", 1, 0},
                                                         {"DynamicRangeMin", 1, 0},
                                                         {"DynamicRangeLimit", 1, 0},
                                                         {"DitherIntensity", 1, 0},
                                                         {"AnimatedDither", 1, 6},
                                                         {"RenderMode", 1, 1},
                                                         {"RayTraceEffects", 1, 1},
                                                         {"DepthBufferAA", 1, 6},
                                                         {"RenderLines", 1, 6},
                                                         {"RayRecursionLimit", 1, 2},
                                                         {"RayPrecision", 1, 2},
                                                         {"RayCutoff", 1, 0},
                                                         {"SaveRGB", 1, 6},
                                                         {"SaveAlpha", 1, 6},
                                                         {"ViewConfiguration", 1, 1},
                                                         {"DefineView", 1, 1},
                                                         {"ViewType", 1, 1},
                                                         {"ViewLevel", 1, 1},
                                                         {"ViewAimpoint", 3, 0},
                                                         {"ViewDirection", 3, 0},
                                                         {"ViewZoomFactor", 1, 3},
                                                         {"ViewXRay", 1, 6},
                                                         {"ViewMBDofPreview", 1, 6},
                                                         {"ViewHeadlight", 1, 6},
                                                         {"GridNumber", 1, 2},
                                                         {"GridSize", 1, 3},
                                                         {"CameraViewBG", 1, 1},
                                                         {"ShowMotionPath", 1, 6},
                                                         {"ShowFogRadius", 1, 6},
                                                         {"ShowFogEffect", 1, 6},
                                                         {"ShowFieldChart", 1, 6},
                                                         {"OverlayColor_fv", 3, 0}};
static bool scene_bitmap_lmotion(component_text_cursor *q, unsigned count, xx_pd_struct *pd)
{
    int32_t channels, keys, interp;
    unsigned channel, k;
    double v, time, last, tension, bias, continuity;
    if (!component_text_next_poison_overflow(q) || !component_text_integer_delimited(q, &channels) || channels != (int32_t)count || !component_text_done(q)) return false;
    for (channel = 0; channel < count; ++channel) {
        last = -1;
        if (!component_text_next_poison_overflow(q) || !component_text_integer_delimited(q, &keys) || keys < 1 || keys > 4096 || !component_text_done(q)) return false;
        for (k = 0; k < (unsigned)keys; ++k) {
            if (xx_component_parser_stopped(pd) || !component_text_next_poison_overflow(q) || !component_text_number_36_digits(q, &v) ||
                !component_text_number_36_digits(q, &time) || time < 0 || time <= last || !component_text_integer_delimited(q, &interp) || interp < 0 || interp > 5 ||
                !component_text_number_36_digits(q, &tension) || tension < -1 || tension > 1 || !component_text_number_36_digits(q, &bias) || bias < -1 || bias > 1 ||
                !component_text_number_36_digits(q, &continuity) || continuity < -1 || continuity > 1 || !component_text_done(q))
                return false;
            last = time;
        }
    }
    if (!component_text_next_poison_overflow(q) || !component_text_word(q, "Pre/PostBehavior")) return false;
    for (k = 0; k < count * 2; ++k)
        if (!component_text_integer_delimited(q, &interp) || interp < 0 || interp > 5) return false;
    return component_text_done(q);
}
static bool scene_bitmap_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    unsigned kind = 0, items = 0, records = 0, required = 0;
    bool motion = false, plugin = false;
    int32_t v;
    char label[48];
    if (!component_utf8(b, n, true, pd) || !component_text_next_poison_overflow(&q) || !component_text_word(&q, "LWSC") || !component_text_done(&q) ||
        !component_text_next_poison_overflow(&q) || !component_text_integer_delimited(&q, &v) || v != 2 || !component_text_done(&q) ||
        !component_emit(f, s, "descriptor.lws", 0, q.p, n))
        return false;
    while (component_text_next_poison_overflow(&q)) {
        uint64_t start = q.start;
        component_text_cursor saved = q;
        unsigned i, j;
        double value;
        if (xx_component_parser_stopped(pd) || records >= 4000) return false;
        if (component_text_word(&q, "LoadObject")) {
            if (plugin || (items && !motion)) return false;
            component_text_space(&q);
            if (q.t == q.stop || q.stop - q.t > 255) return false;
            for (i = 0; q.t < q.stop; ++q.t, ++i)
                if (b[q.t] < 33 || b[q.t] > 126) return false;
            kind = 1;
            motion = false;
            ++items;
        } else if (component_text_word(&q, "AddLight") || component_text_word(&q, "AddCamera")) {
            if (plugin || (items && !motion) || !component_text_done(&q)) return false;
            kind = component_tag(b + start, "AddLight", 8) ? 2 : 3;
            motion = false;
            ++items;
        } else if (component_text_word(&q, "ObjectMotion") || component_text_word(&q, "LightMotion") || component_text_word(&q, "CameraMotion")) {
            unsigned expected = component_tag(b + start, "ObjectMotion", 12) ? 1 : component_tag(b + start, "LightMotion", 11) ? 2 : 3;
            if (plugin || kind != expected || motion || !component_text_word(&q, "(unnamed)") || !component_text_done(&q) ||
                !scene_bitmap_lmotion(&q, kind == 3 ? 6 : 9, pd))
                return false;
            motion = true;
        } else if (component_text_word(&q, "Plugin")) {
            if (plugin || !motion ||
                !((kind == 2 && component_text_word(&q, "LightHandler") && component_text_integer_delimited(&q, &v) && v == 1 &&
                   component_text_word(&q, "DistantLight")) ||
                  (kind == 3 && component_text_word(&q, "CameraHandler") && component_text_integer_delimited(&q, &v) && v == 1 &&
                   component_text_word(&q, "Perspective"))) ||
                !component_text_done(&q))
                return false;
            plugin = true;
        } else if (component_text_word(&q, "EndPlugin")) {
            if (!plugin || !component_text_done(&q)) return false;
            plugin = false;
        } else if (plugin) return false;
        else if (component_text_word(&q, "LightName") || component_text_word(&q, "CameraName")) {
            unsigned expected = component_tag(b + start, "LightName", 9) ? 2 : 3;
            component_text_space(&q);
            if (kind != expected || q.t == q.stop || q.stop - q.t > 255) return false;
        } else if (component_text_word(&q, "DataOverlayLabel")) {
            component_text_space(&q);
            if (q.stop - q.t > 255) return false;
        } else if (component_text_word(&q, "RenderRangeArbitrary")) {
            int32_t end;
            uint64_t dash, stop = q.stop;
            component_text_space(&q);
            dash = q.t;
            while (dash < stop && b[dash] >= '0' && b[dash] <= '9') ++dash;
            if (dash == q.t || dash == stop || b[dash] != '-') return false;
            q.stop = dash;
            if (!component_text_integer_delimited(&q, &v) || q.t != dash) return false;
            q.stop = stop;
            q.t = dash + 1;
            if (!component_text_integer_delimited(&q, &end) || v > end || !component_text_done(&q)) return false;
        } else {
            q = saved;
            for (i = 0; i < sizeof(scene_bitmap_lprops) / sizeof(scene_bitmap_lprops[0]); ++i) {
                q = saved;
                if (component_text_word(&q, scene_bitmap_lprops[i].name)) break;
            }
            if (i == sizeof(scene_bitmap_lprops) / sizeof(scene_bitmap_lprops[0])) return false;
            for (j = 0; j < scene_bitmap_lprops[i].count; ++j) {
                unsigned type = scene_bitmap_lprops[i].kind;
                if (type == 1 || type == 2 || type == 5 || type == 6 || (type == 4 && j < 2)) {
                    if (!component_text_integer_delimited(&q, &v) || (type == 2 && v < 1) || (type == 5 && (v < 0 || v > 255)) || (type == 6 && (v < 0 || v > 1)))
                        return false;
                } else if (!component_text_number_36_digits(&q, &value) || (type == 3 && value <= 0) || (type == 4 && (value < 0 || value > 1))) return false;
            }
            if (!component_text_done(&q)) return false;
            if (!xx_rt_strcmp(scene_bitmap_lprops[i].name, "FirstFrame")) required |= 1;
            if (!xx_rt_strcmp(scene_bitmap_lprops[i].name, "LastFrame")) required |= 2;
            if (!xx_rt_strcmp(scene_bitmap_lprops[i].name, "FramesPerSecond")) required |= 4;
        }
        xx_rt_snprintf(label, sizeof(label), "scene-record-%u.lws", records++);
        if (!component_emit(f, s, label, start, q.p - start, n)) return false;
    }
    return q.p == n && !plugin && items && motion && required == 7 && component_cover(f, s, "whitespace.lws", n);
}

void xx_lightwave_scene_init(xx_lightwave_scene *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_LIGHTWAVE_SCENE, "lws");
    }
}
xx_lightwave_scene *xx_lightwave_scene_create(xx_io_device *d, int64_t at)
{
    xx_lightwave_scene *r = (xx_lightwave_scene *)xx_mem_alloc(sizeof(*r));
    if (r) xx_lightwave_scene_init(r, d, at);
    return r;
}
void xx_lightwave_scene_destroy(xx_lightwave_scene *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_lightwave_scene_free(xx_lightwave_scene *r)
{
    if (r) {
        xx_lightwave_scene_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_lightwave_scene_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_lightwave_scene_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
