/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_cab/xx_sfx_cab.h"
#include "../sfx_arcv2/xx_sixth_wrapper_table.h"
#include "../sfx_arcv2/xx_pe_resource_locator.h"

typedef struct w6_cab_folder_s { uint32_t at;uint16_t blocks,type;uint64_t raw; } w6_cab_folder;
typedef struct w6_cab_file_s { uint32_t at,size;uint16_t folder; } w6_cab_file;
static uint32_t w6_cab_xor(const uint8_t *p,size_t n,uint32_t seed) { size_t i=0;uint32_t tail=0;while(n-i>=4) { seed^=pm_le32(p+i);i+=4; }while(i<n) tail=(tail<<8)|p[i++];return seed^tail; }
static bool w6_cab_at(Abstractformat *f,pm_stream *s,int64_t base,xx_pd_struct *pd) {
    const size_t io_capacity=xx_get_file_buffer_size();
    uint8_t h[36],*payload=NULL;uint32_t size,files_at;uint16_t nf,nn,flags;unsigned i,fr=0,dr=0;int64_t cursor,end,meta;uint64_t namebytes=0,blocks=0,checks=0;w6_cab_folder *folders=NULL;w6_cab_file *files=NULL;wg_extent *ranges=NULL;bool ok=false;
    if(!pm_read(f,base,h,36) || pm_le32(h+4) || pm_le32(h+12) || pm_le32(h+20) || h[24]!=3 || h[25]!=1 || (flags=pm_le16(h+30))&~4U || !(nf=pm_le16(h+26)) || nf>4096 || !(nn=pm_le16(h+28)) || nn>4096 || (size=pm_le32(h+8))<36 || !wg_range(pm_available(f),base,size) || (files_at=pm_le32(h+16))>=size) return false;
    end=base+size;cursor=base+36;
    if(flags&4) { unsigned reserve;if(end-cursor<4 || !pm_read(f,cursor,h,4) || (reserve=pm_le16(h))>4096 || !wg_range(end,cursor+4,reserve)) return false;fr=h[2];dr=h[3];cursor+=4+reserve; }
    payload=(uint8_t *)xx_mem_alloc(io_capacity);
    folders=(w6_cab_folder *)xx_mem_calloc(nf,sizeof(*folders));files=(w6_cab_file *)xx_mem_calloc(nn,sizeof(*files));ranges=(wg_extent *)xx_mem_alloc(nf*sizeof(*ranges));if(!payload || !folders || !files || !ranges) goto done;
    for(i=0;i<nf;++i) { if(wg_stop(pd) || !wg_range(end,cursor,8U+fr) || !pm_read(f,cursor,h,8)) goto done;folders[i].at=pm_le32(h);folders[i].blocks=pm_le16(h+4);folders[i].type=pm_le16(h+6);if(!folders[i].blocks || (folders[i].type&15)>3 || ((folders[i].type&15)<2 && folders[i].type>1) || (blocks+=folders[i].blocks)>65536) goto done;cursor+=8+fr; }
    if(base+files_at<cursor) { goto done; } cursor=base+files_at;
    for(i=0;i<nn;++i) { bool nul=false;unsigned length=0;if(wg_stop(pd) || end-cursor<16 || !pm_read(f,cursor,h,16)) goto done;files[i].size=pm_le32(h);files[i].at=pm_le32(h+4);files[i].folder=pm_le16(h+8);if(files[i].folder>=nf) goto done;cursor+=16;
        while(cursor<end && !nul) { uint8_t *buf=payload;size_t n=(uint64_t)(end-cursor)>io_capacity ? io_capacity:(size_t)(end-cursor),j;if(wg_stop(pd) || !pm_read(f,cursor,buf,n)) goto done;for(j=0;j<n;++j) { ++cursor;if(!buf[j]) { nul=true;break; }if(buf[j]<32 || ++length>4096 || ++namebytes>1048576) goto done; } }
        if(!nul || !length) goto done;
    }meta=cursor;
    for(i=0;i<nf;++i) { unsigned j;int64_t p=base+folders[i].at;if(p<meta || p>=end) goto done;ranges[i].lo=p;
        for(j=0;j<folders[i].blocks;++j) { uint16_t packed,raw;uint32_t checksum;if(wg_stop(pd) || !wg_range(end,p,8U+dr) || !pm_read(f,p,h,8)) goto done;checksum=pm_le32(h);packed=pm_le16(h+4);raw=pm_le16(h+6);if(!packed || !raw || raw>32768 || ((folders[i].type&15)==0 && packed!=raw) || !wg_range(end,p+8,(uint64_t)dr+packed) || (folders[i].raw+=raw)>UINT32_MAX) goto done;
            if(checksum) {
                size_t n=packed,done=0,held=0;uint8_t word[4];uint32_t value=w6_cab_xor(h+4,4,0),tail=0;
                if(n>67108864-checks) { goto done; } checks+=n;
                while(done<n) {
                    size_t amount=n-done,k;if(amount>io_capacity) amount=io_capacity;
                    if(wg_stop(pd) || !pm_read(f,p+8+dr+(int64_t)done,payload,amount)) goto done;
                    for(k=0;k<amount;++k) {word[held++]=payload[k];if(held==sizeof(word)) {value^=pm_le32(word);held=0;}}
                    done+=amount;
                }
                for(done=0;done<held;++done) tail=(tail<<8)|word[done];
                if((value^tail)!=checksum) goto done;
            }
            p+=8+dr+packed;
        }ranges[i].hi=p;
    }if(!wg_extents(ranges,nf,pd)) goto done;
    for(i=0;i<nn;++i) if((uint64_t)files[i].at+files[i].size>folders[files[i].folder].raw) goto done;
    ok=!wg_stop(pd) && w6_component(f,s,base,size,"payload.cab");
done:if(payload) xx_mem_free(payload);if(ranges) xx_mem_free(ranges);if(files) xx_mem_free(files);if(folders) xx_mem_free(folders);return ok;
}
static bool w5_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { static const uint8_t sig[]={'M','S','C','F'};return w6_scan(f,s,sig,4,0,true,false,w6_cab_at,pd); }


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w5_parse(f,s,pd) && wg_members(s,pd); }

