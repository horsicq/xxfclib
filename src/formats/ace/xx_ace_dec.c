/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* Bounded ACE 1.x/2.x LZ/Huffman stream decoder. */
#include "xxfclib/rt/xx_rt.h"
#include "xx_ace_dec.h"
#include "xxfclib/memory/xx_memory.h"
#include <string.h>

#define ACE_MAIN_BITS 11U
#define ACE_META_BITS 7U
#define ACE_MAIN_COUNT 284U
#define ACE_LENGTH_COUNT 256U
#define ACE_DICT_MIN 10U
#define ACE_DICT_MAX 22U
#define ACE_MAX_CODES 289U
/* ACE 2.0 SOUND mode: 32 zero-run codes, 256 values, one mode code. */
#define ACE_SOUND_BITS 10U
#define ACE_SOUND_RUNLEN 32U
#define ACE_SOUND_TYPECODE 288U
#define ACE_SOUND_COUNT 289U
#define ACE_SOUND_MODELS 9U
#define ACE_MODE_SOUND_8 3U
#define ACE_MODE_SOUND_32B 6U
/* ACE 2.0 PIC mode: 365 error contexts per model; row width and plane count
 * are capped far above any picture the ACE 2 compressor produces. */
#define ACE_MODE_PIC 7U
#define ACE_PIC_CONTEXTS 365U
#define ACE_PIC_MAX_WIDTH ((uint64_t)1U << 22)
#define ACE_PIC_MAX_PLANES ((uint64_t)1U << 16)
#define ACE_PIC_MAX_RICE 24U
#define ACE_PIC_MAX_VALUE 0x7FFFFFFFU

typedef struct ace_bits_s { const uint8_t *data; size_t size, position; bool bad; } ace_bits;
typedef struct ace_state_s {
    ace_bits bits; uint16_t main_table[1U<<ACE_MAIN_BITS], length_table[1U<<ACE_MAIN_BITS], meta_table[1U<<ACE_META_BITS];
    uint8_t main_width[ACE_MAIN_COUNT], length_width[ACE_LENGTH_COUNT], meta_width[16];
    uint8_t *dict; size_t dict_size, dict_pos, history, output_pos, output_size;
    uint32_t old_dist[4]; unsigned old_index, block_left; uint8_t *output;
    uint8_t next_mode, delta_dist, exe_mode; uint32_t delta_len;
} ace_state;
static uint32_t ace_word(const ace_bits *b,size_t offset){uint32_t value=0;unsigned i;for(i=0;i<4U&&offset+i<b->size;i++)value|=(uint32_t)b->data[offset+i]<<(8U*i);return value;}
/* ACE Huffman decoding reads a short zero-padded look-ahead window.  Bounds
 * are enforced when bits are consumed, rather than when they are inspected. */
static uint32_t ace_peek(ace_bits *b,unsigned n){size_t word,shift;uint64_t value;if(!b||n==0U||n>24U||b->position>b->size*8U){if(b)b->bad=true;return 0;}word=b->position/32U;shift=b->position%32U;value=(uint64_t)ace_word(b,word*4U)<<32U;value|=ace_word(b,(word+1U)*4U);return (uint32_t)((value<<shift)>>(64U-n));}
static void ace_skip(ace_bits *b,unsigned n){if(!b||b->position+n>b->size*8U)b->bad=true;else b->position+=n;}
static void ace_sort_range(uint8_t *frequency,uint16_t *symbol,int left,int right){int lo=left,hi=right;uint8_t pivot=frequency[right];do{while(frequency[lo]>pivot)++lo;while(frequency[hi]<pivot)--hi;if(lo<=hi){uint8_t f=frequency[lo];uint16_t s=symbol[lo];frequency[lo]=frequency[hi];symbol[lo]=symbol[hi];frequency[hi]=f;symbol[hi]=s;++lo;--hi;}}while(lo<hi);if(left<hi){if(left<hi-1)ace_sort_range(frequency,symbol,left,hi);else if(frequency[left]<frequency[hi]){uint8_t f=frequency[left];uint16_t s=symbol[left];frequency[left]=frequency[hi];symbol[left]=symbol[hi];frequency[hi]=f;symbol[hi]=s;}}if(right>lo){if(lo<right-1)ace_sort_range(frequency,symbol,lo,right);else if(frequency[lo]<frequency[right]){uint8_t f=frequency[lo];uint16_t s=symbol[lo];frequency[lo]=frequency[right];symbol[lo]=symbol[right];frequency[right]=f;symbol[right]=s;}}}
/* ACE's table construction preserves the original decoder's quick-sort tie
 * order.  That order is observable for intentionally degenerate tables. */
