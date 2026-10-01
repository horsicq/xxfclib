/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://docs.oracle.com/javase/specs/jvms/se25/html/jvms-4.html
 * Class-file versions45-69, constant references and counted member/attribute
 * framing. Exports encoded components; no bytecode execution/verification.
 */
#include "xxfclib/formats/java_class/xx_java_class.h"
#include "../xx_payload_members.h"

typedef struct class_cp { uint16_t a,b; uint8_t tag,kind; } class_cp;
static bool class_u16(Abstractformat *f,int64_t *at,uint16_t *out) {
    uint8_t p[2]; if(!pm_read(f,*at,p,2)) return false; *at+=2; *out=pm_be16(p); return true;
}
static bool class_attributes(Abstractformat *f,int64_t *at,class_cp *cp,uint16_t total,xx_pd_struct *pd) {
    uint16_t count,i; uint8_t p[6];
    if(!class_u16(f,at,&count)) return false;
    for(i=0;i<count;++i) {
        uint16_t name; uint32_t n;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,*at,p,6)) return false;
        name=pm_be16(p); n=pm_be32(p+2); *at+=6;
        if(!name || name>=total || cp[name].tag!=1 || n>(uint64_t)(pm_available(f)-*at)) return false;
        *at+=n;
    }
    return true;
}
static bool class_cp_valid(class_cp *cp,uint16_t total,uint16_t major,xx_pd_struct *pd) {
    uint32_t i;
    for(i=1;i<total;++i) {
        uint16_t a=cp[i].a,b=cp[i].b; uint8_t tag=cp[i].tag;
        if(!(i&255) && pd && xx_pd_is_stopped(pd)) return false;
        if(tag==7 || tag==8 || tag==16 || tag==19 || tag==20) { if(!a || a>=total || cp[a].tag!=1) return false; }
        else if(tag==9 || tag==10 || tag==11) { if(!a || a>=total || cp[a].tag!=7 || !b || b>=total || cp[b].tag!=12) return false; }
        else if(tag==12) { if(!a || a>=total || cp[a].tag!=1 || !b || b>=total || cp[b].tag!=1) return false; }
        else if(tag==17 || tag==18) { if(!b || b>=total || cp[b].tag!=12) return false; }
        else if(tag==15) {
            uint8_t target;
            if(!a || a>=total || cp[i].kind<1 || cp[i].kind>9) return false; target=cp[a].tag;
            if(cp[i].kind<=4 ? target!=9 : cp[i].kind==9 ? target!=11 :
               (cp[i].kind==5 || cp[i].kind==8) ? target!=10 :
               (target!=10 && !(major>=52 && target==11))) return false;
        }
    }
    return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[10],p[8]; uint16_t major,minor,total,i,count,self,super,flags; int64_t at=10,start; class_cp *cp=NULL; bool ok=false;
    const xx_var *budget=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    if(!pm_read(f,0,h,10) || pm_be32(h)!=0xcafebabeU) return false;
    minor=pm_be16(h+4); major=pm_be16(h+6); total=pm_be16(h+8);
    if(major<45 || major>69 || (major==45?minor>3:(minor && !(major>=56 && minor==65535))) || total<2) return false;
    if(budget && (uint64_t)total*sizeof(*cp)>xx_var_get_u64(budget)) return false;
    cp=(class_cp *)xx_mem_alloc((size_t)total*sizeof(*cp)); if(!cp) return false; xx_mem_zero(cp,(size_t)total*sizeof(*cp));
    for(i=1;i<total;++i) {
        uint8_t tag; uint32_t bytes=0;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at++,&tag,1)) goto done; cp[i].tag=tag;
        switch(tag) {
            case 1: { uint16_t n; if(!class_u16(f,&at,&n) || n>(uint64_t)(pm_available(f)-at)) goto done; at+=n; continue; }
            case 3: case 4: bytes=4; break;
            case 5: case 6: bytes=8; if(i+1>=total) goto done; ++i; break;
            case 7: case 8: bytes=2; break;
            case 9: case 10: case 11: case 12: bytes=4; break;
            case 15: if(major<51) goto done; bytes=3; break;
            case 16: if(major<51) goto done; bytes=2; break;
            case 17: if(major<55) goto done; bytes=4; break;
            case 18: if(major<51) goto done; bytes=4; break;
            case 19: case 20: if(major<53) goto done; bytes=2; break;
            default: goto done;
        }
        if(!pm_read(f,at,p,bytes)) goto done;
        if(tag==15) { cp[i].kind=p[0]; cp[i].a=pm_be16(p+1); }
        else if(tag==7 || tag==8 || tag==16 || tag==19 || tag==20) cp[i].a=pm_be16(p);
        else if(tag==9 || tag==10 || tag==11 || tag==12 || tag==17 || tag==18) { cp[i].a=pm_be16(p); cp[i].b=pm_be16(p+2); }
        at+=bytes;
    }
    if(!class_cp_valid(cp,total,major,pd) || !pm_add(f,s,"constant-pool.bin",10,at-10)) goto done;
    if(!class_u16(f,&at,&flags) || !class_u16(f,&at,&self) || !class_u16(f,&at,&super) || !class_u16(f,&at,&count)) goto done;
    if(!self || self>=total || cp[self].tag!=7 || (super && (super>=total || cp[super].tag!=7))) goto done;
    start=at;
    for(i=0;i<count;++i) { uint16_t index; if((pd && xx_pd_is_stopped(pd)) || !class_u16(f,&at,&index) || !index || index>=total || cp[index].tag!=7) goto done; }
    if(count && !pm_add(f,s,"interfaces.bin",start,at-start)) goto done;
    {
        unsigned group;
        for(group=0;group<2;++group) {
            if(!class_u16(f,&at,&count)) goto done;
            for(i=0;i<count;++i) {
                uint16_t name,descriptor; char label[48]; start=at;
                if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at,p,6)) goto done; at+=6;
                name=pm_be16(p+2); descriptor=pm_be16(p+4);
                if(!name || name>=total || cp[name].tag!=1 || !descriptor || descriptor>=total || cp[descriptor].tag!=1 || !class_attributes(f,&at,cp,total,pd)) goto done;
                xx_rt_snprintf(label,sizeof(label),"%s-%u.bin",group?"method":"field",(unsigned)i);
                if(!pm_add(f,s,label,start,at-start)) goto done;
            }
        }
    }
    start=at; if(!class_attributes(f,&at,cp,total,pd)) goto done;
    if(at-start>2 && !pm_add(f,s,"class-attributes.bin",start,at-start)) goto done;
    s->size=at; ok=true;
done: if(cp) xx_mem_free(cp); return ok;
}

void xx_java_class_init(xx_java_class *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_JAVA_CLASS,"class"); } }
xx_java_class *xx_java_class_create(xx_io_device *d,int64_t b) { xx_java_class *r=(xx_java_class *)xx_mem_alloc(sizeof(*r)); if(r) xx_java_class_init(r,d,b); return r; }
void xx_java_class_destroy(xx_java_class *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_java_class_free(xx_java_class *r) { if(r) { xx_java_class_destroy(r); xx_mem_free(r); } }
bool xx_java_class_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_java_class_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
