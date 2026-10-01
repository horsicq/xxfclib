/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent SoundFont 2 RIFF/sfbk reader. Layout reference:
 * https://github.com/FluidSynth/fluidsynth/blob/master/src/sfloader/fluid_sffile.c
 *
 * Each shdr record identifies a range in sdta/smpl, measured in 16-bit sample
 * points. The original PCM16LE bytes and optional sm24 low-byte plane are
 * exported without synthesis, resampling, or lossy conversion. SoundFont 3
 * compressed samples, ROM samples, and preset/instrument rendering are outside
 * this stored-stream reader's scope.
 */
#include "xxfclib/formats/soundfont2/xx_soundfont2.h"
#include "../xx_payload_members.h"

#define SF2_MAX_CHUNKS 4096U
#define SF2_MAX_SAMPLES 32767U

typedef struct sf2_chunk {
    uint8_t id[4];
    uint64_t data, size, next;
} sf2_chunk;

typedef struct sf2_layout {
    uint64_t riff_end;
    uint64_t smpl_at, smpl_size;
    uint64_t sm24_at, sm24_size;
    uint64_t shdr_at, shdr_size;
    unsigned next_table;
    bool has_ifil, has_smpl, has_sm24;
} sf2_layout;

static bool sf2_tag(const uint8_t *p, const char *s)
{
    return xx_rt_memcmp(p, s, 4) == 0;
}

static bool sf2_chunk_at(Abstractformat *f, uint64_t at, uint64_t end,
                         sf2_chunk *c)
{
    uint8_t h[8];
    if (at > end || end-at < sizeof(h) ||
        !pm_read(f, (int64_t)at, h, sizeof(h))) return false;
    xx_rt_memcpy(c->id, h, 4);
    c->data=at+8;
    c->size=pm_le32(h+4);
    if (c->size > end-c->data ||
        (c->size & 1U) > end-c->data-c->size) return false;
    c->next=c->data+c->size+(c->size & 1U);
    return true;
}

static int sf2_table_id(const uint8_t *id)
{
    static const char *const names[] = {
        "phdr", "pbag", "pmod", "pgen", "inst",
        "ibag", "imod", "igen", "shdr"
    };
    unsigned i;
    for (i=0; i<sizeof(names)/sizeof(names[0]); ++i)
        if (sf2_tag(id,names[i])) return (int)i;
    return -1;
}

static bool sf2_list(Abstractformat *f, uint64_t at, uint64_t end,
                     unsigned kind, sf2_layout *layout, xx_pd_struct *pd)
{
    static const uint8_t record_size[] = {38,4,10,4,22,4,10,4,46};
    unsigned chunks=0;
    while (at<end) {
        sf2_chunk c;
        int table;
        if (++chunks>SF2_MAX_CHUNKS || (pd && xx_pd_is_stopped(pd)) ||
            !sf2_chunk_at(f,at,end,&c)) return false;
        if (kind==0 && sf2_tag(c.id,"ifil")) {
            uint8_t version[4];
            if (layout->has_ifil || c.size!=4 ||
                !pm_read(f,(int64_t)c.data,version,4) ||
                pm_le16(version)!=2) return false;
            layout->has_ifil=true;
        } else if (kind==1 && sf2_tag(c.id,"smpl")) {
            if (layout->has_smpl || (c.size & 1U)) return false;
            layout->smpl_at=c.data;
            layout->smpl_size=c.size;
            layout->has_smpl=true;
        } else if (kind==1 && sf2_tag(c.id,"sm24")) {
            if (layout->has_sm24 || !layout->has_smpl) return false;
            layout->sm24_at=c.data;
            layout->sm24_size=c.size;
            layout->has_sm24=true;
        } else if (kind==2 && (table=sf2_table_id(c.id))>=0) {
            if ((unsigned)table!=layout->next_table ||
                !c.size || c.size%record_size[table] ||
                (table==8 && c.size/46U>SF2_MAX_SAMPLES+1U)) return false;
            ++layout->next_table;
            if (table==8) {
                layout->shdr_at=c.data;
                layout->shdr_size=c.size;
            }
        }
        at=c.next;
    }
    return at==end;
}

static bool sf2_scan(Abstractformat *f, sf2_layout *layout,
                     xx_pd_struct *pd)
{
    sf2_chunk c;
    uint8_t h[12], type[4];
    uint64_t at, available;
    unsigned step=0, chunks=0;
    int64_t total=pm_available(f);
    if (total<12 || !pm_read(f,0,h,sizeof(h)) ||
        !sf2_tag(h,"RIFF") || !sf2_tag(h+8,"sfbk") ||
        pm_le32(h+4)<4U) return false;
    available=(uint64_t)total;
    layout->riff_end=8U+(uint64_t)pm_le32(h+4);
    if (layout->riff_end>available) return false;
    at=12;
    while (at<layout->riff_end) {
        uint64_t list_end;
        if (++chunks>SF2_MAX_CHUNKS || (pd && xx_pd_is_stopped(pd)) ||
            !sf2_chunk_at(f,at,layout->riff_end,&c)) return false;
        if (sf2_tag(c.id,"LIST")) {
            if (c.size<4 || !pm_read(f,(int64_t)c.data,type,4)) return false;
            list_end=c.data+c.size;
            if (sf2_tag(type,"INFO")) {
                if (step!=0 || !sf2_list(f,c.data+4,list_end,0,layout,pd) ||
                    !layout->has_ifil) return false;
                step=1;
            } else if (sf2_tag(type,"sdta")) {
                if (step!=1 || !sf2_list(f,c.data+4,list_end,1,layout,pd) ||
                    (layout->has_sm24 &&
                     layout->sm24_size!=layout->smpl_size/2U)) return false;
                step=2;
            } else if (sf2_tag(type,"pdta")) {
                if (step!=2 || !sf2_list(f,c.data+4,list_end,2,layout,pd) ||
                    layout->next_table!=9U) return false;
                step=3;
            }
        }
        at=c.next;
    }
    /* An empty bank can have only EOS in shdr and an empty sdta LIST. This
     * matches FluidSynth's process_sdta() empty-list case. A real sample
     * header must always have a bounded smpl chunk to address. */
    return at==layout->riff_end && step==3 &&
        (layout->has_smpl || layout->shdr_size==46U);
}