static bool ace_make_table(uint8_t *width,size_t count,unsigned bits,uint16_t *table){uint16_t symbol[ACE_MAX_CODES];uint8_t frequency[ACE_MAX_CODES];size_t i,nonzero=0,cursor=0;unsigned size=1U<<bits;if(!width||!table||count==0U||count>ACE_MAX_CODES||bits>ACE_MAIN_BITS)return false;for(i=0;i<count;i++){if(width[i]>bits)return false;frequency[i]=width[i];symbol[i]=(uint16_t)i;}if(count>1U)ace_sort_range(frequency,symbol,0,(int)count-1);while(nonzero<count&&frequency[nonzero])++nonzero;if(nonzero<2U){width[symbol[0]]=1U;if(!nonzero)nonzero=1U;}--nonzero;for(i=0;i<size;i++)table[i]=UINT16_MAX;for(;;){unsigned span=1U<<(bits-frequency[nonzero]);if(cursor+span>size)return false;while(span--)table[cursor++]=symbol[nonzero];if(nonzero==0U)break;--nonzero;}return true;}
static bool ace_read_widths(ace_state *s,unsigned bits,uint16_t *table,uint8_t *width,size_t capacity){unsigned number,lower,upper,i,j=0;if(!s||capacity==0)return false;xx_rt_memset(width,0,capacity);number=ace_peek(&s->bits,9);ace_skip(&s->bits,9);lower=ace_peek(&s->bits,4);ace_skip(&s->bits,4);upper=ace_peek(&s->bits,4);ace_skip(&s->bits,4);if(s->bits.bad||number>=capacity||upper>15U)return false;xx_rt_memset(s->meta_width,0,sizeof(s->meta_width));for(i=0;i<=upper;i++){s->meta_width[i]=(uint8_t)ace_peek(&s->bits,3);ace_skip(&s->bits,3);}if(s->bits.bad||!ace_make_table(s->meta_width,upper+1U,ACE_META_BITS,s->meta_table))return false;while(j<=number){unsigned symbol=ace_peek(&s->bits,ACE_META_BITS),run;if(symbol>=(1U<<ACE_META_BITS)||(symbol=s->meta_table[symbol])>upper||!s->meta_width[symbol])return false;ace_skip(&s->bits,s->meta_width[symbol]);if(symbol<upper)width[j++]=(uint8_t)symbol;else{run=ace_peek(&s->bits,4)+4U;ace_skip(&s->bits,4);while(run--&&j<=number)width[j++]=0U;}}if(s->bits.bad)return false;for(i=1;i<=number;i++)if(upper)width[i]=(uint8_t)((width[i]+width[i-1])%upper);for(i=0;i<=number;i++)if(width[i]){if(width[i]+lower>bits)return false;width[i]=(uint8_t)(width[i]+lower);}return ace_make_table(width,number+1U,bits,table);}
static bool ace_new_block(ace_state *s){if(!ace_read_widths(s,ACE_MAIN_BITS,s->main_table,s->main_width,ACE_MAIN_COUNT)||!ace_read_widths(s,ACE_MAIN_BITS,s->length_table,s->length_width,ACE_LENGTH_COUNT))return false;s->block_left=ace_peek(&s->bits,15);ace_skip(&s->bits,15);return !s->bits.bad&&s->block_left!=0U;}
static bool ace_byte(ace_state *s,uint8_t value){if(!s||s->output_pos>=s->output_size)return false;s->output[s->output_pos++]=value;s->dict[s->dict_pos++]=value;s->dict_pos&=s->dict_size-1U;if(s->history<s->dict_size)++s->history;return true;}
static bool ace_copy(ace_state *s,uint32_t distance,unsigned length){size_t p;if(!s||distance==0U||distance>s->dict_size||distance>s->history||length>259U||length>s->output_size-s->output_pos)return false;p=(s->dict_pos+s->dict_size-distance)&(s->dict_size-1U);while(length--){if(!ace_byte(s,s->dict[p]))return false;p=(p+1U)&(s->dict_size-1U);}return true;}
/* Reads the mode instruction that follows an ACE 2 type code. */
static int ace_read_mode(ace_state *s){s->next_mode=(uint8_t)ace_peek(&s->bits,8);ace_skip(&s->bits,8);if(s->next_mode==1U){s->delta_dist=(uint8_t)ace_peek(&s->bits,8);ace_skip(&s->bits,8);s->delta_len=ace_peek(&s->bits,17);ace_skip(&s->bits,17);}else if(s->next_mode==2U){s->exe_mode=(uint8_t)ace_peek(&s->bits,8);ace_skip(&s->bits,8);}return s->bits.bad?-1:1;}
/* Returns 0 for output, 1 for an ACE 2 mode instruction and -1 for invalid
 * compressed data.  The mode marker is part of the ACE 2 main symbol tree. */
