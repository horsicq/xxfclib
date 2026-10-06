/* Copyright 2010-2016 Ratisbon University & Matthias Stirner.
 * Native C adaptation of packMP3 v1.0g by Matthias Stirner.
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Modified for xxfclib: per-call models, bounded arithmetic padding,
 * checked symbol/context indexes and failure propagation.
 */
#define XX_PM_ARITH_LIMIT 0x80000000U

#ifdef XX_MP3_TRACE
#include <stdio.h>
#endif
static void xx_pm_entropy_fail(xx_mp3_context *ctx, int line) {
 if (!ctx) return;
#ifdef XX_MP3_TRACE
 if (!ctx->failed) fprintf(stderr, "packMP3 entropy error at line %d, input %zu/%zu\n",
                           line, ctx->input_position, ctx->input_size);
#else
 (void)line;
#endif
 ctx->failed = true;
}

static void xx_pm_shift_model_model_s_3( model_s* model, int ctx1, int ctx2 ) {
 xx_pm_model_s_shift_context(model,  ctx1 );
 xx_pm_model_s_shift_context(model,  ctx2 );
}
static void xx_pm_shift_model_model_s_4( model_s* model, int ctx1, int ctx2, int ctx3 ) {
 xx_pm_model_s_shift_context(model,  ctx1 );
 xx_pm_model_s_shift_context(model,  ctx2 );
 xx_pm_model_s_shift_context(model,  ctx3 );
}
static void xx_pm_shift_model_model_b_3( model_b* model, int ctx1, int ctx2 ) {
 xx_pm_model_b_shift_context(model,  ctx1 );
 xx_pm_model_b_shift_context(model,  ctx2 );
}
static XXFC_MAYBE_UNUSED void xx_pm_shift_model_model_b_4( model_b* model, int ctx1, int ctx2, int ctx3 ) {
 xx_pm_model_b_shift_context(model,  ctx1 );
 xx_pm_model_b_shift_context(model,  ctx2 );
 xx_pm_model_b_shift_context(model,  ctx3 );
}
static int xx_pm_decode_ari_model_s( aricoder* decoder, model_s* model ) {
 symbol s = {0U, 0U, 0U};
 unsigned int count;
 int c = 0;
 int attempts;
 if (!decoder || !decoder->ctx) return 0;
 if (!model || model->ctx != decoder->ctx || model->error) {
  xx_pm_entropy_fail(decoder->ctx, __LINE__);
  return 0;
 }
 for (attempts = 0; attempts < model->max_order + 2; ++attempts) {
  if (decoder->ctx->failed) return 0;
  xx_pm_model_s_get_symbol_scale(model,  &s );
  if (decoder->ctx->failed) return 0;
  count = xx_pm_aricoder_decode_count(decoder,  &s );
  if (decoder->ctx->failed) return 0;
  c = xx_pm_model_s_convert_symbol_to_int(model,  count, &s );
  if (decoder->ctx->failed) return 0;
  xx_pm_aricoder_decode(decoder,  &s );
  if (decoder->ctx->failed) return 0;
  if (c != (int)(XX_PM_ARITH_LIMIT / 4U)) {
   xx_pm_model_s_update_model(model, c);
   return decoder->ctx->failed ? 0 : c;
  }
 }
 xx_pm_entropy_fail(decoder->ctx, __LINE__);
 return 0;
}
static int xx_pm_decode_ari_model_b( aricoder* decoder, model_b* model ) {
 symbol s = {0U, 0U, 0U};
 unsigned int count;
 int c;
 if (!decoder || !decoder->ctx) return 0;
 if (!model || model->ctx != decoder->ctx || model->error) {
  xx_pm_entropy_fail(decoder->ctx, __LINE__);
  return 0;
 }
 if (decoder->ctx->failed) return 0;
 xx_pm_model_b_get_symbol_scale(model,  &s );
 if (decoder->ctx->failed) return 0;
 count = xx_pm_aricoder_decode_count(decoder,  &s );
 if (decoder->ctx->failed) return 0;
 c = xx_pm_model_b_convert_symbol_to_int(model,  count, &s );
 if (decoder->ctx->failed) return 0;
 xx_pm_aricoder_decode(decoder,  &s );
 if (decoder->ctx->failed) return 0;
 xx_pm_model_b_update_model(model,  c );
 return decoder->ctx->failed ? 0 : c;
}