static void sf2_sample_label(const uint8_t *header, bool low_byte,
                             char *out, size_t capacity)
{
    char name[21];
    size_t i, n=0;
    while (n<20 && header[n]) ++n;
    while (n && header[n-1]==' ') --n;
    if (!n) {
        (void)xx_rt_snprintf(out,capacity,low_byte ?
                             "unnamed.pcm24-lsb8" : "unnamed.pcm16le");
        return;
    }
    for (i=0; i<n; ++i) {
        uint8_t c=header[i];
        name[i]=(char)(c>=32 && c<=126 ? c : '_');
    }
    name[n]=0;
    (void)xx_rt_snprintf(out,capacity,"%s.%s",name,
                         low_byte ? "pcm24-lsb8" : "pcm16le");
}

static bool sf2_samples(Abstractformat *f, pm_stream *s,
                        const sf2_layout *layout, xx_pd_struct *pd)
{
    uint8_t *headers=NULL;
    uint32_t samples=(uint32_t)(layout->shdr_size/46U-1U);
    unsigned pass;
    bool ok=false;
    const uint8_t *terminal;
    headers=(uint8_t *)xx_mem_alloc((size_t)layout->shdr_size);
    if (!headers || !pm_read(f,(int64_t)layout->shdr_at,headers,
                             (size_t)layout->shdr_size)) goto done;
    terminal=headers+(size_t)samples*46U;
    if (xx_rt_memcmp(terminal,"EOS",3)!=0 || terminal[3]!=0) goto done;
    for (pass=0; pass<(layout->has_sm24 ? 2U : 1U); ++pass) {
        uint32_t i;
        for (i=0; i<samples; ++i) {
            const uint8_t *h=headers+(size_t)i*46U;
            uint32_t start=pm_le32(h+20), end=pm_le32(h+24);
            uint32_t loop_start=pm_le32(h+28), loop_end=pm_le32(h+32);
            uint32_t rate=pm_le32(h+36);
            uint16_t sample_type=pm_le16(h+44);
            uint16_t base_type=(uint16_t)(sample_type & 0x7fffU);
            char label[48];
            uint64_t offset, size;
            if ((pd && xx_pd_is_stopped(pd)) ||
                (base_type!=1U && base_type!=2U &&
                 base_type!=4U && base_type!=8U) || !rate ||
                start>end || loop_start<start || loop_start>loop_end ||
                loop_end>end) goto done;
            /* ROM sample indices address external memory, not sdta/smpl. */
            if (sample_type & 0x8000U) continue;
            if ((uint64_t)end*2U>layout->smpl_size) goto done;
            sf2_sample_label(h,pass!=0,label,sizeof(label));
            if (!pass) {
                offset=layout->smpl_at+(uint64_t)start*2U;
                size=(uint64_t)(end-start)*2U;
            } else {
                offset=layout->sm24_at+start;
                size=(uint64_t)(end-start);
            }
            if (!pm_add(f,s,label,(int64_t)offset,(int64_t)size)) goto done;
        }
    }
    ok=true;
done:
    if (headers) xx_mem_free(headers);
    return ok;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    sf2_layout layout;
    xx_mem_zero(&layout,sizeof(layout));
    if (!sf2_scan(f,&layout,pd) || !sf2_samples(f,s,&layout,pd)) return false;
    s->size=(int64_t)layout.riff_end;
    return true;
}

void xx_soundfont2_init(xx_soundfont2 *r, xx_io_device *d, int64_t base)
{
    if (r) {
        xx_mem_zero(r,sizeof(*r));
        pm_init(&r->format,d,base,XX_FILE_TYPE_SOUNDFONT2,"sf2");
    }
}
xx_soundfont2 *xx_soundfont2_create(xx_io_device *d, int64_t base)
{
    xx_soundfont2 *r=(xx_soundfont2 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_soundfont2_init(r,d,base);
    return r;
}
void xx_soundfont2_destroy(xx_soundfont2 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_soundfont2_free(xx_soundfont2 *r)
{
    if (r) { xx_soundfont2_destroy(r); xx_mem_free(r); }
}
bool xx_soundfont2_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f,pd);
}
bool xx_soundfont2_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f,pd);
}