static int ace_symbol(ace_state *s,bool blocked){unsigned code,length_code,base=2U;uint32_t distance;if(!s->block_left&&!ace_new_block(s))return -1;code=ace_peek(&s->bits,ACE_MAIN_BITS);if(s->bits.bad||code>=(1U<<ACE_MAIN_BITS)||(code=s->main_table[code])>=ACE_MAIN_COUNT||!s->main_width[code])return -1;ace_skip(&s->bits,s->main_width[code]);--s->block_left;if(code==283U)return blocked?ace_read_mode(s):-1;if(code<256U)return ace_byte(s,(uint8_t)code)?0:-1;if(code>259U){unsigned exponent=code-260U;if(exponent>22U)return -1;if(exponent>1U){distance=ace_peek(&s->bits,exponent-1U)+(1U<<(exponent-1U));ace_skip(&s->bits,exponent-1U);}else distance=exponent;s->old_index=(s->old_index+1U)&3U;s->old_dist[s->old_index]=distance;if(distance>255U)++base;if(distance>8191U)++base;}else{unsigned ref=code&3U,k;distance=s->old_dist[(s->old_index-ref)&3U];for(k=ref+1U;k-- >0U;)s->old_dist[(s->old_index-k)&3U]=s->old_dist[(s->old_index-k+1U)&3U];s->old_dist[s->old_index]=distance;if(ref>1U)++base;}length_code=ace_peek(&s->bits,ACE_MAIN_BITS);if(s->bits.bad||length_code>=(1U<<ACE_MAIN_BITS)||(length_code=s->length_table[length_code])>=ACE_LENGTH_COUNT||!s->length_width[length_code])return -1;ace_skip(&s->bits,s->length_width[length_code]);return !s->bits.bad&&ace_copy(s,distance+1U,length_code+base)?0:-1;}
static bool ace_init(ace_state *s,const void *source,size_t source_size,void *destination,size_t destination_size,unsigned dictionary_bits){if((!source&&source_size)||!destination||source_size==0U||(source_size&3U)||dictionary_bits<ACE_DICT_MIN||dictionary_bits>ACE_DICT_MAX)return false;xx_rt_memset(s,0,sizeof(*s));s->bits.data=(const uint8_t *)source;s->bits.size=source_size;s->output=(uint8_t *)destination;s->output_size=destination_size;s->dict_size=(size_t)1U<<dictionary_bits;s->dict=(uint8_t *)xx_mem_calloc(s->dict_size,1U);return s->dict!=NULL;}
static bool ace_filter_delta(uint8_t *out,size_t start,size_t end,unsigned dist,uint32_t expected,uint8_t *last){uint8_t *tmp;size_t n=end-start,i,plane,pos;if(!dist||!expected||n!=expected||n%dist)return false;for(i=start;i<end;i++){out[i]=(uint8_t)(out[i]+*last);*last=out[i];}tmp=(uint8_t *)xx_mem_alloc(n);if(!tmp)return false;plane=n/dist;pos=0;for(i=0;i<plane;i++){size_t j;for(j=i;j<n;j+=plane)tmp[pos++]=out[start+j];}xx_rt_memcpy(out+start,tmp,n);xx_mem_free(tmp);return true;}
static void ace_filter_exe(uint8_t *out,size_t start,size_t end,unsigned mode){size_t i;for(i=start;i<end;i++){uint32_t v,pos=(uint32_t)i;if(out[i]==0xe8U&&i+2U<end){if(mode==0U){v=(uint32_t)out[i+1]|((uint32_t)out[i+2]<<8);v=(v-pos)&0xffffU;out[i+1]=(uint8_t)v;out[i+2]=(uint8_t)(v>>8);i+=2U;}else if(i+4U<end){v=(uint32_t)out[i+1]|((uint32_t)out[i+2]<<8)|((uint32_t)out[i+3]<<16)|((uint32_t)out[i+4]<<24);v-=pos;out[i+1]=(uint8_t)v;out[i+2]=(uint8_t)(v>>8);out[i+3]=(uint8_t)(v>>16);out[i+4]=(uint8_t)(v>>24);i+=4U;}}else if(out[i]==0xe9U&&i+2U<end){v=(uint32_t)out[i+1]|((uint32_t)out[i+2]<<8);v=(v-pos)&0xffffU;out[i+1]=(uint8_t)v;out[i+2]=(uint8_t)(v>>8);i+=2U;}}}
void xx_ace_history_clear(xx_ace_history *history) {
    if (!history) return;
    xx_mem_free(history->dictionary);
    xx_rt_memset(history, 0, sizeof(*history));
}
bool xx_ace_history_append(xx_ace_history *history, const void *bytes,
                           size_t count, unsigned dictionary_bits) {
    const uint8_t *source = (const uint8_t *)bytes;
    size_t size, i;
    if (!history || (!source && count) || dictionary_bits < ACE_DICT_MIN ||
        dictionary_bits > ACE_DICT_MAX) return false;
    size = (size_t)1U << dictionary_bits;
    if (history->dictionary_size != size) xx_ace_history_clear(history);
    if (!history->dictionary) {
        history->dictionary = (uint8_t *)xx_mem_calloc(size, 1U);
        if (!history->dictionary) return false;
        history->dictionary_size = size;
    }
    for (i = 0; i < count; ++i) {
        history->dictionary[history->position] = source[i];
        history->position = (history->position + 1U) & (size - 1U);
        if (history->filled < size) ++history->filled;
    }
    return true;
}
static void ace_reuse_history(ace_state *s, xx_ace_history *history) {
    if (!history || !history->dictionary) return;
    if (history->dictionary_size != s->dict_size || history->position >= s->dict_size ||
        history->filled > s->dict_size) {
        xx_ace_history_clear(history);
        return;
    }
    xx_mem_free(s->dict);
    s->dict = history->dictionary;
    s->dict_pos = history->position;
    s->history = history->filled;
    xx_rt_memset(history, 0, sizeof(*history));
}
static void ace_finish_history(ace_state *s, xx_ace_history *history, bool ok) {
    if (history && ok) {
        history->dictionary = s->dict;
        history->dictionary_size = s->dict_size;
        history->position = s->dict_pos;
        history->filled = s->history;
    } else {
        xx_mem_free(s->dict);
        if (history) xx_ace_history_clear(history);
    }
}
bool xx_ace_decode_lzh_solid(const void *source,size_t source_size,void *destination,size_t destination_size,unsigned dictionary_bits,xx_ace_history *history){ace_state s;bool ok;if(!ace_init(&s,source,source_size,destination,destination_size,dictionary_bits))return false;ace_reuse_history(&s,history);while(s.output_pos<destination_size&&!s.bits.bad)if(ace_symbol(&s,false)!=0){s.bits.bad=true;break;}ok=!s.bits.bad&&s.output_pos==destination_size&&s.bits.position<=source_size*8U&&source_size*8U-s.bits.position<32U;ace_finish_history(&s,history,ok);return ok;}
bool xx_ace_decode_lzh(const void *source,size_t source_size,void *destination,size_t destination_size,unsigned dictionary_bits){return xx_ace_decode_lzh_solid(source,source_size,destination,destination_size,dictionary_bits,NULL);}
/* ACE 2.0 SOUND mode (modes 3..6): per-channel adaptive linear predictor
 * over Huffman-coded residuals, three code trees per channel.
 * Ported from acefile.py (class Sound), Copyright (C) 2017-2026 Daniel
 * Roethlisberger, BSD 2-Clause licence. */