/* The generic CAB reader already validates the folder/file graph and knows
 * how to decode its compression methods. Locate a CAB within a validated PE
 * carrier and delegate the archive members instead of exposing payload.cab. */
static bool sfx_cab_probe_at(Abstractformat *format, pm_stream *unused,
                             int64_t at, xx_pd_struct *pd) {
    xx_sfx_cab *archive = (xx_sfx_cab *)format;
    xx_cab candidate;
    bool valid;
    uint8_t header[36];
    int64_t total = pm_available(format);
    (void)unused;
    if (at < 0 || at > total - 36 ||
        !pm_read(format, at, header, sizeof(header)) ||
        xx_rt_memcmp(header, "MSCF", 4) != 0 ||
        pm_le32(header + 4) != 0U ||
        pm_le32(header + 12) != 0U ||
        pm_le32(header + 20) != 0U ||
        header[24] != 3U || header[25] != 1U ||
        pm_le16(header + 26) == 0U ||
        pm_le16(header + 28) == 0U ||
        (pm_le16(header + 30) & ~7U) != 0U ||
        pm_le32(header + 8) < 36U ||
        pm_le32(header + 8) > (uint64_t)(total - at) ||
        pm_le32(header + 16) >= pm_le32(header + 8)) return false;
    xx_cab_init(&candidate, format->device, format->base_address + at);
    valid = xx_cab_handle_base_info(&candidate.format, pd) &&
            candidate.number_of_records != 0U &&
            candidate.number_of_records <= 65535U &&
            candidate.format.format_size >= 36 &&
            candidate.format.format_size <= total - at;
    xx_cab_destroy(&candidate);
    if (valid) archive->payload_offset = at;
    return valid;
}

