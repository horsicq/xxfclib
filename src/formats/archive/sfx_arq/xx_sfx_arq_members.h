/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT. Private native ARQ member delegation. */
typedef struct arqm_cursor { xx_arq *reader; xx_archive_record_state *nested; } arqm_cursor;
static xx_arq *arqm_open(Abstractformat *f,int64_t *extent,xx_pd_struct *pd)
{
    pm_stream *p=pm_open(f,pd); if(!p) return NULL;
    xx_arq *r=p->count==1?xx_arq_create(f->device,p->items[0].offset):NULL;
    if(!r || carrier_stop(pd) || !xx_arq_handle_base_info(&r->format,pd) ||
       r->format.format_size!=p->items[0].size || !r->format.number_of_archive_records) {
        if(r) xx_arq_free(r);
        pm_free_stream(p); return NULL;
    }
    *extent=p->size; pm_free_stream(p); return r;
}
static int64_t arqm_size(Abstractformat *f,xx_pd_struct *pd) { return f && !carrier_stop(pd) && (f->base_info_handled || xx_sfx_arq_handle_base_info(f,pd))?f->format_size:-1; }
static uint64_t arqm_count(Abstractformat *f,xx_pd_struct *pd) { return arqm_size(f,pd)>=0?f->number_of_archive_records:0; }
static bool arqm_options(xx_list_s *dst,const xx_list_s *src)
{
    xx_list_clear(dst);
    for(size_t i=0;src && i<src->count;i++) {
        const xx_meta *m=(const xx_meta *)xx_list_at(src,i); xx_meta copy;
        if(!m) continue;
        xx_meta_init(&copy,m->meta_id);
        if(!xx_var_copy(&copy.var,&m->var)) { xx_meta_cleanup(&copy); return false; }
        if(!xx_list_append(dst,&copy)) { xx_meta_cleanup(&copy); return false; }
    }
    return true;
}
static void arqm_cursor_free(void *p)
{
    arqm_cursor *c=(arqm_cursor *)p;
    if(c) { if(c->nested) xx_arq_free_archive_records_reading(NULL,c->nested); xx_arq_free(c->reader); xx_mem_free(c); }
}
static xx_archive_record_state *arqm_records(Abstractformat *f,const xx_list_s *opts,xx_pd_struct *pd)
{
    int64_t extent; xx_arq *r=arqm_open(f,&extent,pd); if(!r) return NULL;
    xx_archive_record_state *s=(xx_archive_record_state *)xx_mem_alloc(sizeof(*s));
    if(!s) { xx_arq_free(r); return NULL; }
    xx_archive_record_state_init(s,f);
    arqm_cursor *c=(arqm_cursor *)xx_mem_calloc(1,sizeof(*c));
    if(!c) { xx_arq_free(r); xx_archive_record_state_free(s); return NULL; }
    s->internal_state=c; s->free_internal=arqm_cursor_free; c->reader=r;
    if(!arqm_options(&s->options,opts)) { xx_archive_record_state_free(s); return NULL; }
    c->nested=xx_arq_create_archive_records_reading(&r->format,&s->options,pd);
    if(!c->nested || !c->nested->has_record) { xx_archive_record_state_free(s); return NULL; }
    s->has_record=true; s->current_index=0; s->total_records=c->nested->total_records; return s;
}
static const xx_archive_record *arqm_current(Abstractformat *f,xx_archive_record_state *s)
{
    if(!f || !s || s->format!=f || !s->has_record) return NULL;
    arqm_cursor *c=(arqm_cursor *)s->internal_state;
    return c && c->nested?xx_arq_get_current_archive_record(&c->reader->format,c->nested):NULL;
}
static bool arqm_next(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd)
{
    if(!f || !s || s->format!=f || !s->has_record || carrier_stop(pd)) return false;
    arqm_cursor *c=(arqm_cursor *)s->internal_state; if(!c || !c->nested) return false;
    s->has_record=xx_arq_archive_record_move_to_next(&c->reader->format,c->nested,pd);
    if(s->has_record) ++s->current_index;
    return s->has_record;
}
static bool arqm_unpack(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd)
{
    if(!f || !s || s->format!=f || !s->has_record || carrier_stop(pd)) return false;
    arqm_cursor *c=(arqm_cursor *)s->internal_state;
    if(!c || !c->nested || !arqm_options(&c->nested->options,&s->options)) return false;
    return xx_arq_unpack_current_archive_record(&c->reader->format,c->nested,pd);
}
static void arqm_free_records(Abstractformat *f,xx_archive_record_state *s) { (void)f; xx_archive_record_state_free(s); }