typedef struct ace_channel_s {
    int32_t pred_dif_cnt[2], last_pred_dif_cnt[2];
    int32_t rar_dif_cnt[4], rar_coeff[4], rar_dif[9];
    int32_t last_sample, last_delta;
    uint32_t byte_count, adapt_model_cnt;
    unsigned adapt_model_use, get_state, get_code;
} ace_channel;
typedef struct ace_sound_s {
    uint16_t table[ACE_SOUND_MODELS][1U << ACE_SOUND_BITS];
    uint8_t width[ACE_SOUND_MODELS][ACE_SOUND_COUNT];
    ace_channel channel[3];
    unsigned variant, models, block_left;
} ace_sound;
static const uint8_t ace_sound_channels[4] = {1U, 2U, 3U, 3U};
static const uint8_t ace_sound_use[4][4] = {
    {0U, 0U, 0U, 0U}, {0U, 1U, 0U, 1U}, {0U, 1U, 0U, 2U}, {1U, 0U, 2U, 0U}};
static int32_t ace_schar(int32_t v) { return (int32_t)((v & 0xFF) ^ 0x80) - 0x80; }
static int32_t ace_abs(int32_t v) { return v < 0 ? -v : v; }
/* Floor division by 8, independent of the compiler's signed shift. */
static int32_t ace_floor8(int32_t v) { return v >= 0 ? v / 8 : -((-v + 7) / 8); }
/* Bit length of |v| for v in -128..128 (stored as an unsigned byte index). */
static unsigned ace_quantize(uint8_t index) {
    unsigned v = index <= 128U ? index : 256U - index, n = 0;
    while (v) { ++n; v >>= 1; }
    return n;
}
static void ace_sound_reset(ace_sound *snd, unsigned variant) {
    xx_rt_memset(snd, 0, sizeof(*snd));
    snd->variant = variant;
    snd->models = 3U * ace_sound_channels[variant];
}
static int ace_sound_symbol(ace_state *s, ace_sound *snd, unsigned model) {
    unsigned code, i;
    if (model >= snd->models) return -1;
    if (!snd->block_left) {
        for (i = 0; i < snd->models; ++i)
            if (!ace_read_widths(s, ACE_SOUND_BITS, snd->table[i], snd->width[i], ACE_SOUND_COUNT))
                return -1;
        snd->block_left = ace_peek(&s->bits, 15);
        ace_skip(&s->bits, 15);
        if (s->bits.bad || !snd->block_left) return -1;
    }
    code = ace_peek(&s->bits, ACE_SOUND_BITS);
    if (s->bits.bad || code >= (1U << ACE_SOUND_BITS)) return -1;
    code = snd->table[model][code];
    if (code >= ACE_SOUND_COUNT || !snd->width[model][code]) return -1;
    ace_skip(&s->bits, snd->width[model][code]);
    --snd->block_left;
    return s->bits.bad ? -1 : (int)code;
}
/* Returns 0 with a residual byte in *out, 1 for a mode instruction, -1 on error. */
static int ace_sound_get(ace_state *s, ace_sound *snd, unsigned ch, unsigned *out) {
    ace_channel *c = &snd->channel[ch];
    unsigned value = 0;
    if (c->get_state != 2U) {
        unsigned model = c->get_state << 1;
        int sym;
        if (!model) model += c->adapt_model_use;
        sym = ace_sound_symbol(s, snd, model + 3U * ch);
        if (sym < 0) return -1;
        if ((unsigned)sym == ACE_SOUND_TYPECODE) return ace_read_mode(s);
        c->get_code = (unsigned)sym;
    }
    if (c->get_state == 0U) {
        if (c->get_code >= ACE_SOUND_RUNLEN) {
            value = c->get_code - ACE_SOUND_RUNLEN;
            c->adapt_model_cnt = ((c->adapt_model_cnt * 7U) >> 3) + value;
            c->adapt_model_use = c->adapt_model_cnt > 40U ? 1U : 0U;
        } else {
            c->get_state = 2U;
        }
    } else if (c->get_state == 1U) {
        value = c->get_code;
        c->get_state = 0U;
    }
    if (c->get_state == 2U) {
        if (!c->get_code) c->get_state = 1U;
        else --c->get_code;
        value = 0;
    }
    *out = (value & 1U) ? 255U - (value >> 1) : value >> 1;
    return 0;
}
static int32_t ace_sound_predicted(const ace_channel *c) {
    int32_t v = 8 * c->last_sample + c->rar_coeff[0] * c->rar_dif_cnt[0] +
                c->rar_coeff[1] * c->rar_dif_cnt[1] + c->rar_coeff[2] * c->rar_dif_cnt[2] +
                c->rar_coeff[3] * c->rar_dif_cnt[3];
    return ace_floor8(v) & 0xFF;
}
static int32_t ace_sound_predict(const ace_channel *c) {
    return c->pred_dif_cnt[0] > c->pred_dif_cnt[1] ? c->last_sample : ace_sound_predicted(c);
}
static void ace_sound_adjust(ace_channel *c, int32_t sample) {
    int32_t pred_dif = ace_schar(ace_sound_predicted(c) - sample) * 8;
    unsigned i;
    ++c->byte_count;
    for (i = 0; i < 4U; ++i) {
        c->rar_dif[2U * i] += ace_abs(pred_dif - c->rar_dif_cnt[i]);
        c->rar_dif[2U * i + 1U] += ace_abs(pred_dif + c->rar_dif_cnt[i]);
    }
    c->rar_dif[8] += ace_abs(pred_dif);
    c->last_delta = ace_schar(sample - c->last_sample);
    c->pred_dif_cnt[0] += (int32_t)ace_quantize((uint8_t)(pred_dif / 8));
    c->pred_dif_cnt[1] += (int32_t)ace_quantize((uint8_t)(c->last_sample - sample));
    c->last_sample = sample;
    if (!(c->byte_count & 0x1FU)) {
        int32_t min_dif = 0xFFFF;
        unsigned pos = 8U;
        for (i = 9U; i-- > 0U;) {
            if (c->rar_dif[i] <= min_dif) { min_dif = c->rar_dif[i]; pos = i; }
            c->rar_dif[i] = 0;
        }
        if (pos != 8U) {
            i = pos >> 1;
            if (!(pos & 1U)) { if (c->rar_coeff[i] >= -16) --c->rar_coeff[i]; }
            else if (c->rar_coeff[i] <= 16) ++c->rar_coeff[i];
        }
        if (!(c->byte_count & 0xFFU))
            for (i = 0; i < 2U; ++i) {
                c->pred_dif_cnt[i] -= c->last_pred_dif_cnt[i];
                c->last_pred_dif_cnt[i] = c->pred_dif_cnt[i];
            }
    }
    c->rar_dif_cnt[3] = c->rar_dif_cnt[2];
    c->rar_dif_cnt[2] = c->rar_dif_cnt[1];
    c->rar_dif_cnt[1] = c->last_delta - c->rar_dif_cnt[0];
    c->rar_dif_cnt[0] = c->last_delta;
}
/* Decodes one SOUND segment.  Like the reference, it emits whole groups of
 * four bytes; returns 1 at a mode instruction, 0 at the output limit and -1
 * on invalid data. */
