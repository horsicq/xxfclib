/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/kafka_record_batch/xx_kafka_record_batch.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_KAFKA_RECORD_BATCH};
static Abstractformat *open_reader(xx_io_device *d) { xx_kafka_record_batch *r=xx_kafka_record_batch_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_kafka_record_batch_free((xx_kafka_record_batch *)f); }
static const xx_format_search_desc desc = {types,1U,NULL,0U,open_reader,close_reader};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_kafka_record_batch_extractor = {create_search,current_search,next_search,free_search};