static bool sfx_cab_find(xx_sfx_cab *archive, xx_pd_struct *pd) {
    int64_t low, total;
    size_t size, index;
    unsigned candidates = 0U;
    uint8_t *data;
    if (!archive || !archive->format.device) return false;
    if (archive->payload_offset >= 0) return true;
    total = pm_available(&archive->format);
    if (!w6_carrier(&archive->format, &low, true, false, pd) ||
        low < 0 || low >= total) return false;
    size = (size_t)((total - low) > 16777216 ? 16777216 : total - low);
    data = (uint8_t *)xx_mem_alloc(size);
    if (!data) return false;
    if (!pm_read(&archive->format, low, data, size)) {
        xx_mem_free(data);
        return false;
    }
    archive->payload_offset = -1;
    for (index = 0U; index + 36U <= size; ++index) {
        if ((index & 65535U) == 0U && wg_stop(pd)) break;
        if (data[index] != 'M' || data[index + 1U] != 'S' ||
            data[index + 2U] != 'C' || data[index + 3U] != 'F') continue;
        if (++candidates > 16U) break;
        if (sfx_cab_probe_at(&archive->format, NULL,
                             low + (int64_t)index, pd)) {
            xx_mem_free(data);
            return true;
        }
    }
    xx_mem_free(data);
    return false;
}

static bool sfx_cab_prefix_record(xx_archive_record_state *state) {
    const char *name;
    char *prefixed;
    bool result;
    if (!state || !state->has_record) return true;
    name = xx_archive_record_get_original_name(&state->current_record);
    if (!name || !name[0]) return false;
    prefixed = xx_str_concat("1/", name);
    if (!prefixed) return false;
    result = xx_archive_record_set_original_name(&state->current_record,
                                                  prefixed);
    xx_str_free(prefixed);
    return result;
}

static bool sfx_cab_prefix_output_path(xx_archive_record_state *state) {
    size_t index;
    if (!state) return false;
    for (index = 0U; index < state->options.count; ++index) {
        xx_meta *meta = (xx_meta *)xx_list_at(
            (xx_list_t *)&state->options, index);
        const char *base = NULL;
        char *converted = NULL;
        char *joined;
        bool result;
        size_t length;
        if (!meta || meta->meta_id != XX_META_ID_OPT_UNPACK_PATH) continue;
        if (meta->var.type == XX_VAR_TYPE_STRING ||
            meta->var.type == XX_VAR_TYPE_STRING_VIEW) {
            base = xx_var_get_str(&meta->var);
        } else if (meta->var.type == XX_VAR_TYPE_WSTRING ||
                   meta->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            converted = xx_str_unicode_to_utf8(xx_var_get_wstr(&meta->var));
            base = converted;
        }
        if (!base) {
            xx_str_free(converted);
            return false;
        }
        length = xx_str_len(base);
        joined = length == 0U ? xx_str_dup("1") :
                 (base[length - 1U] == '/' || base[length - 1U] == '\\')
                     ? xx_str_concat(base, "1")
                     : xx_str_concat3(base, "/", "1");
        xx_str_free(converted);
        if (!joined) return false;
        result = xx_var_set_str(&meta->var, joined);
        xx_str_free(joined);
        return result;
    }
    return true;
}

static int64_t sfx_cab_size(Abstractformat *format, xx_pd_struct *pd) {
    if (!format || (!format->base_info_handled &&
                    !xx_sfx_cab_handle_base_info(format, pd))) return -1;
    return format->format_size;
}

static uint64_t sfx_cab_count(Abstractformat *format, xx_pd_struct *pd) {
    if (!format || (!format->base_info_handled &&
                    !xx_sfx_cab_handle_base_info(format, pd))) return 0U;
    return format->number_of_archive_records;
}

static xx_archive_record_state *sfx_cab_create_records(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    xx_sfx_cab *archive = (xx_sfx_cab *)format;
    xx_archive_record_state *state;
    if (!archive || (!archive->inner_ready &&
                     !xx_sfx_cab_handle_base_info(format, pd))) return NULL;
    state = xx_cab_create_archive_records_reading(&archive->inner.format,
                                                   options, pd);
    if (state && archive->resource_member &&
        (!sfx_cab_prefix_output_path(state) ||
         !sfx_cab_prefix_record(state))) {
        xx_cab_free_archive_records_reading(&archive->inner.format, state);
        return NULL;
    }
    return state;
}