static int ace_sound_run(ace_state *s, ace_sound *snd) {
    size_t want = (s->output_size - s->output_pos) & ~(size_t)3U, i;
    for (i = 0; i < want; ++i) {
        unsigned ch = ace_sound_use[snd->variant][i & 3U], value = 0;
        ace_channel *c = &snd->channel[ch];
        int got = ace_sound_get(s, snd, ch, &value);
        uint8_t sample;
        if (got) return got;
        sample = (uint8_t)((int32_t)value + ace_sound_predict(c));
        if (!ace_byte(s, sample)) return -1;
        ace_sound_adjust(c, ace_schar(sample));
    }
    return 0;
}
/* ACE 2.0 PIC mode (mode 7): rows of interleaved colour planes, each pixel
 * predicted from its neighbours by one of four predictors chosen per error
 * context, with the prediction error stored as an adaptive Golomb-Rice code.
 * Ported from acefile.py (class Pic and BitStream.read_golomb_rice),
 * Copyright (C) 2017-2026 Daniel Roethlisberger, BSD 2-Clause licence. */
typedef struct ace_pic_context_s {
    uint64_t average;
    uint32_t used, error[4];
    unsigned predictor;
} ace_pic_context;
typedef struct ace_pic_s {
    ace_pic_context model[2][ACE_PIC_CONTEXTS]; /* plane 0, planes 1..N */
    uint8_t *row, *prev; /* width + planes bytes each; the tail stays zero */
    size_t width, planes;
} ace_pic;
/* Neighbourhood of the current pixel X:   C A D
 *                                         B X
 * kind 0 codes plain values, kinds 1 and 2 code differences to the previous
 * plane of the same pixel. */
