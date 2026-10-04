/* SPDX-License-Identifier: MIT - read-only, memory-only SQLite SQL export. */
#include "xxfclib/formats/sqlite_sql/xx_sqlite_sql.h"
#ifdef XXFC_SQLITE_HOSTED_DISABLED
xx_sqlite_sql *xx_sqlite_sql_create(xx_io_device *d,int64_t b) { (void)d;(void)b;return NULL; }
void xx_sqlite_sql_free(xx_sqlite_sql *r) { (void)r; }
#else
#include "upstream/sqlite3.h"
#include "../xx_payload_members.h"
#ifdef _WIN32
#include <windows.h>
static volatile LONG sql_lock;
static bool sql_enter(xx_pd_struct *pd) { while(InterlockedCompareExchange(&sql_lock,1,0)) { if(pd && xx_pd_is_stopped(pd)) return false; Sleep(1); } return true; }
static void sql_leave(void) { InterlockedExchange(&sql_lock,0); }
#else
#include <pthread.h>
static pthread_mutex_t sql_lock=PTHREAD_MUTEX_INITIALIZER;
static bool sql_enter(xx_pd_struct *pd) { (void)pd; return pthread_mutex_lock(&sql_lock)==0; }
static void sql_leave(void) { (void)pthread_mutex_unlock(&sql_lock); }
#endif
typedef struct sql_text { uint8_t *p; size_t n,cap,limit; } sql_text;
static bool sql_bytes(sql_text *b,const void *p,size_t n) {
    size_t cap; void *next;
    if(n>b->limit-b->n) return false;
    if(b->n+n>b->cap) { cap=b->cap?b->cap:1024;while(cap<b->n+n) { if(cap>b->limit/2) { cap=b->limit;break; } cap*=2; }
        next=xx_mem_realloc(b->p,cap);if(!next)return false;b->p=(uint8_t *)next;b->cap=cap; }
    if(n)xx_rt_memcpy(b->p+b->n,p,n);b->n+=n;return true;
}
static bool sql_str(sql_text *b,const char *p) { return sql_bytes(b,p,xx_rt_strlen(p)); }
static bool sql_ident(sql_text *b,const char *p) {
    if(!sql_str(b,"\""))return false;
    while(*p) { if(*p=='"' && !sql_str(b,"\""))return false;if(!sql_bytes(b,p++,1))return false; }
    return sql_str(b,"\"");
}
static int sql_stopped(void *p) { return p && xx_pd_is_stopped((xx_pd_struct *)p); }
static bool sql_value(sql_text *out,sqlite3_stmt *rows,int i) {
    int type=sqlite3_column_type(rows,i),n,j;const uint8_t *bytes;static const char hex[]="0123456789abcdef";
    if(type==SQLITE_NULL)return sql_str(out,"NULL");
    if(type==SQLITE_INTEGER || type==SQLITE_FLOAT) {
        char *real=NULL;
        const char *value=(const char *)sqlite3_column_text(rows,i);
        if(!value)return false;
        if(type==SQLITE_FLOAT) { bool ok;real=sqlite3_mprintf("%!.17g",sqlite3_column_double(rows,i));if(!real)return false;
            value=real;if(!xx_rt_strcmp(value,"Inf"))value="9e999";else if(!xx_rt_strcmp(value,"-Inf"))value="-9e999";
            ok=sql_str(out,value);sqlite3_free(real);return ok; }
        /* SQLite emits Inf for an infinite REAL; valid SQL uses a numeric overflow. */
        if(!xx_rt_strcmp(value,"Inf"))value="9e999";else if(!xx_rt_strcmp(value,"-Inf"))value="-9e999";
        return sql_str(out,value);
    }
    bytes=type==SQLITE_TEXT?sqlite3_column_text(rows,i):(const uint8_t *)sqlite3_column_blob(rows,i);n=sqlite3_column_bytes(rows,i);
    if(n<0 || (n && !bytes) || !sql_str(out,type==SQLITE_TEXT?"CAST(X'":"X'"))return false;
    for(j=0;j<n;++j) { char pair[2]={hex[bytes[j]>>4],hex[bytes[j]&15]};if(!sql_bytes(out,pair,2))return false; }
    return sql_str(out,type==SQLITE_TEXT?"' AS TEXT)":"'");
}
static bool sql_table(sqlite3 *db,const char *name,sql_text *out,xx_pd_struct *pd) {
    sqlite3_stmt *columns=NULL,*rows=NULL;char *pragma=NULL;char **names=NULL;size_t count=0,capacity=0,i;sql_text query={0};int rc;bool ok=false;size_t nrows=0;
    pragma=sqlite3_mprintf("PRAGMA table_xinfo(\"%w\")",name);if(!pragma)goto done;
    if(sqlite3_prepare_v2(db,pragma,-1,&columns,NULL)!=SQLITE_OK)goto done;
    while((rc=sqlite3_step(columns))==SQLITE_ROW) {
        const char *col=(const char *)sqlite3_column_text(columns,1);void *next;
        if(sqlite3_column_int(columns,6)!=0)continue;
        if(!col || count>=2000)goto done;
        if(count==capacity) { capacity=capacity?capacity*2:8;next=xx_mem_realloc(names,capacity*sizeof(*names));if(!next)goto done;names=(char **)next; }
        names[count]=xx_str_dup(col);if(!names[count])goto done;++count;
    }
    if(rc!=SQLITE_DONE || !count)goto done;
    query.limit=1024U*1024U;
    if(!sql_str(&query,"SELECT "))goto done;
    for(i=0;i<count;++i)if((i && !sql_str(&query,",")) || !sql_ident(&query,names[i]))goto done;
    if(!sql_str(&query," FROM ") || !sql_ident(&query,name) || !sql_bytes(&query,"",1) || sqlite3_prepare_v2(db,(char *)query.p,-1,&rows,NULL)!=SQLITE_OK)goto done;
    while((rc=sqlite3_step(rows))==SQLITE_ROW) {
        if(++nrows>1000000 || sql_stopped(pd) || !sql_str(out,"INSERT INTO ") || !sql_ident(out,name) || !sql_str(out,"("))goto done;
        for(i=0;i<count;++i)if((i && !sql_str(out,",")) || !sql_ident(out,names[i]))goto done;
        if(!sql_str(out,") VALUES("))goto done;
        for(i=0;i<count;++i)if((i && !sql_str(out,",")) || !sql_value(out,rows,(int)i))goto done;
        if(!sql_str(out,");\n"))goto done;
    }
    ok=rc==SQLITE_DONE;
done:
    sqlite3_finalize(rows);sqlite3_finalize(columns);sqlite3_free(pragma);xx_mem_free(query.p);
    for(i=0;i<count;++i)xx_str_free(names[i]);xx_mem_free(names);return ok;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    sqlite3 *db=NULL;sqlite3_stmt *schema=NULL,*check=NULL;uint8_t *image=NULL;int64_t n=pm_available(f);size_t limit=64U*1024U*1024U;const xx_var *v;
    sql_text out={0};int rc;bool ok=false;size_t tables=0;
    v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);if(v && xx_var_get_u64(v)<limit)limit=(size_t)xx_var_get_u64(v);
    if(n<100 || (uint64_t)n>limit/3 || !sql_enter(pd))return false;
    sqlite3_hard_heap_limit64((sqlite3_int64)(limit/3));
    image=(uint8_t *)xx_mem_alloc((size_t)n);out.limit=limit/3;
    if(!image || !pm_read(f,0,image,(size_t)n) || xx_rt_memcmp(image,"SQLite format 3\0",16))goto done;
    { uint32_t page=pm_be16(image+16),pages=pm_be32(image+28);
      if(page==1)page=65536;
      if(page<512 || page>65536 || (page&(page-1)) || !pages || (uint64_t)page*pages>(uint64_t)n ||
         (image[18]!=1 && image[18]!=2) || (image[19]!=1 && image[19]!=2))goto done; }
    /* A detached database has no WAL file. Treat the serialized image as a
     * rollback database; the input itself remains untouched. */
    image[18]=image[19]=1;
    if(sqlite3_open_v2(":memory:",&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,"xfu-ram-only")!=SQLITE_OK ||
       sqlite3_deserialize(db,"main",image,n,n,SQLITE_DESERIALIZE_READONLY)!=SQLITE_OK)goto done;
    sqlite3_progress_handler(db,1000,sql_stopped,pd);
    sqlite3_limit(db,SQLITE_LIMIT_LENGTH,(int)(limit/3));sqlite3_limit(db,SQLITE_LIMIT_SQL_LENGTH,1024*1024);
    if(sqlite3_db_config(db,SQLITE_DBCONFIG_TRUSTED_SCHEMA,0,NULL)!=SQLITE_OK || sqlite3_db_config(db,SQLITE_DBCONFIG_DEFENSIVE,1,NULL)!=SQLITE_OK)goto done;
    if(sqlite3_prepare_v2(db,"PRAGMA integrity_check",-1,&check,NULL)!=SQLITE_OK || sqlite3_step(check)!=SQLITE_ROW ||
       !sqlite3_column_text(check,0) || xx_rt_strcmp((const char *)sqlite3_column_text(check,0),"ok") || sqlite3_step(check)!=SQLITE_DONE)goto done;
    sqlite3_finalize(check);check=NULL;
    if(!sql_str(&out,"PRAGMA foreign_keys=OFF;\nBEGIN TRANSACTION;\n") ||
       sqlite3_prepare_v2(db,"SELECT name,sql,(SELECT type FROM pragma_table_list WHERE schema='main' AND name=s.name) FROM sqlite_schema s WHERE type='table' AND name NOT GLOB 'sqlite_*' ORDER BY name",-1,&schema,NULL)!=SQLITE_OK)goto done;
    while((rc=sqlite3_step(schema))==SQLITE_ROW) {
        const char *name=(const char *)sqlite3_column_text(schema,0),*ddl=(const char *)sqlite3_column_text(schema,1);
        const char *kind=(const char *)sqlite3_column_text(schema,2);
        if(!name || !ddl || !kind || xx_rt_strcmp(kind,"table") || ++tables>10000 || sql_stopped(pd) ||
           !sql_str(&out,ddl) || !sql_str(&out,";\n") || !sql_table(db,name,&out,pd))goto done;
    }
    if(rc!=SQLITE_DONE)goto done;sqlite3_finalize(schema);schema=NULL;
    /* AUTOINCREMENT's history can exceed the maximum remaining row. Preserve
     * the sequence after CREATE/INSERT have recreated its system table. */
    if(sqlite3_prepare_v2(db,"SELECT name FROM sqlite_schema WHERE type='table' AND name='sqlite_sequence'",-1,&schema,NULL)!=SQLITE_OK)goto done;
    rc=sqlite3_step(schema);
    if(rc==SQLITE_ROW) {
        if(!sql_str(&out,"DELETE FROM \"sqlite_sequence\";\n") || !sql_table(db,"sqlite_sequence",&out,pd))goto done;
    } else if(rc!=SQLITE_DONE)goto done;
    sqlite3_finalize(schema);schema=NULL;
    if(sqlite3_prepare_v2(db,"SELECT sql FROM sqlite_schema WHERE type IN ('index','trigger','view') AND sql IS NOT NULL ORDER BY type,name",-1,&schema,NULL)!=SQLITE_OK)goto done;
    while((rc=sqlite3_step(schema))==SQLITE_ROW) { const char *ddl=(const char *)sqlite3_column_text(schema,0);if(!ddl || sql_stopped(pd) || !sql_str(&out,ddl) || !sql_str(&out,";\n"))goto done; }
    if(rc!=SQLITE_DONE || !sql_str(&out,"COMMIT;\n") || !pm_add(f,s,"database.sql",0,0))goto done;
    s->items[0].memory=out.p;s->items[0].size=(int64_t)out.n;s->items[0].packed_size=n;out.p=NULL;s->size=n;ok=true;
done:
    sqlite3_finalize(check);sqlite3_finalize(schema);sqlite3_close(db);xx_mem_free(image);xx_mem_free(out.p);sql_leave();return ok;
}
xx_sqlite_sql *xx_sqlite_sql_create(xx_io_device *d,int64_t b) { xx_sqlite_sql *r=(xx_sqlite_sql *)xx_mem_alloc(sizeof(*r));if(r) { xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_SQLITE3,"sqlite"); }return r; }
void xx_sqlite_sql_free(xx_sqlite_sql *r) { if(r) { xx_format_cleanup_extra_parameters(&r->format);xx_mem_free(r); } }
#endif
