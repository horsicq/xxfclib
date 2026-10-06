/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native PYC extent reader. Marshal layouts follow CPython Python/marshal.c
 * (1.5.2, 2.0, 2.7, 3.10, 3.14) and PEP 552. No code is executed.
 */
#include "xxfclib/formats/pyc/xx_pyc.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"

#define PYC_MEMORY_LIMIT ((size_t)64U * 1024U * 1024U)
#define PYC_MAX_OBJECTS 262144U
#define PYC_MAX_DEPTH 128U
#define PYC_MAX_STRING (16U * 1024U * 1024U)

typedef struct { uint16_t nMagic; const char *pszVersion; } pyc_magic_record;
static const pyc_magic_record g_records[] = {
    {2012, "1.5.2"},  {3000, "3000"},   {3111, "3.0a4"},  {3131, "3.0b1"},  {3141, "3.1a1"},
    {3151, "3.1a1"},  {3160, "3.2a1"},  {3170, "3.2a2"},  {3180, "3.2a3"},  {3190, "3.3a1"},
    {3200, "3.3a1"},  {3210, "3.3a1"},  {3220, "3.3a2"},  {3230, "3.3a4"},  {3250, "3.4a1"},
    {3260, "3.4a1"},  {3270, "3.4a1"},  {3280, "3.4a1"},  {3290, "3.4a4"},  {3300, "3.4a4"},
    {3310, "3.4rc2"}, {3320, "3.5a1"},  {3330, "3.5b1"},  {3340, "3.5b2"},  {3350, "3.5b3"},
    {3351, "3.5.2"},  {3360, "3.6a0"},  {3361, "3.6a1"},  {3370, "3.6a2"},  {3371, "3.6a2"},
    {3372, "3.6a2"},  {3373, "3.6b1"},  {3375, "3.6b1"},  {3376, "3.6b1"},  {3377, "3.6b1"},
    {3378, "3.6b2"},  {3379, "3.6rc1"}, {3390, "3.7a1"},  {3391, "3.7a2"},  {3392, "3.7a4"},
    {3393, "3.7b1"},  {3394, "3.7b5"},  {3400, "3.8a1"},  {3401, "3.8a1"},  {3410, "3.8a1"},
    {3411, "3.8b2"},  {3412, "3.8b2"},  {3413, "3.8b4"},  {3420, "3.9a0"},  {3421, "3.9a0"},
    {3422, "3.9a0"},  {3423, "3.9a2"},  {3424, "3.9a2"},  {3425, "3.9a2"},  {3430, "3.10a1"},
    {3431, "3.10a1"}, {3432, "3.10a2"}, {3433, "3.10a2"}, {3434, "3.10a6"}, {3435, "3.10a7"},
    {3436, "3.10b1"}, {3437, "3.10b1"}, {3438, "3.10b1"}, {3439, "3.10b1"}, {3450, "3.11a1"},
    {3451, "3.11a1"}, {3452, "3.11a1"}, {3453, "3.11a1"}, {3454, "3.11a1"}, {3455, "3.11a1"},
    {3456, "3.11a1"}, {3457, "3.11a1"}, {3458, "3.11a1"}, {3459, "3.11a1"}, {3460, "3.11a1"},
    {3461, "3.11a1"}, {3462, "3.11a2"}, {3463, "3.11a3"}, {3464, "3.11a3"}, {3465, "3.11a3"},
    {3466, "3.11a4"}, {3467, "3.11a4"}, {3468, "3.11a4"}, {3469, "3.11a4"}, {3470, "3.11a4"},
    {3471, "3.11a4"}, {3472, "3.11a4"}, {3473, "3.11a4"}, {3474, "3.11a4"}, {3475, "3.11a5"},
    {3476, "3.11a5"}, {3477, "3.11a5"}, {3478, "3.11a5"}, {3479, "3.11a5"}, {3480, "3.11a5"},
    {3481, "3.11a5"}, {3482, "3.11a5"}, {3483, "3.11a5"}, {3484, "3.11a5"}, {3485, "3.11a5"},
    {3486, "3.11a6"}, {3487, "3.11a6"}, {3488, "3.11a6"}, {3489, "3.11a6"}, {3490, "3.11a6"},
    {3491, "3.11a6"}, {3492, "3.11a7"}, {3493, "3.11a7"}, {3494, "3.11a7"}, {3495, "3.11b4"},
    {3500, "3.12a1"}, {3501, "3.12a1"}, {3502, "3.12a1"}, {3503, "3.12a1"}, {3504, "3.12a1"},
    {3505, "3.12a1"}, {3506, "3.12a1"}, {3507, "3.12a1"}, {3508, "3.12a1"}, {3509, "3.12a1"},
    {3510, "3.12a2"}, {3511, "3.12a2"}, {3512, "3.12a2"}, {3513, "3.12a4"}, {3514, "3.12a4"},
    {3515, "3.12a5"}, {3516, "3.12a5"}, {3517, "3.12a5"}, {3518, "3.12a6"}, {3519, "3.12a6"},
    {3520, "3.12a6"}, {3521, "3.12a7"}, {3522, "3.12a7"}, {3523, "3.12a7"}, {3524, "3.12a7"},
    {3525, "3.12b1"}, {3526, "3.12b1"}, {3527, "3.12b1"}, {3528, "3.12b1"}, {3529, "3.12b1"},
    {3530, "3.12b1"}, {3531, "3.12b1"}, {3550, "3.13a1"}, {3551, "3.13a1"}, {3552, "3.13a1"},
    {3553, "3.13a1"}, {3554, "3.13a1"}, {3555, "3.13a1"}, {3556, "3.13a1"}, {3557, "3.13a1"},
    {3558, "3.13a1"}, {3559, "3.13a1"}, {3560, "3.13a1"}, {3561, "3.13a1"}, {3562, "3.13a1"},
    {3563, "3.13a1"}, {3564, "3.13a1"}, {3565, "3.13a1"}, {3566, "3.13a1"}, {3567, "3.13a1"},
    {3568, "3.13a1"}, {3569, "3.13a5"}, {3570, "3.13a6"}, {3571, "3.13b1"}, {3600, "3.14 will start with"}, {3627, "3.14"},
    {5042, "1.6"},    {5082, "2.0.1"},  {6020, "2.1.2"},  {6071, "2.2"},    {6201, "2.3a0"},
    {6202, "2.3a0"},  {6204, "2.4a0"},  {6205, "2.4a3"},  {6206, "2.4b1"},  {6207, "2.5a0"},
    {6208, "2.5a0"},  {6209, "2.5a0"},  {6210, "2.5b3"},  {6211, "2.5b3"},  {6212, "2.5c1"},
    {6213, "2.5c2"},  {6215, "2.6a0"},  {6216, "2.6a1"},  {6217, "2.7a0"},  {6218, "2.7a0"},
    {6219, "2.7a0"},  {6220, "2.7a0"},  {6221, "2.7a0"}};