#define XX_PM_SHIFT_SELECT(_a,_b,_c,_d,_fn,...) _fn
#define xx_pm_shift_model_model_s(...) XX_PM_SHIFT_SELECT(__VA_ARGS__,xx_pm_shift_model_model_s_4,xx_pm_shift_model_model_s_3)(__VA_ARGS__)
#define xx_pm_shift_model_model_b(...) XX_PM_SHIFT_SELECT(__VA_ARGS__,xx_pm_shift_model_model_b_4,xx_pm_shift_model_model_b_3)(__VA_ARGS__)
static void xx_pm_aricoder_init(aricoder *self, iostream* stream, int iomode) {
 int i;
 if (!stream || stream->ctx != self->ctx || (iomode != 0 && iomode != 1)) {
  xx_pm_entropy_fail(self->ctx, __LINE__);
  return;
 }
 self->ccode = 0;
 self->clow = 0;
 self->chigh = XX_PM_ARITH_LIMIT - 1;
 self->cstep = 0;
 self->bbyte = 0;
 self->cbit = 0;
 self->nrbits = 0;
 self->padding_bytes = 0U;
 self->sptr = stream;
 self->mode = iomode;
 if ( self->mode == 0 ) {
  for ( i = 0; i < 31; i++ )
   self->ccode = ( self->ccode << 1 ) | xx_pm_aricoder_read_bit(self);
 }
}

static aricoder *xx_pm_aricoder_new(xx_mp3_context *ctx, iostream* stream, int iomode) {aricoder *self = (aricoder *)xx_pm_calloc(ctx, 1, sizeof(*self));
 if (!self) { if (ctx) xx_pm_entropy_fail(ctx, __LINE__); return NULL; }
 self->ctx = ctx;
 xx_pm_aricoder_init(self, stream, iomode);
 if (ctx->failed) { xx_pm_free(ctx, self); return NULL; }
 return self;}

static void xx_pm_aricoder_destroy(aricoder *self) {
 if (!self || !self->ctx || self->ctx->failed) return;
 if ( self->mode == 1 ) {
  if ( self->clow < ( ( XX_PM_ARITH_LIMIT / 4 ) * 1 ) ) {
   xx_pm_aricoder_write_bit(self, 0 );
   xx_pm_aricoder_write_bit(self, 1 );
   while ( self->nrbits-- > 0 )
    xx_pm_aricoder_write_bit(self, 1 );
  }
  else {
   xx_pm_aricoder_write_bit(self, 1 );
  }
  while ( self->cbit > 0 ) xx_pm_aricoder_write_bit(self, 0 );
 }
}

static void xx_pm_aricoder_delete(aricoder *self) {if (self) { xx_pm_aricoder_destroy(self); xx_pm_free(self->ctx, self); }}

static XXFC_MAYBE_UNUSED void xx_pm_aricoder_encode(aricoder *self, symbol* s) {
 if (!self || !self->ctx || self->ctx->failed) return;
 if (!s || !s->scale || s->scale >= XX_PM_ARITH_LIMIT / 4U ||
     s->low_count >= s->high_count || s->high_count > s->scale ||
     self->clow > self->chigh || self->chigh >= XX_PM_ARITH_LIMIT) {
  xx_pm_entropy_fail(self->ctx, __LINE__); return;
 }
 self->cstep = ( ( self->chigh - self->clow ) + 1 ) / s->scale;
 if (!self->cstep) { xx_pm_entropy_fail(self->ctx, __LINE__); return; }
 self->chigh = self->clow + ( self->cstep * s->high_count ) - 1;
 self->clow = self->clow + ( self->cstep * s->low_count );
 while ( ( self->clow >= ( ( XX_PM_ARITH_LIMIT / 4 ) * 2 ) ) || ( self->chigh < ( ( XX_PM_ARITH_LIMIT / 4 ) * 2 ) ) ) {
  if ( self->chigh < ( ( XX_PM_ARITH_LIMIT / 4 ) * 2 ) ) {
   xx_pm_aricoder_write_bit(self, 0 );
   for ( ; self->nrbits > 0; self->nrbits-- )
    xx_pm_aricoder_write_bit(self, 1 );
  }
  else {
   xx_pm_aricoder_write_bit(self, 1 );
   self->clow &= ( ( XX_PM_ARITH_LIMIT / 4 ) * 2 ) - 1;
   self->chigh &= ( ( XX_PM_ARITH_LIMIT / 4 ) * 2 ) - 1;
   for ( ; self->nrbits > 0; self->nrbits-- )
    xx_pm_aricoder_write_bit(self, 0 );
  }
  self->clow <<= 1;
  self->chigh <<= 1;
  self->chigh++;
 }
 while ( ( self->clow >= ( ( XX_PM_ARITH_LIMIT / 4 ) * 1 ) ) && ( self->chigh < ( ( XX_PM_ARITH_LIMIT / 4 ) * 3 ) ) ) {
  self->nrbits++;
  self->clow &= ( ( XX_PM_ARITH_LIMIT / 4 ) * 1 ) - 1;
  self->chigh ^= ( ( XX_PM_ARITH_LIMIT / 4 ) * 1 ) + ( ( XX_PM_ARITH_LIMIT / 4 ) * 2 );
  self->clow <<= 1;
  self->chigh <<= 1;
  self->chigh++;
 }
}