typedef struct ace_pixel_s { int32_t a, b, c, d, x; unsigned kind; } ace_pixel;
/* Golomb-Rice code: k remainder bits, then a unary quotient of one bits that
 * a zero bit ends.  Values above limit are refused; one bits are counted 24
 * at a time so long runs cost little. */
static bool ace_rice(ace_bits *b, unsigned k, uint64_t limit, uint64_t *out) {
    uint64_t value = 0;
    if (k > ACE_PIC_MAX_RICE) return false;
    if (k) { value = ace_peek(b, k); ace_skip(b, k); }
    for (;;) {
        uint32_t window = ace_peek(b, 24U);
        unsigned ones = 0;
        if (b->bad) return false;
        while (ones < 24U && (window & (1U << (23U - ones)))) ++ones;
        value += (uint64_t)ones << k;
        if (value > limit) return false;
        ace_skip(b, ones < 24U ? ones + 1U : 24U);
        if (b->bad) return false;
        if (ones < 24U) break;
    }
    *out = value;
    return true;
}
static int32_t ace_pic_quantize(int32_t d) {
    int32_t m = d < 0 ? -d : d, q = m > 20 ? 4 : m > 6 ? 3 : m > 2 ? 2 : m > 0 ? 1 : 0;
    return d < 0 ? -q : q;
}
static int32_t ace_pic_predict(const ace_pixel *p, unsigned predictor) {
    if (predictor == 0U) return p->a;
    if (predictor == 1U) return p->b;
    if (predictor == 2U) return (p->a + p->b) >> 1;
    return (int32_t)((uint32_t)(p->a + p->b - p->c) & 0xFFU);
}
/* Bit width of a prediction error, read as a signed byte and zigzag coded. */
static unsigned ace_pic_error_bits(int32_t delta) {
    unsigned v = (uint32_t)delta & 0xFFU, n = 0;
    v = v < 128U ? 2U * v : 2U * (256U - v) - 1U;
    while (v) { ++n; v >>= 1; }
    return n;
}
static void ace_pic_set_d(ace_pixel *p, int32_t here, int32_t ref) {
    if (p->kind == 1U) here = 128 + here - ref;
    else if (p->kind == 2U) here = 128 + here - ((ref * 11) >> 4);
    p->d = (int32_t)((uint32_t)here & 0xFFU);
}
static uint8_t ace_pic_produce(const ace_pixel *p, int32_t ref) {
    int32_t v = p->x;
    if (p->kind == 1U) v = v + ref - 128;
    else if (p->kind == 2U) v = v + ((ref * 11) >> 4) - 128;
    return (uint8_t)((uint32_t)v & 0xFFU);
}
static bool ace_pic_pixel(ace_bits *b, ace_pixel *p, ace_pic_context *ctx) {
    uint64_t r, value;
    unsigned k = 0, i, best = 0;
    int64_t epsilon;
    ++ctx->used;
    for (r = ctx->average / ctx->used; r; r >>= 1) ++k;
    if (!ace_rice(b, k, ACE_PIC_MAX_VALUE, &value)) return false;
    epsilon = (value & 1U) ? -(int64_t)(value >> 1) - 1 : (int64_t)(value >> 1);
    p->x = (int32_t)((uint64_t)(ace_pic_predict(p, ctx->predictor) + epsilon) & 0xFFU);
    ctx->average += (uint64_t)(epsilon < 0 ? -epsilon : epsilon);
    if (ctx->used == 128U) { ctx->used >>= 1; ctx->average >>= 1; }
    for (i = 0; i < 4U; ++i) {
        ctx->error[i] += ace_pic_error_bits(p->x - ace_pic_predict(p, i));
        if (i == 0U || ctx->error[i] < ctx->error[best]) best = i;
    }
    ctx->predictor = best;
    if (ctx->error[0] > 0x7FU || ctx->error[1] > 0x7FU || ctx->error[2] > 0x7FU ||
        ctx->error[3] > 0x7FU)
        for (i = 0; i < 4U; ++i) ctx->error[i] >>= 1;
    return true;
}
static void ace_pic_free(ace_pic *pic) {
    if (!pic) return;
    xx_mem_free(pic->row);
    xx_mem_free(pic->prev);
    xx_mem_free(pic);
}
/* Entering PIC mode reads the row width and plane count and resets the
 * error models and the previous row. */