static const pyc_magic_record pyc_historical[] = {
    {20121,"1.5.2"},{50428,"1.6"},{50823,"2.0.1"},{60202,"2.1.2"},
    {60717,"2.2"},{62011,"2.3"},{62021,"2.3a0"},{62041,"2.4a0"},
    {62051,"2.4a3"},{62061,"2.4"},{62071,"2.5a0"},{62081,"2.5a0"},
    {62091,"2.5a0"},{62092,"2.5a0"},{62101,"2.5b3"},{62111,"2.5b3"},
    {62121,"2.5c1"},{62131,"2.5"},{62151,"2.6a0"},{62161,"2.6"},
    {62171,"2.7a0"},{62181,"2.7a0"},{62191,"2.7a0"},{62201,"2.7a0"},{62211,"2.7"}
};
static const char *pyc_version(uint16_t magic) {
    size_t i;
    for (i=0;i<sizeof(g_records)/sizeof(g_records[0]);++i)
        if (g_records[i].nMagic==magic) return g_records[i].pszVersion;
    for (i=0;i<sizeof(pyc_historical)/sizeof(pyc_historical[0]);++i)
        if (pyc_historical[i].nMagic==magic) return pyc_historical[i].pszVersion;
    return "";
}
bool xx_pyc_is_known_magic(uint16_t magic) { return *pyc_version(magic)!=0; }
bool xx_pyc_check_magic(const uint8_t *data,size_t size) {
    return data && size>=4 && data[2]==13 && data[3]==10 &&
        xx_pyc_is_known_magic((uint16_t)(data[0]|((uint16_t)data[1]<<8)));
}