static unsigned int xx_pm_aricoder_decode_count(aricoder *self, symbol* s) {
 unsigned int count;
 if (!self || !self->ctx || self->ctx->failed) return 0U;
 if (!s || s->scale == 0U || s->scale >= XX_PM_ARITH_LIMIT / 4U ||
     self->clow > self->chigh || self->chigh >= XX_PM_ARITH_LIMIT ||
     self->ccode < self->clow || self->ccode > self->chigh) {
  xx_pm_entropy_fail(self->ctx, __LINE__);
  return 0U;
 }
 self->cstep = ( ( self->chigh - self->clow ) + 1 ) / s->scale;
 if (self->cstep == 0U) { xx_pm_entropy_fail(self->ctx, __LINE__); return 0U; }
 count = (self->ccode - self->clow) / self->cstep;
 if (count >= s->scale) { xx_pm_entropy_fail(self->ctx, __LINE__); return 0U; }
 return count;
}

static void xx_pm_aricoder_decode(aricoder *self, symbol* s) {
 uint64_t low, high;
 if (!self || !self->ctx || self->ctx->failed) return;
 if (!s || !self->cstep || !s->scale ||
     s->low_count >= s->high_count || s->high_count > s->scale) {
  xx_pm_entropy_fail(self->ctx, __LINE__);
  return;
 }
 low = (uint64_t)self->clow + (uint64_t)self->cstep * s->low_count;
 high = (uint64_t)self->clow + (uint64_t)self->cstep * s->high_count - 1U;
 if (high > self->chigh || low > high ||
     self->ccode < low || self->ccode > high) {
  xx_pm_entropy_fail(self->ctx, __LINE__);
  return;
 }
 self->chigh = self->clow + ( self->cstep * s->high_count ) - 1;
 self->clow = self->clow + ( self->cstep * s->low_count );
 while ( ( self->clow >= ( ( XX_PM_ARITH_LIMIT / 4 ) * 2 ) ) || ( self->chigh < ( ( XX_PM_ARITH_LIMIT / 4 ) * 2 ) ) ) {
  if ( self->clow >= ( ( XX_PM_ARITH_LIMIT / 4 ) * 2 ) ) {
   self->clow &= ( ( XX_PM_ARITH_LIMIT / 4 ) * 2 ) - 1;
   self->chigh &= ( ( XX_PM_ARITH_LIMIT / 4 ) * 2 ) - 1;
   self->ccode &= ( ( XX_PM_ARITH_LIMIT / 4 ) * 2 ) - 1;
  }
  self->clow <<= 1;
  self->chigh <<= 1;
  self->chigh++;
  self->ccode <<= 1;
  self->ccode |= xx_pm_aricoder_read_bit(self);
  if (self->ctx->failed) return;
  self->nrbits = 0;
 }
 while ( ( self->clow >= ( ( XX_PM_ARITH_LIMIT / 4 ) * 1 ) ) && ( self->chigh < ( ( XX_PM_ARITH_LIMIT / 4 ) * 3 ) ) ) {
  self->nrbits++;
  self->clow &= ( ( XX_PM_ARITH_LIMIT / 4 ) * 1 ) - 1;
  self->chigh ^= ( ( XX_PM_ARITH_LIMIT / 4 ) * 1 ) + ( ( XX_PM_ARITH_LIMIT / 4 ) * 2 );
  self->ccode -= ( ( XX_PM_ARITH_LIMIT / 4 ) * 1 );
  self->clow <<= 1;
  self->chigh <<= 1;
  self->chigh++;
  self->ccode <<= 1;
  self->ccode |= xx_pm_aricoder_read_bit(self);
  if (self->ctx->failed) return;
 }
}

static void xx_pm_aricoder_write_bit(aricoder *self, unsigned char bit) {
 if (!self || !self->ctx || self->ctx->failed) return;
 self->bbyte = ( self->bbyte << 1 ) | bit;
 self->cbit++;
 if ( self->cbit == 8 ) {
  xx_pm_iostream_write(self->sptr,  (void*) &self->bbyte, 1, 1 );
  self->cbit = 0;
 }
}

static unsigned char xx_pm_aricoder_read_bit(aricoder *self) {
 if (!self || !self->ctx || self->ctx->failed) return 0U;
 if ( self->cbit == 0 ) {
  if (self->ctx->input_position < self->ctx->input_size) {
   if (xx_pm_iostream_read(self->sptr, &self->bbyte, 1, 1) != 1) {
    xx_pm_entropy_fail(self->ctx, __LINE__);
    return 0U;
   }
  } else {
   /* The arithmetic format defines implicit zero termination bits. */
   if (++self->padding_bytes > 4U) { xx_pm_entropy_fail(self->ctx, __LINE__); return 0U; }
   self->bbyte = 0U;
  }
  self->cbit = 8;
 }
 self->cbit--;
 return ( (self->bbyte >> self->cbit) & 0x1 );
}