static bool ace_pic_reset(ace_state *s, ace_pic *pic) {
    uint64_t width, planes;
    unsigned m, i;
    xx_mem_free(pic->row);
    xx_mem_free(pic->prev);
    xx_rt_memset(pic, 0, sizeof(*pic));
    if (!ace_rice(&s->bits, 12U, ACE_PIC_MAX_WIDTH, &width) ||
        !ace_rice(&s->bits, 2U, ACE_PIC_MAX_PLANES, &planes)) return false;
    pic->width = (size_t)width;
    pic->planes = (size_t)planes;
    pic->row = (uint8_t *)xx_mem_calloc(pic->width + pic->planes + 1U, 1U);
    pic->prev = (uint8_t *)xx_mem_calloc(pic->width + pic->planes + 1U, 1U);
    if (!pic->row || !pic->prev) return false;
    for (m = 0; m < 2U; ++m)
        for (i = 0; i < ACE_PIC_CONTEXTS; ++i) pic->model[m][i].average = 4U;
    return true;
}
/* Decodes one row into pic->row, then makes it the previous row. */
static bool ace_pic_row(ace_state *s, ace_pic *pic) {
    size_t width = pic->width, planes = pic->planes, plane, col;
    uint8_t *row = pic->row, *prev = pic->prev;
    xx_rt_memset(row, 0, width + planes);
    for (plane = 0; plane < planes; ++plane) {
        ace_pic_context *model = pic->model[plane ? 1 : 0];
        ace_pixel p;
        p.kind = 0;
        if (plane) {
            p.kind = ace_peek(&s->bits, 2U);
            ace_skip(&s->bits, 2U);
            if (s->bits.bad || p.kind > 2U) return false;
        }
        p.a = p.b = p.c = p.x = p.kind ? 128 : 0;
        ace_pic_set_d(&p, prev[plane], plane ? prev[plane - 1U] : 0);
        for (col = plane; col < width; col += planes) {
            int32_t context;
            p.c = p.a;
            p.a = p.d;
            p.b = p.x;
            ace_pic_set_d(&p, prev[col + planes], prev[col + planes - 1U]);
            context = 81 * ace_pic_quantize(p.d - p.a) + 9 * ace_pic_quantize(p.a - p.c) +
                      ace_pic_quantize(p.c - p.b);
            if (context < 0) context = -context; /* at most 81*4 + 9*4 + 4 = 364 */
            if ((uint32_t)context >= ACE_PIC_CONTEXTS ||
                !ace_pic_pixel(&s->bits, &p, &model[context]))
                return false;
            row[col] = ace_pic_produce(&p, col ? row[col - 1U] : 0);
        }
    }
    pic->row = prev;
    pic->prev = row;
    return true;
}
/* Decodes PIC rows, each announced by a one bit; a zero bit announces a mode
 * instruction.  Like the reference, a row is decoded in full even when only
 * part of it fits the output.  Returns 1 at a mode instruction, 0 at the
 * output limit and -1 on invalid data. */