typedef enum { PK_SCALAR,PK_BYTES,PK_TEXT,PK_TUPLE,PK_LIST,PK_DICT,
               PK_SET,PK_FROZENSET,PK_SLICE,PK_CODE } pyc_kind;
typedef struct { pyc_kind kind; uint8_t encoding; int64_t offset; uint32_t size; } pyc_value;
typedef struct {
    xx_pyc *reader;
    xx_pd_struct *pd;
    int64_t at,end,cache_at;
    size_t cache_size,limit,used;
    uint8_t cache[8192];
    unsigned major,minor,objects;
    pyc_value *refs,*interned;
    size_t ref_count,ref_capacity,intern_count,intern_capacity;
    bool collect_enabled,in_root_constants,budget_failed;
} pyc_cursor;

static void pyc_constant_free(void *p) { xx_str_free(*(char **)p); }
static void pyc_clear_constants(xx_pyc *r) {
    xx_list_cleanup(&r->constants);
    (void)xx_list_init(&r->constants,sizeof(char *),pyc_constant_free);
}
static bool pyc_span(pyc_cursor *c,int64_t at,size_t n) {
    return at>=0 && at<=c->end && (uint64_t)n<=(uint64_t)(c->end-at) && !xx_pd_is_stopped(c->pd);
}
static int pyc_byte_at(pyc_cursor *c,int64_t at) {
    if (!pyc_span(c,at,1)) return -1;
    if (at<c->cache_at || (uint64_t)(at-c->cache_at)>=c->cache_size) {
        size_t n=(size_t)((c->end-at)<(int64_t)sizeof(c->cache) ? c->end-at : (int64_t)sizeof(c->cache)),done=0;
        c->cache_at=at;c->cache_size=0;
        if (xx_io_seek64(c->reader->format.device,c->reader->format.base_address+at,SEEK_SET)) return -1;
        while (done<n) {
            ssize_t got=xx_io_read(c->reader->format.device,c->cache+done,n-done);
            if (got<=0 || (size_t)got>n-done || xx_pd_is_stopped(c->pd)) return -1;
            done+=(size_t)got;
        }
        c->cache_size=n;
    }
    return c->cache[(size_t)(at-c->cache_at)];
}
static bool pyc_number(pyc_cursor *c,unsigned bytes,uint32_t *out) {
    unsigned i;uint32_t n=0;
    if (!pyc_span(c,c->at,bytes)) return false;
    for (i=0;i<bytes;++i) { int ch=pyc_byte_at(c,c->at++);if(ch<0)return false;n|=(uint32_t)ch<<(i*8U); }
    *out=n;return true;
}
static bool pyc_skip(pyc_cursor *c,uint64_t size) {
    if (size>SIZE_MAX || !pyc_span(c,c->at,(size_t)size)) return false;
    c->at+=(int64_t)size;return true;
}
static bool pyc_budget(pyc_cursor *c,size_t more) {
    if (c->used>c->limit || more>c->limit-c->used) { c->budget_failed=true;return false; }
    return true;
}
static bool pyc_reference_add(pyc_cursor *c,bool interned,pyc_value value,size_t *index) {
    pyc_value **array=interned ? &c->interned : &c->refs;
    size_t *count=interned ? &c->intern_count : &c->ref_count;
    size_t *capacity=interned ? &c->intern_capacity : &c->ref_capacity;
    if (*count>=PYC_MAX_OBJECTS) return false;
    if (*count==*capacity) {
        size_t n=*capacity ? *capacity*2U : 64U,delta=(n-*capacity)*sizeof(**array);
        pyc_value *next;
        if (!pyc_budget(c,delta)) return false;
        next=(pyc_value *)xx_mem_realloc(*array,n*sizeof(*next));
        if (!next) { c->budget_failed=true;return false; }
        *array=next;*capacity=n;c->used+=delta;
    }
    *index=(*count)++;(*array)[*index]=value;return true;
}
static bool pyc_utf8(pyc_cursor *c,int64_t at,uint32_t size) {
    uint32_t i=0;
    while(i<size) {
        int ch=pyc_byte_at(c,at+i++);unsigned left;uint32_t cp,min;
        if(ch<0)return false;
        if(ch<128)continue;
        if(ch>=0xC2&&ch<=0xDF){left=1;cp=(uint32_t)(ch&31);min=0x80;}
        else if(ch>=0xE0&&ch<=0xEF){left=2;cp=(uint32_t)(ch&15);min=0x800;}
        else if(ch>=0xF0&&ch<=0xF4){left=3;cp=(uint32_t)(ch&7);min=0x10000;}
        else return false;
        if(left>size-i)return false;
        while(left--) { ch=pyc_byte_at(c,at+i++);if(ch<0x80||ch>0xBF)return false;cp=(cp<<6)|(uint32_t)(ch&63); }
        /* CPython uses surrogatepass for marshal Unicode. */
        if(cp<min||cp>0x10FFFF)return false;
    }
    return true;
}
static bool pyc_collect(pyc_cursor *c,pyc_value v) {
    xx_list_s *list=&c->reader->constants;
    size_t bytes,i,n=0;
    char *text;
    if(v.kind!=PK_TEXT&&v.kind!=PK_BYTES)return true;
    bytes=(size_t)v.size*(v.encoding ? 2U:1U)+1U;
    if(!pyc_budget(c,bytes))return false;
    text=(char *)xx_mem_alloc(bytes);if(!text){c->budget_failed=true;return false;}
    for(i=0;i<v.size;++i) {
        int ch=pyc_byte_at(c,v.offset+(int64_t)i);
        if(ch<0){xx_mem_free(text);return false;}
        if(!ch)break;
        if(v.encoding&&ch>=128) { text[n++]=(char)(0xC0|(ch>>6));text[n++]=(char)(0x80|(ch&63)); }
        else text[n++]=(char)ch;
    }
    text[n]=0;
    if(list->count==list->capacity) {
        size_t capacity=list->capacity ? list->capacity*2U:16U;
        size_t delta=(capacity-list->capacity)*sizeof(char *);
        if(!pyc_budget(c,bytes+delta)||!xx_list_reserve(list,capacity)){xx_mem_free(text);c->budget_failed=true;return false;}
        c->used+=delta;
    }
    if(!xx_list_append(list,&text)){xx_mem_free(text);c->budget_failed=true;return false;}
    c->used+=bytes;return true;
}
static bool pyc_object(pyc_cursor *,unsigned,bool,pyc_value *);
static bool pyc_field(pyc_cursor *c,unsigned depth,pyc_kind kind,bool collect) {
    pyc_value v;
    if(!pyc_object(c,depth+1U,collect,&v))return false;
    return v.kind==kind || (kind==PK_TEXT && c->major<3 && v.kind==PK_BYTES);
}
static bool pyc_code(pyc_cursor *c,unsigned depth) {
    unsigned i,ints=4,width=(c->major<2 || (c->major==2&&c->minor<3)) ? 2U:4U;
    bool modern=c->major>=3 && c->reader->magic>=3453 && c->reader->magic<5000;
    uint32_t n;
    if(c->major>=3) {
        ints=5;
        if(c->reader->magic>=3410)++ints;
        if(c->reader->magic>=3452&&c->reader->magic<5000)--ints;
    }
    for(i=0;i<ints;++i)if(!pyc_number(c,width,&n)||(i+1U<ints&&n>INT32_MAX))return false;
    if(!pyc_field(c,depth,PK_BYTES,false))return false;
    if(depth==0){c->in_root_constants=true;c->collect_enabled=true;}
    if(!pyc_field(c,depth,PK_TUPLE,depth==0))return false;
    if(depth==0)c->in_root_constants=false;
    if(!pyc_field(c,depth,PK_TUPLE,false)||!pyc_field(c,depth,PK_TUPLE,false))return false;
    if(modern) {
        if(c->reader->magic>=3457) { if(!pyc_field(c,depth,PK_BYTES,false))return false; }
        else { pyc_value v;if(!pyc_object(c,depth+1U,false,&v)||(v.kind!=PK_TUPLE&&v.kind!=PK_BYTES))return false; }
    } else if(c->major>2 || (c->major==2&&c->minor>=1)) {
        if(!pyc_field(c,depth,PK_TUPLE,false)||!pyc_field(c,depth,PK_TUPLE,false))return false;
    }
    if(!pyc_field(c,depth,PK_TEXT,false)||!pyc_field(c,depth,PK_TEXT,false))return false;
    if(modern&&c->reader->magic>=3460&&!pyc_field(c,depth,PK_TEXT,false))return false;
    if(!pyc_number(c,width,&n)||n>INT32_MAX||!pyc_field(c,depth,PK_BYTES,false))return false;
    if(c->major>=3&&c->reader->magic>=3450&&c->reader->magic<5000&&!pyc_field(c,depth,PK_BYTES,false))return false;
    return true;
}
static bool pyc_object(pyc_cursor *c,unsigned depth,bool collect,pyc_value *out) {
    int actual;
    uint8_t type,flag;
    uint32_t n=0,i;
    size_t ref=SIZE_MAX;
    pyc_value v={PK_SCALAR,0,0,0};
    if(depth>PYC_MAX_DEPTH||++c->objects>PYC_MAX_OBJECTS||(actual=pyc_byte_at(c,c->at++))<0)return false;
    flag=(uint8_t)(actual&128);type=(uint8_t)(actual&127);
    if(type=='s')v.kind=PK_BYTES;
    else if(type=='t'||type=='u'||type=='a'||type=='A'||type=='z'||type=='Z')v.kind=PK_TEXT;
    else if(type=='('||type==')')v.kind=PK_TUPLE;
    else if(type=='[')v.kind=PK_LIST;
    else if(type=='{')v.kind=PK_DICT;
    else if(type=='<')v.kind=PK_SET;
    else if(type=='>')v.kind=PK_FROZENSET;
    else if(type==':')v.kind=PK_SLICE;
    else if(type=='c')v.kind=PK_CODE;
    if(flag && (type=='r'||type=='R'||type=='0'||!pyc_reference_add(c,false,v,&ref)))return false;
    switch(type) {
    case 'N':case 'F':case 'T':case 'S':case '.':break;
    case 'i':if(!pyc_skip(c,4))return false;break;
    case 'I':case 'g':if(!pyc_skip(c,8))return false;break;
    case 'y':if(!pyc_skip(c,16))return false;break;
    case 'f':case 'x':
        for(i=0;i<(type=='x'?2U:1U);++i)if(!pyc_number(c,1,&n)||!n||!pyc_skip(c,n))return false;
        break;
    case 'l': {
        int32_t count;uint32_t digit=0;
        if(!pyc_number(c,4,&n)) {return false; } count=(int32_t)n;
        if(count==INT32_MIN) {return false; } n=(uint32_t)(count<0?-count:count);
        if(n>PYC_MAX_STRING/2U||!pyc_span(c,c->at,(size_t)n*2U))return false;
        for(i=0;i<n;++i)if(!pyc_number(c,2,&digit)||digit>32767)return false;
        if(n&&!digit) {return false; } break;
    }
    case 's':case 't':case 'u':case 'a':case 'A':case 'z':case 'Z': {
        bool ascii=type=='a'||type=='A'||type=='z'||type=='Z';
        if(!pyc_number(c,(type=='z'||type=='Z')?1U:4U,&n)||n>PYC_MAX_STRING||!pyc_span(c,c->at,n))return false;
        v.offset=c->at;v.size=n;v.encoding=(uint8_t)(type!='s'&&type!='u');
        if((type=='u'||(type=='t'&&c->major>=3))&&!pyc_utf8(c,c->at,n))return false;
        if(ascii)for(i=0;i<n;++i){int ch=pyc_byte_at(c,c->at+i);if(ch<0||ch>127)return false;}
        if(!pyc_skip(c,n))return false;
        if(type=='t'){size_t index;if(!pyc_reference_add(c,true,v,&index))return false;}
        break;
    }
    case 'r':case 'R':
        if(!pyc_number(c,4,&n))return false;
        if(type=='r'){if(n>=c->ref_count)return false;v=c->refs[n];}
        else {if(n>=c->intern_count)return false;v=c->interned[n];}
        /* These immutable objects are reserved until their fields are decoded. */
        if((v.kind==PK_CODE||v.kind==PK_SLICE||v.kind==PK_FROZENSET)&&!v.size)return false;
        break;
    case '(':case ')':case '[':case '<':case '>':
        if(!pyc_number(c,type==')'?1U:4U,&n)||n>PYC_MAX_OBJECTS||n>(uint64_t)(c->end-c->at))return false;
        for(i=0;i<n;++i) { pyc_value child;if(!pyc_object(c,depth+1U,false,&child))return false;
            if(collect&&c->collect_enabled&&!pyc_collect(c,child))return false; }
        if(type=='>')v.size=1;
        break;
    case '{':
        for(i=0;i<PYC_MAX_OBJECTS;++i) {
            pyc_value child;int ch=pyc_byte_at(c,c->at);
            if(ch=='0'){++c->at;break;}
            if(ch<0||!pyc_object(c,depth+1U,false,&child)||!pyc_object(c,depth+1U,false,&child))return false;
        }
        if(i==PYC_MAX_OBJECTS) {return false; } break;
    case 'c':
        if(c->in_root_constants)c->collect_enabled=false;
        if(!pyc_code(c,depth)) {return false; } v.size=1;break;
    case ':':
        if(c->major<3||c->minor<14)return false;
        for(i=0;i<3;++i){pyc_value child;if(!pyc_object(c,depth+1U,false,&child))return false;}
        v.size=1;break;
    default:return false;
    }
    if(ref!=SIZE_MAX)c->refs[ref]=v;
    *out=v;return !xx_pd_is_stopped(c->pd);
}