static const xx_archive_record *sfx_cab_current(
    Abstractformat *format, xx_archive_record_state *state) {
    xx_sfx_cab *archive = (xx_sfx_cab *)format;
    return archive && archive->inner_ready
               ? xx_cab_get_current_archive_record(&archive->inner.format,
                                                   state)
               : NULL;
}

static bool sfx_cab_next(Abstractformat *format,
                          xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_sfx_cab *archive = (xx_sfx_cab *)format;
    return archive && archive->inner_ready &&
           xx_cab_archive_record_move_to_next(&archive->inner.format,
                                              state, pd) &&
           (!archive->resource_member || sfx_cab_prefix_record(state));
}

static bool sfx_cab_unpack(Abstractformat *format,
                            xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_sfx_cab *archive = (xx_sfx_cab *)format;
    return archive && archive->inner_ready &&
           xx_cab_unpack_current_archive_record(&archive->inner.format,
                                                state, pd);
}

static void sfx_cab_free_records(Abstractformat *format,
                                  xx_archive_record_state *state) {
    xx_sfx_cab *archive = (xx_sfx_cab *)format;
    if (archive && archive->inner_ready)
        xx_cab_free_archive_records_reading(&archive->inner.format, state);
    else
        xx_archive_record_state_free(state);
}

void xx_sfx_cab_init(xx_sfx_cab *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    pm_init(&archive->format, device, base_address, XX_FILE_TYPE_SFX_CAB,
            "exe");
    archive->payload_offset = -1;
    archive->format.check_is_valid = xx_sfx_cab_check_is_valid;
    archive->format.handle_base_info = xx_sfx_cab_handle_base_info;
    archive->format.get_format_size = sfx_cab_size;
    archive->format.get_number_of_archive_records = sfx_cab_count;
    archive->format.create_archive_records_reading = sfx_cab_create_records;
    archive->format.get_current_archive_record = sfx_cab_current;
    archive->format.archive_record_move_to_next = sfx_cab_next;
    archive->format.unpack_current_archive_record = sfx_cab_unpack;
    archive->format.free_archive_records_reading = sfx_cab_free_records;
}

xx_sfx_cab *xx_sfx_cab_create(xx_io_device *device, int64_t base_address) {
    xx_sfx_cab *archive = (xx_sfx_cab *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_cab_init(archive, device, base_address);
    return archive;
}

void xx_sfx_cab_destroy(xx_sfx_cab *archive) {
    if (!archive) return;
    if (archive->inner_ready) xx_cab_destroy(&archive->inner);
    archive->inner_ready = false;
    archive->resource_member = false;
    xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfx_cab_free(xx_sfx_cab *archive) {
    if (!archive) return;
    xx_sfx_cab_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_cab_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    return sfx_cab_find((xx_sfx_cab *)format, pd);
}

bool xx_sfx_cab_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xx_sfx_cab *archive = (xx_sfx_cab *)format;
    int64_t total;
    if (!archive) return false;
    if (archive->inner_ready) {
        xx_cab_destroy(&archive->inner);
        archive->inner_ready = false;
    }
    archive->resource_member = false;
    if (!sfx_cab_find(archive, pd)) return false;
    xx_cab_init(&archive->inner, format->device,
                format->base_address + archive->payload_offset);
    if (!xx_cab_handle_base_info(&archive->inner.format, pd)) {
        xx_cab_destroy(&archive->inner);
        return false;
    }
    archive->inner_ready = true;
    archive->resource_member = xx_sfx_pe_resource_contains(
        format, archive->payload_offset,
        (uint32_t)archive->inner.format.format_size);
    format->format_size = archive->payload_offset +
                          archive->inner.format.format_size;
    format->number_of_archive_records =
        archive->inner.format.number_of_archive_records;
    format->is_valid = true;
    format->base_info_handled = true;
    total = pm_available(format);
    format->overlay_offset = format->format_size < total
                                 ? format->base_address + format->format_size
                                 : -1;
    format->overlay_size = format->format_size < total
                               ? total - format->format_size : 0;
    return true;
}