static int ace_pic_run(ace_state *s, ace_pic *pic) {
    while (s->output_pos < s->output_size) {
        size_t i, n;
        unsigned flag = ace_peek(&s->bits, 1U);
        ace_skip(&s->bits, 1U);
        if (s->bits.bad) return -1;
        if (!flag) return ace_read_mode(s);
        if (!ace_pic_row(s, pic)) return -1;
        n = s->output_size - s->output_pos;
        if (n > pic->width) n = pic->width;
        for (i = 0; i < n; ++i)
            if (!ace_byte(s, pic->prev[i])) return -1;
    }
    return 0;
}
bool xx_ace_decode_blocked_solid(const void *source, size_t source_size, void *destination,
                                 size_t destination_size, unsigned dictionary_bits,
                                 xx_ace_history *history) {
    ace_state s;
    ace_sound *snd = NULL;
    ace_pic *pic = NULL;
    size_t begin = 0;
    uint8_t mode = 0, last_delta = 0, delta_dist = 0, exe_mode = 0;
    uint32_t delta_len = 0;
    bool ok = false;
    if (!ace_init(&s, source, source_size, destination, destination_size, dictionary_bits))
        return false;
    ace_reuse_history(&s, history);
    while (s.output_pos < destination_size && !s.bits.bad) {
        int got;
        if (mode == ACE_MODE_PIC) got = ace_pic_run(&s, pic);
        else if (mode >= ACE_MODE_SOUND_8) {
            got = ace_sound_run(&s, snd);
            if (got == 0 && s.output_pos < destination_size) got = -1;
        } else got = ace_symbol(&s, true);
        if (got < 0) { s.bits.bad = true; break; }
        if (got == 1) {
            if (mode == 1U && !ace_filter_delta(s.output, begin, s.output_pos, delta_dist,
                                                delta_len, &last_delta)) {
                s.bits.bad = true;
                break;
            }
            if (mode == 2U) ace_filter_exe(s.output, begin, s.output_pos, exe_mode);
            if (s.next_mode > ACE_MODE_PIC) { s.bits.bad = true; break; }
            if (s.next_mode >= ACE_MODE_SOUND_8 && s.next_mode <= ACE_MODE_SOUND_32B &&
                s.next_mode != mode) {
                if (!snd && !(snd = (ace_sound *)xx_mem_alloc(sizeof(*snd)))) {
                    s.bits.bad = true;
                    break;
                }
                ace_sound_reset(snd, s.next_mode - ACE_MODE_SOUND_8);
            }
            /* Like the reference, PIC -> PIC keeps the width and the models. */
            if (s.next_mode == ACE_MODE_PIC && mode != ACE_MODE_PIC) {
                if (!pic && !(pic = (ace_pic *)xx_mem_calloc(1U, sizeof(*pic)))) {
                    s.bits.bad = true;
                    break;
                }
                if (!ace_pic_reset(&s, pic)) { s.bits.bad = true; break; }
            }
            mode = s.next_mode;
            delta_dist = s.delta_dist;
            delta_len = s.delta_len;
            exe_mode = s.exe_mode;
            begin = s.output_pos;
        }
    }
    xx_mem_free(snd);
    ace_pic_free(pic);
    if (!s.bits.bad) {
        if (mode == 1U)
            ok = ace_filter_delta(s.output, begin, s.output_pos, delta_dist, delta_len,
                                  &last_delta);
        else {
            if (mode == 2U) ace_filter_exe(s.output, begin, s.output_pos, exe_mode);
            ok = true;
        }
    }
    ok = ok && s.output_pos == destination_size && s.bits.position <= source_size * 8U &&
         source_size * 8U - s.bits.position < 32U;
    ace_finish_history(&s, history, ok);
    return ok;
}
bool xx_ace_decode_blocked(const void *source,size_t source_size,void *destination,size_t destination_size,unsigned dictionary_bits){return xx_ace_decode_blocked_solid(source,source_size,destination,destination_size,dictionary_bits,NULL);}