static void pyc_version_numbers(const char *text,unsigned *major,unsigned *minor) {
    *major=*minor=0;
    while(*text>='0'&&*text<='9')*major=*major*10U+(unsigned)(*text++-'0');
    if(*text=='.') { ++text;while(*text>='0'&&*text<='9')*minor=*minor*10U+(unsigned)(*text++-'0'); }
}
static void pyc_destroy_callback(Abstractformat *f) { xx_pyc_destroy((xx_pyc *)f); }
void xx_pyc_init(xx_pyc *r,xx_io_device *device,int64_t base_address) {
    if(!r)return;
    xx_mem_zero(r,sizeof(*r));xx_format_init(&r->format,device,base_address);
    r->format.file_type=XX_FILE_TYPE_PYC;r->format.format_type=XX_TYPE_CONSOLE_APPLICATION;
    r->format.is_executable=true;xx_format_set_extension(&r->format,"pyc");
    xx_format_set_mime_type(&r->format,"application/x-python-code");
    r->format.check_is_valid=xx_pyc_check_is_valid;r->format.handle_base_info=xx_pyc_handle_base_info;
    r->format.get_format_size=xx_pyc_get_format_size;r->format.destroy=pyc_destroy_callback;
    (void)xx_list_init(&r->constants,sizeof(char *),pyc_constant_free);
}
xx_pyc *xx_pyc_create(xx_io_device *device,int64_t base_address) {
    xx_pyc *r=(xx_pyc *)xx_mem_alloc(sizeof(*r));if(r)xx_pyc_init(r,device,base_address);return r;
}
void xx_pyc_destroy(xx_pyc *r) {
    if(r){xx_list_cleanup(&r->constants);xx_format_cleanup_extra_parameters(&r->format);r->analyzed=false;}
}
void xx_pyc_free(xx_pyc *r) { if(r){xx_pyc_destroy(r);xx_mem_free(r);} }
bool xx_pyc_analyze(xx_pyc *r,xx_pd_struct *pd) {
    pyc_cursor c;pyc_value module;
    uint8_t header[4];uint32_t flags=0;int64_t total;
    const xx_var *option;
    unsigned i;
    bool valid;
    if(!r||!r->format.device||r->format.base_address<0||xx_pd_is_stopped(pd))return false;
    if(r->analyzed)return true;
    total=xx_io_size(r->format.device);
    if(total<r->format.base_address||total-r->format.base_address<4)return false;
    xx_mem_zero(&c,sizeof(c));c.reader=r;c.pd=pd;c.end=total-r->format.base_address;c.cache_at=-1;c.limit=PYC_MEMORY_LIMIT;c.used=sizeof(c);
    option=xx_format_resolve_extra_parameter(&r->format,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    if(option&&xx_var_get_u64(option)<c.limit)c.limit=(size_t)xx_var_get_u64(option);
    if(c.used>c.limit)return false;
    for(i=0;i<4;++i){int ch=pyc_byte_at(&c,i);if(ch<0)return false;header[i]=(uint8_t)ch;}
    if(!xx_pyc_check_magic(header,sizeof(header)))return false;
    r->magic=(uint16_t)(header[0]|((uint16_t)header[1]<<8));
    (void)xx_rt_snprintf(r->version,sizeof(r->version),"%s",pyc_version(r->magic));
    pyc_version_numbers(r->version,&c.major,&c.minor);
    /* The historical Python 3000 entry keeps its public version label. */
    if(r->magic==3000){c.major=3;c.minor=0;}
    r->header_size=c.major>=3 ? (r->magic>=3392 ? 16U:r->magic>=3210 ? 12U:8U):8U;
    if(!pyc_span(&c,0,r->header_size))return false;
    if(r->header_size==16){c.at=4;if(!pyc_number(&c,4,&flags))return false;}
    r->flags=flags;r->marshal_offset=(int64_t)r->header_size;c.at=r->marshal_offset;
    pyc_clear_constants(r);
    valid=!(flags&~3U)&&pyc_object(&c,0,false,&module)&&module.kind==PK_CODE;
    xx_mem_free(c.refs);xx_mem_free(c.interned);
    if(xx_pd_is_stopped(pd)||c.budget_failed){pyc_clear_constants(r);return false;}
    r->analyzed=true;r->format.is_valid=valid;
    xx_format_set_version(&r->format,r->version);
    if(valid){r->marshal_size=c.at-r->marshal_offset;r->format.format_size=c.at;
        r->format.overlay_offset=c.at<c.end ? r->format.base_address+c.at:-1;
        r->format.overlay_size=c.end-c.at;r->format.base_info_handled=true;}
    return true;
}
bool xx_pyc_const_present(const xx_pyc *r,const char *value) {
    size_t i;if(!r||!value)return false;
    for(i=0;i<r->constants.count;++i)if(!xx_rt_strcmp(*(char **)xx_list_at(&r->constants,i),value))return true;
    return false;
}
bool xx_pyc_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return f&&xx_pyc_analyze((xx_pyc *)f,pd)&&f->is_valid; }
bool xx_pyc_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return xx_pyc_check_is_valid(f,pd); }
int64_t xx_pyc_get_format_size(Abstractformat *f,xx_pd_struct *pd) { return xx_pyc_handle_base_info(f,pd)?f->format_size:-1; }