static void xx_pm_model_s_init(model_s *self, int max_s, int max_c, int max_o, int c_lim) {
 table_s* null_table;
 table_s* start_table;
 int i;
 self->error = false;
 if (max_s < 1 || max_s > 1024 || max_c < 0 || max_c > 1024 ||
     max_o < -1 || max_o > 4 || (max_o > 0 && max_c == 0) ||
     c_lim < 2 || c_lim > 65535) {
  self->error = true;
  xx_pm_entropy_fail(self->ctx, __LINE__);
  return;
 }
 self->max_symbol = max_s;
 self->max_context = max_c;
 self->max_order = max_o;
 self->max_count = c_lim;
 self->totals = ( unsigned int* ) xx_pm_calloc(self->ctx,  self->max_symbol + 2, sizeof( int ) );
 self->scoreboard = ( int8_t* ) xx_pm_calloc(self->ctx,  self->max_symbol, sizeof( int8_t ) );
 if (!self->totals || !self->scoreboard) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; }
 self->sb0_count = self->max_symbol;
 self->current_order = self->max_order;
 null_table = ( table_s* ) xx_pm_calloc(self->ctx,  1, sizeof( table_s ) );
 if ( null_table == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
 null_table->counts = ( unsigned short* ) xx_pm_calloc(self->ctx,  self->max_symbol, sizeof( short ) );
 if ( null_table->counts == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
 for ( i = 0; i < self->max_symbol; i++ )
  null_table->counts[ i ] = 1;
 null_table->max_count = 1;
 null_table->max_symbol = (unsigned short)self->max_symbol;
 start_table = ( table_s* ) xx_pm_calloc(self->ctx,  1, sizeof( table_s ) );
    if ( start_table == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
 start_table->links = self->max_context ? (table_s **)xx_pm_calloc(self->ctx, self->max_context, sizeof(table_s *)) : NULL;
 if ( self->max_context && start_table->links == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
 start_table->max_count = 0;
 start_table->max_symbol = 0;
 start_table->lesser = null_table;
 null_table->links = self->max_context ? (table_s **)xx_pm_calloc(self->ctx, self->max_context, sizeof(table_s *)) : NULL;
 if ( self->max_context && null_table->links == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
 for ( i = 0; i < self->max_context; i++ )
  null_table->links[ i ] = start_table;
 self->storage = ( table_s** ) xx_pm_calloc(self->ctx,  self->max_order + 3, sizeof( table_s* ) );
 if ( self->storage == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
 self->contexts = self->storage + 1;
 self->contexts[ -1 ] = null_table;
 self->contexts[ 0 ] = start_table;
 for ( i = 1; i <= self->max_order; i++ ) {
  self->contexts[ i ] = ( table_s* ) xx_pm_calloc(self->ctx,  1, sizeof( table_s ) );
     if ( self->contexts[ i ] == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
  self->contexts[ i ]->max_count = 0;
  self->contexts[ i ]->max_symbol = 0;
  self->contexts[ i ]->lesser = self->contexts[ i - 1 ];
  if ( i < self->max_order ) {
   self->contexts[ i ]->links = ( table_s** ) xx_pm_calloc(self->ctx,  self->max_context, sizeof( table_s* ) );
   if ( self->contexts[ i ]->links == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
  }
  else {
   self->contexts[ i ]->links = NULL;
  }
  self->contexts[ i - 1 ]->links[ 0 ] = self->contexts[ i ];
 }
}

static model_s *xx_pm_model_s_new(xx_mp3_context *ctx, int max_s, int max_c, int max_o, int c_lim) {model_s *self = (model_s *)xx_pm_calloc(ctx, 1, sizeof(*self));
 if (!self) { if (ctx) xx_pm_entropy_fail(ctx, __LINE__); return NULL; }
 self->ctx = ctx;
 xx_pm_model_s_init(self, max_s, max_c, max_o, c_lim);
 if (ctx->failed) { xx_pm_free(ctx, self); return NULL; }
 return self;}

static void xx_pm_model_s_destroy(model_s *self) {
 table_s* context;
 if (!self || !self->contexts) return;
 context = self->contexts[ 0 ];
 xx_pm_model_s_recursive_cleanup(self, context );
 context = self->contexts[ -1 ];
 if (!context) return;
 if ( context->links != NULL )
  xx_pm_free(self->ctx,  context->links );
 if ( context->counts != NULL ) xx_pm_free(self->ctx,  context->counts );
 xx_pm_free(self->ctx,  context );
 xx_pm_free(self->ctx,  self->storage );
 xx_pm_free(self->ctx,  self->totals );
 xx_pm_free(self->ctx,  self->scoreboard );
}

static void xx_pm_model_s_delete(model_s *self) {if (self) { xx_pm_model_s_destroy(self); xx_pm_free(self->ctx, self); }}

static void xx_pm_model_s_update_model(model_s *self, int symbol) {
 table_s* context;
 unsigned short* counts;
 int local_order;
 int i;
 if (!self || !self->ctx || self->ctx->failed) return;
 if (!self->contexts || !self->scoreboard || symbol < -1 ||
     symbol >= self->max_symbol || self->current_order < -1 ||
     self->current_order > self->max_order) {
  self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return;
 }
 if ( symbol >= 0 ) {
  for ( local_order = ( self->current_order < 0 ) ? 0 : self->current_order;
    local_order <= self->max_order; local_order++ ) {
   context = self->contexts[ local_order ];
   if (!context || !context->counts) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; }
   counts = context->counts + symbol;
   (*counts)++;
   if ( (*counts) > context->max_count ) context->max_count = (*counts);
   if ( symbol >= context->max_symbol ) context->max_symbol = (unsigned short)(symbol+1);
   if ( (*counts) >= self->max_count )
    xx_pm_model_s_rescale_table(self, context, 1 );
  }
 }
 self->current_order = self->max_order;
 for ( i = 0; i < self->max_symbol; i++ )
  self->scoreboard[ i ] = 0;
 self->sb0_count = self->max_symbol;
}

static void xx_pm_model_s_shift_context(model_s *self, int c) {
 table_s* context;
 int i;
 if (!self || !self->ctx || self->ctx->failed) return;
 if ( ( self->max_order < 1 ) || ( c < 0 ) ) return;
 if (c >= self->max_context || !self->contexts) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; }
 for ( i = self->max_order; i > 0; i-- ) {
  if (!self->contexts[i-1] || !self->contexts[i-1]->links ||
      !self->contexts[i-2] || !self->contexts[i-2]->links) {
   self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return;
  }
  context = self->contexts[ i - 1 ]->links[ c ];
  if ( context == NULL ) {
   context = ( table_s* ) xx_pm_calloc(self->ctx,  1, sizeof( table_s ) );
   if ( context == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
   context->counts = NULL;
   context->max_count = 0;
   context->max_symbol = 0;
   context->lesser = self->contexts[ i - 2 ]->links[ c ];
   if ( i == self->max_order )
    context->links = NULL;
   else {
    context->links = ( table_s** ) xx_pm_calloc(self->ctx,  self->max_context, sizeof( table_s* ) );
    if ( context->links == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
    self->contexts[ i + 1 ]->lesser = context;
   }
   self->contexts[ i - 1 ]->links[ c ] = context;
  }
  self->contexts[ i ] = context;
 }
}

static void xx_pm_model_s_flush_model(model_s *self, int scale_factor) {
 if (!self || !self->ctx || self->ctx->failed) return;
 if (scale_factor < 0 || scale_factor > 15 || !self->contexts) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; }
 xx_pm_model_s_recursive_flush(self, self->contexts[ 0 ], scale_factor );
}

static XXFC_MAYBE_UNUSED void xx_pm_model_s_exclude_symbols(model_s *self, char rule, int c) {
 if (!self || !self->ctx || self->ctx->failed) return;
 if (!self->scoreboard || c < -1 || c > self->max_symbol ||
     (rule == 'e' && (c < 0 || c >= self->max_symbol))) {
  self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return;
 }
 switch ( rule )
 {
  case 'a':
   for ( c = c + 1; c < self->max_symbol; c++ ) {
    if ( self->scoreboard[ c ] == 0 ) {
     self->scoreboard[ c ] = 1;
     self->sb0_count--;
    }
   }
   break;
  case 'b':
   for ( c = c - 1; c >= 0; c-- ) {
    if ( self->scoreboard[ c ] == 0 ) {
     self->scoreboard[ c ] = 1;
     self->sb0_count--;
    }
   }
   break;
  case 'e':
   if ( self->scoreboard[ c ] == 0 ) {
    self->scoreboard[ c ] = 1;
    self->sb0_count--;
   }
   break;
  default:
   break;
 }
}

static XXFC_MAYBE_UNUSED int xx_pm_model_s_convert_int_to_symbol(model_s *self, int c, symbol *s) {
 table_s* context;
 if (!self || !self->ctx || self->ctx->failed) return 0;
 if (!s || !self->contexts || c < -1 || c >= self->max_symbol ||
     self->current_order < -1 || self->current_order > self->max_order) {
  self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return 0;
 }
 context = self->contexts[ self->current_order ];
 xx_pm_model_s_totalize_table(self, context );
 if (self->ctx->failed) return 0;
 s->scale = self->totals[ 0 ];
 if ( c >= 0 ) {
  if ( context->counts[ c ] > 0 ) {
   s->low_count = self->totals[ c + 2 ];
   s->high_count = self->totals[ c + 1 ];
   return 0;
  }
 }
 s->low_count = self->totals[ 1 ];
 s->high_count = self->totals[ 0 ];
 self->current_order--;
 if (self->current_order < -1) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return 0; }
 return 1;
}

static void xx_pm_model_s_get_symbol_scale(model_s *self, symbol *s) {
 if (!self || !self->ctx || self->ctx->failed) return;
 if (!s || !self->contexts || self->current_order < -1 || self->current_order > self->max_order) {
  self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return;
 }
 xx_pm_model_s_totalize_table(self, self->contexts[ self->current_order ] );
 s->scale = self->totals[ 0 ];
}

static int xx_pm_model_s_convert_symbol_to_int(model_s *self, int count, symbol *s) {
 int c;
 if (!self || !self->ctx || self->ctx->failed) return 0;
 if (!s || !self->totals || count < 0 || (unsigned)count >= self->totals[0]) {
  self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return 0;
 }
 for (c = 1; c <= self->max_symbol + 1 && (unsigned)count < self->totals[c]; ++c) {}
 if (c > self->max_symbol + 1 || self->totals[c] >= self->totals[c-1]) {
  self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return 0;
 }
 s->low_count = self->totals[ c ];
 s->high_count = self->totals[ c - 1 ];
 if ( c == 1 ) {
  self->current_order--;
  if (self->current_order < -1) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return 0; }
  return ( ( XX_PM_ARITH_LIMIT / 4 ) * 1 );
 }
 return ( c - 2 );
}

static void xx_pm_model_s_totalize_table(model_s *self, table_s *context) {
 unsigned short* counts;
 signed int local_symb;
 unsigned int curr_total;
 unsigned int curr_count;
 unsigned int esc_prob;
 int i;
 if (!self || !self->ctx || self->ctx->failed) return;
 if (!context || !self->totals || !self->scoreboard ||
     context->max_symbol > self->max_symbol ||
     self->sb0_count < 0 || self->sb0_count > self->max_symbol) {
  self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return;
 }
 counts = context->counts;
 if ( counts != NULL ) {
  local_symb = self->sb0_count;
  i = context->max_symbol - 1;
  self->totals[ i + 2 ] = 0;
  curr_total = 0;
  for ( ; i >= 0; i-- ) {
   if ( self->scoreboard[ i ] == 0 ) {
    curr_count = counts[ i ];
    if ( curr_count > 0 ) {
     curr_total = curr_total + curr_count;
     self->scoreboard[ i ] = 1;
     self->sb0_count--;
    }
   }
   self->totals[ i + 1 ] = curr_total;
  }
  if ( local_symb == self->sb0_count )
   esc_prob = 1;
  else if ( self->sb0_count == 0 )
   esc_prob = 0;
  else {
   if (local_symb <= 0 || context->max_count == 0U) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; }
   esc_prob = self->sb0_count * ( local_symb - self->sb0_count );
   esc_prob /= ( local_symb * context->max_count );
   esc_prob++;
  }
  self->totals[ 0 ] = self->totals[ 1 ] + esc_prob;
 }
 else {
  context->counts = ( unsigned short* ) xx_pm_calloc(self->ctx,  self->max_symbol, sizeof( short ) );
  if ( context->counts == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
  self->totals[ 0 ] = 1;
  self->totals[ 1 ] = 0;
 }
}

static void xx_pm_model_s_rescale_table(model_s *self, table_s* context, int scale_factor) {
 if (!self || !self->ctx || self->ctx->failed) return;
 if (!context || context->max_symbol > self->max_symbol || scale_factor < 0 || scale_factor > 15) {
  self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return;
 }
 unsigned short* counts = context->counts;
 int lst_symbol = context->max_symbol;
 int i;
 if ( counts == NULL ) return;
 for ( i = 0; i < lst_symbol; i++ ) {
  if ( counts[ i ] > 0 )
   counts[ i ] >>= scale_factor;
 }
 context->max_count >>= scale_factor;
 for ( i = lst_symbol - 1; i >= 0; i-- )
  if ( counts[ i ] > 0 ) break;
 context->max_symbol = (unsigned short)(i + 1);
}

static void xx_pm_model_s_recursive_flush(model_s *self, table_s* context, int scale_factor) {
 int i;
 if (!context || self->ctx->failed) return;
 if ( context->links != NULL )
  for ( i = 0; i < self->max_context; i++ )
   if ( context->links[ i ] != NULL )
    xx_pm_model_s_recursive_flush(self, context->links[ i ], scale_factor );
 xx_pm_model_s_rescale_table(self, context, scale_factor );
}

static void xx_pm_model_s_recursive_cleanup(model_s *self, table_s *context) {
 int i;
 if (!context) return;
 if ( context->links != NULL ) {
  for ( i = 0; i < self->max_context; i++ )
   if ( context->links[ i ] != NULL )
    xx_pm_model_s_recursive_cleanup(self, context->links[ i ] );
  xx_pm_free(self->ctx,  context->links );
 }
 if ( context->counts != NULL ) xx_pm_free(self->ctx,  context->counts );
 xx_pm_free(self->ctx,  context );
}

static void xx_pm_model_b_init(model_b *self, int max_c, int max_o, int c_lim) {
 table* null_table;
 table* start_table;
 int i;
 self->error = false;
 if (max_c < 0 || max_c > 1024 || max_o < -1 || max_o > 4 ||
     (max_o > 0 && max_c == 0) || c_lim < 2 || c_lim > 65535) {
  self->error = true;
  xx_pm_entropy_fail(self->ctx, __LINE__);
  return;
 }
 self->max_context = max_c;
 self->max_order = max_o;
 self->max_count = c_lim;
 null_table = ( table* ) xx_pm_calloc(self->ctx,  1, sizeof( table ) );
 if ( null_table == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
 null_table->counts = ( unsigned short* ) xx_pm_calloc(self->ctx,  2, sizeof( short ) );
 if ( null_table->counts == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
 null_table->counts[ 0 ] = 1;
 null_table->counts[ 1 ] = 1;
 null_table->scale = 2;
 start_table = ( table* ) xx_pm_calloc(self->ctx,  1, sizeof( table ) );
    if ( start_table == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
 start_table->links = self->max_context ? (table **)xx_pm_calloc(self->ctx, self->max_context, sizeof(table *)) : NULL;
 if ( self->max_context && start_table->links == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
 start_table->scale = 0;
 start_table->lesser = null_table;
 null_table->links = self->max_context ? (table **)xx_pm_calloc(self->ctx, self->max_context, sizeof(table *)) : NULL;
 if ( self->max_context && null_table->links == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
 for ( i = 0; i < self->max_context; i++ )
  null_table->links[ i ] = start_table;
 self->storage = ( table** ) xx_pm_calloc(self->ctx,  self->max_order + 3, sizeof( table* ) );
 if ( self->storage == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
 self->contexts = self->storage + 1;
 self->contexts[ -1 ] = null_table;
 self->contexts[ 0 ] = start_table;
 for ( i = 1; i <= self->max_order; i++ ) {
  self->contexts[ i ] = ( table* ) xx_pm_calloc(self->ctx,  1, sizeof( table ) );
     if ( self->contexts[ i ] == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
  self->contexts[ i ]->scale = 0;
  self->contexts[ i ]->lesser = self->contexts[ i - 1 ];
  if ( i < self->max_order ) {
   self->contexts[ i ]->links = ( table** ) xx_pm_calloc(self->ctx,  self->max_context, sizeof( table* ) );
   if ( self->contexts[ i ]->links == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
  }
  else {
   self->contexts[ i ]->links = NULL;
  }
  self->contexts[ i - 1 ]->links[ 0 ] = self->contexts[ i ];
 }
}

static model_b *xx_pm_model_b_new(xx_mp3_context *ctx, int max_c, int max_o, int c_lim) {model_b *self = (model_b *)xx_pm_calloc(ctx, 1, sizeof(*self));
 if (!self) { if (ctx) xx_pm_entropy_fail(ctx, __LINE__); return NULL; }
 self->ctx = ctx;
 xx_pm_model_b_init(self, max_c, max_o, c_lim);
 if (ctx->failed) { xx_pm_free(ctx, self); return NULL; }
 return self;}

static void xx_pm_model_b_destroy(model_b *self) {
 table* context;
 if (!self || !self->contexts) return;
 context = self->contexts[ 0 ];
 xx_pm_model_b_recursive_cleanup(self, context );
 context = self->contexts[ -1 ];
 if (!context) return;
 if ( context->links != NULL )
  xx_pm_free(self->ctx,  context->links );
 if ( context->counts != NULL ) xx_pm_free(self->ctx,  context->counts );
 xx_pm_free(self->ctx,  context );
 xx_pm_free(self->ctx,  self->storage );
}

static void xx_pm_model_b_delete(model_b *self) {if (self) { xx_pm_model_b_destroy(self); xx_pm_free(self->ctx, self); }}

static void xx_pm_model_b_update_model(model_b *self, int symbol) {
 table* context;
 if (!self || !self->ctx || self->ctx->failed) return;
 if (!self->contexts || symbol < -1 || symbol > 1) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; }
 context = self->contexts[ self->max_order ];
 if (!context || !context->counts) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; }
 if ( ( symbol >= 0 ) && ( self->max_order >= 0 ) ) {
  context->counts[ symbol ]++;
  context->scale++;
  if ( context->counts[ symbol ] >= self->max_count )
   xx_pm_model_b_rescale_table(self, context, 1 );
 }
}

static void xx_pm_model_b_shift_context(model_b *self, int c) {
 table* context;
 int i;
 if (!self || !self->ctx || self->ctx->failed) return;
 if ( ( self->max_order < 1 ) || ( c < 0 ) ) return;
 if (c >= self->max_context || !self->contexts) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; }
 for ( i = self->max_order; i > 0; i-- ) {
  if (!self->contexts[i-1] || !self->contexts[i-1]->links ||
      !self->contexts[i-2] || !self->contexts[i-2]->links) {
   self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return;
  }
  context = self->contexts[ i - 1 ]->links[ c ];
  if ( context == NULL ) {
   context = ( table* ) xx_pm_calloc(self->ctx,  1, sizeof( table ) );
   if ( context == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
   context->counts = NULL;
   context->scale = 0;
   context->lesser = self->contexts[ i - 2 ]->links[ c ];
   if ( i == self->max_order ) {
    context->links = NULL;
   }
   else {
    context->links = ( table** ) xx_pm_calloc(self->ctx,  self->max_context, sizeof( table* ) );
    if ( context->links == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
    self->contexts[ i + 1 ]->lesser = context;
   }
   self->contexts[ i - 1 ]->links[ c ] = context;
  }
  self->contexts[ i ] = context;
 }
}

static void xx_pm_model_b_flush_model(model_b *self, int scale_factor) {
 if (!self || !self->ctx || self->ctx->failed) return;
 if (scale_factor < 0 || scale_factor > 15 || !self->contexts) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; }
 xx_pm_model_b_recursive_flush(self, self->contexts[ 0 ], scale_factor );
}

static XXFC_MAYBE_UNUSED int xx_pm_model_b_convert_int_to_symbol(model_b *self, int c, symbol *s) {
 table* context;
 if (!self || !self->ctx || self->ctx->failed) return 0;
 if (!s || !self->contexts || c < 0 || c > 1) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return 0; }
 context = self->contexts[ self->max_order ];
 xx_pm_model_b_check_counts(self, context );
 if (self->ctx->failed) return 0;
 s->scale = context->scale;
 if ( c == 0 ) {
  s->low_count = 0;
  s->high_count = context->counts[ 0 ];
 }
 else {
  s->low_count = context->counts[ 0 ];
  s->high_count = context->scale;
 }
 return 1;
}

static void xx_pm_model_b_get_symbol_scale(model_b *self, symbol *s) {
 table* context;
 if (!self || !self->ctx || self->ctx->failed) return;
 if (!s || !self->contexts) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; }
 context = self->contexts[ self->max_order ];
 xx_pm_model_b_check_counts(self, context );
 s->scale = context->scale;
}

static int xx_pm_model_b_convert_symbol_to_int(model_b *self, int count, symbol *s) {
 table* context;
 unsigned short counts0;
 if (!self || !self->ctx || self->ctx->failed) return 0;
 if (!s || !self->contexts || count < 0 || (unsigned)count >= s->scale) {
  self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return 0;
 }
 context = self->contexts[ self->max_order ];
 if (!context || !context->counts) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return 0; }
 counts0 = context->counts[ 0 ];
 if ( count < counts0 ) {
  s->low_count = 0;
  s->high_count = counts0;
  return 0;
 }
 else {
  s->low_count = counts0;
  s->high_count = s->scale;
  return 1;
 }
}

static void xx_pm_model_b_check_counts(model_b *self, table *context) {
 if (!self || !self->ctx || self->ctx->failed) return;
 if (!context) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; }
 unsigned short* counts = context->counts;
 if ( counts == NULL ) {
  counts = ( unsigned short* ) xx_pm_calloc(self->ctx,  2, sizeof( short ) );
  if ( counts == NULL ) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; };
  counts[ 0 ] = 1;
  counts[ 1 ] = 1;
  context->counts = counts;
  context->scale = 2;
 }
}

static void xx_pm_model_b_rescale_table(model_b *self, table* context, int scale_factor) {
 if (!self || !self->ctx || self->ctx->failed) return;
 if (!context || scale_factor < 0 || scale_factor > 15) { self->error = true; xx_pm_entropy_fail(self->ctx, __LINE__); return; }
 unsigned short* counts = context->counts;
 if ( counts == NULL ) return;
 counts[ 0 ] >>= scale_factor;
 counts[ 1 ] >>= scale_factor;
 if ( counts[ 0 ] == 0 ) counts[ 0 ] = 1;
 if ( counts[ 1 ] == 0 ) counts[ 1 ] = 1;
 context->scale = counts[ 0 ] + counts[ 1 ];
}

static void xx_pm_model_b_recursive_flush(model_b *self, table* context, int scale_factor) {
 int i;
 if (!context || self->ctx->failed) return;
 if ( context->links != NULL )
  for ( i = 0; i < self->max_context; i++ )
   if ( context->links[ i ] != NULL )
    xx_pm_model_b_recursive_flush(self, context->links[ i ], scale_factor );
 xx_pm_model_b_rescale_table(self, context, scale_factor );
}

static void xx_pm_model_b_recursive_cleanup(model_b *self, table *context) {
 int i;
 if (!context) return;
 if ( context->links != NULL ) {
  for ( i = 0; i < self->max_context; i++ )
   if ( context->links[ i ] != NULL )
    xx_pm_model_b_recursive_cleanup(self, context->links[ i ] );
  xx_pm_free(self->ctx,  context->links );
 }
 if ( context->counts != NULL ) xx_pm_free(self->ctx,  context->counts );
 xx_pm_free(self->ctx,  context );
}
