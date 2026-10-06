/* SPDX-License-Identifier: MIT
 * Native message-catalog and MIME extraction. Grammars documented by GNU
 * gettext, Qt's qm.cpp and RFC 2045/2046. No upstream implementation copied.
 */
#include "xxfclib/formats/ue2_documents/xx_ue2_documents.h"
#include "../xx_payload_members.h"

typedef struct doc_bytes { uint8_t *p; size_t n, cap, limit; } doc_bytes;
static bool doc_append(doc_bytes *b, const void *p, size_t n) {
    size_t cap; void *next;
    if (n > b->limit-b->n) return false;
    if (b->n+n>b->cap) {
        cap=b->cap ? b->cap : 1024;
        while(cap<b->n+n) { if(cap>b->limit/2) { cap=b->limit; break; } cap*=2; }
        next=xx_mem_realloc(b->p,cap); if(!next) return false;
        b->p=(uint8_t *)next; b->cap=cap;
    }
    if(n) { xx_rt_memcpy(b->p+b->n,p,n); } b->n+=n; return true;
}
static bool doc_text(doc_bytes *b,const char *p) { return doc_append(b,p,xx_rt_strlen(p)); }
static size_t doc_limit(Abstractformat *f) {
    const xx_var *v=xx_format_resolve_extra_parameter(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n=v ? xx_var_get_u64(v) : 64U*1024U*1024U;
    return (size_t)(n>64U*1024U*1024U ? 64U*1024U*1024U : n);
}
static bool doc_member(Abstractformat *f,pm_stream *s,const char *name,doc_bytes *b) {
    pm_member *m;
    if(!pm_add(f,s,name,0,0)) return false;
    m=&s->items[s->count-1]; m->memory=b->p; m->size=(int64_t)b->n;
    m->packed_size=(int64_t)b->n; b->p=NULL; b->n=b->cap=0; return true;
}
static bool doc_escape_po(doc_bytes *b,const uint8_t *p,size_t n) {
    size_t i; char oct[5];
    if(!doc_text(b,"\"")) return false;
    for(i=0;i<n;++i) {
        uint8_t c=p[i];
        if(c=='\n') { if(!doc_text(b,"\\n")) return false; }
        else if(c=='\r') { if(!doc_text(b,"\\r")) return false; }
        else if(c=='\t') { if(!doc_text(b,"\\t")) return false; }
        else if(c=='"' || c=='\\') { if(!doc_text(b,"\\") || !doc_append(b,&c,1)) return false; }
        else if(c<32 || c==127) { xx_rt_snprintf(oct,sizeof(oct),"\\%03o",(unsigned)c); if(!doc_text(b,oct)) return false; }
        else if(!doc_append(b,&c,1)) return false;
    }
    return doc_text(b,"\"\n");
}
static uint32_t doc_u32(const uint8_t *p,bool little) { return little ? pm_le32(p) : pm_be32(p); }
static bool doc_mo(Abstractformat *f,pm_stream *s,const uint8_t *p,size_t n,xx_pd_struct *pd) {
    uint32_t count,orig,trans,hash_count,hash; bool little; size_t i; doc_bytes out={0}; bool ok=false;
    if(n<28) return false;
    little=pm_le32(p)==0x950412deU;
    if(!little && pm_be32(p)!=0x950412deU) return false;
    /* Major revision 1 adds system-dependent strings. Refuse rather than
     * silently lose messages. All major revision 0 catalogs are represented. */
    if(doc_u32(p+4,little)>>16) return false;
    count=doc_u32(p+8,little); orig=doc_u32(p+12,little); trans=doc_u32(p+16,little);
    hash_count=doc_u32(p+20,little); hash=doc_u32(p+24,little);
    if(count>1000000 || orig>n || trans>n || (uint64_t)count*8>n-orig || (uint64_t)count*8>n-trans ||
       (hash_count && (hash>n || (uint64_t)hash_count*4>n-hash))) return false;
    for(i=0;i<hash_count;++i) if(doc_u32(p+hash+i*4,little)>count) return false;
    out.limit=doc_limit(f)/2;
    for(i=0;i<count;++i) {
        uint32_t al=doc_u32(p+orig+i*8,little),ao=doc_u32(p+orig+i*8+4,little);
        uint32_t bl=doc_u32(p+trans+i*8,little),bo=doc_u32(p+trans+i*8+4,little);
        const uint8_t *a,*b; size_t context=0,plural=0,j,pos=0,index=0; char label[48];
        if((pd && xx_pd_is_stopped(pd)) || ao>=n || bo>=n || al>=n-ao || bl>=n-bo || p[ao+al] || p[bo+bl]) goto done;
        a=p+ao; b=p+bo;
        while(context<al && a[context]!=4 && a[context]) ++context;
        if(context<al && a[context]==4) {
            if(!doc_text(&out,"msgctxt ") || !doc_escape_po(&out,a,context)) goto done;
            a+=context+1; al-=(uint32_t)context+1;
        }
        while(plural<al && a[plural]) ++plural;
        if(!doc_text(&out,"msgid ") || !doc_escape_po(&out,a,plural)) goto done;
        if(plural<al) {
            if(!doc_text(&out,"msgid_plural ") || !doc_escape_po(&out,a+plural+1,al-plural-1)) goto done;
            do {
                j=pos; while(j<bl && b[j]) ++j;
                xx_rt_snprintf(label,sizeof(label),"msgstr[%u] ",(unsigned)index++);
                if(!doc_text(&out,label) || !doc_escape_po(&out,b+pos,j-pos)) goto done;
                pos=j+1;
            } while(pos<=bl);
        } else if(!doc_text(&out,"msgstr ") || !doc_escape_po(&out,b,bl)) goto done;
        if(!doc_text(&out,"\n")) goto done;
    }
    ok=doc_member(f,s,"messages.po",&out);
done: xx_mem_free(out.p); return ok;
}
static bool doc_xml(doc_bytes *b,const uint8_t *p,size_t n) {
    size_t i;
    /* QM's byte fields are UTF-8. Reject malformed sequences and XML-invalid
     * scalar values instead of exporting a TS file no XML reader can open. */
    for(i=0;i<n;) {
        uint32_t cp=p[i++];unsigned tail=0;uint32_t minimum=0;
        if(cp>=0xc2 && cp<=0xdf) {tail=1;minimum=0x80;cp&=31;}
        else if(cp>=0xe0 && cp<=0xef) {tail=2;minimum=0x800;cp&=15;}
        else if(cp>=0xf0 && cp<=0xf4) {tail=3;minimum=0x10000;cp&=7;}
        else if(cp>=128)return false;
        if(tail>n-i)return false;
        while(tail--) {uint8_t c=p[i++];if((c&0xc0)!=0x80)return false;cp=(cp<<6)|(c&63);}
        if(cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff) ||
           cp==0xfffe || cp==0xffff || (cp<32 && cp!=9 && cp!=10 && cp!=13))return false;
    }
    for(i=0;i<n;++i) {
        uint8_t c=p[i];
        if(c=='&') { if(!doc_text(b,"&amp;")) return false; }
        else if(c=='<') { if(!doc_text(b,"&lt;")) return false; }
        else if(c=='>') { if(!doc_text(b,"&gt;")) return false; }
        else if(c=='"') { if(!doc_text(b,"&quot;")) return false; }
        else if(c<32 && c!=9 && c!=10 && c!=13) return false;
        else if(!doc_append(b,&c,1)) return false;
    }
    return true;
}
static bool doc_utf16_xml(doc_bytes *b,const uint8_t *p,size_t n) {
    size_t i; uint8_t utf[4];
    if(n&1) return false;
    for(i=0;i<n;i+=2) {
        uint32_t c=pm_be16(p+i); size_t bytes;
        if(c>=0xd800 && c<=0xdbff) {
            uint32_t low; if(i+4>n || (low=pm_be16(p+i+2))<0xdc00 || low>0xdfff) return false;
            c=0x10000+((c-0xd800)<<10)+(low-0xdc00); i+=2;
        } else if(c>=0xdc00 && c<=0xdfff) return false;
        if(c<0x80) { utf[0]=(uint8_t)c; bytes=1; }
        else if(c<0x800) { utf[0]=(uint8_t)(0xc0|(c>>6)); utf[1]=(uint8_t)(0x80|(c&63)); bytes=2; }
        else if(c<0x10000) { utf[0]=(uint8_t)(0xe0|(c>>12)); utf[1]=(uint8_t)(0x80|((c>>6)&63)); utf[2]=(uint8_t)(0x80|(c&63)); bytes=3; }
        else { utf[0]=(uint8_t)(0xf0|(c>>18)); utf[1]=(uint8_t)(0x80|((c>>12)&63)); utf[2]=(uint8_t)(0x80|((c>>6)&63)); utf[3]=(uint8_t)(0x80|(c&63)); bytes=4; }
        if(!doc_xml(b,utf,bytes)) return false;
    }
    return true;
}
typedef struct doc_qm_message { const uint8_t *context,*source,*comment,*translations[64]; size_t cn,sn,mn,tn[64],count; bool c16,s16; } doc_qm_message;
static bool doc_qm(Abstractformat *f,pm_stream *s,const uint8_t *p,size_t n,xx_pd_struct *pd) {
    static const uint8_t magic[16]={0x3c,0xb8,0x64,0x18,0xca,0xef,0x9c,0x95,0xcd,0x21,0x1c,0xbf,0x60,0xa1,0xbd,0xdd};
    const uint8_t *hashes=NULL,*messages=NULL,*language=NULL; size_t hn=0,mn=0,ln=0,at=16,i; doc_bytes out={0}; bool ok=false; doc_qm_message m={0};
    if(n<16 || xx_rt_memcmp(p,magic,16)) return false;
    while(at<n) {
        uint8_t tag; uint32_t len;
        if(n-at<5) { return false; } tag=p[at++]; len=pm_be32(p+at); at+=4;
        if(len>n-at || !tag) return false;
        if(tag==0x42) { if(hashes) return false; hashes=p+at; hn=len; }
        if(tag==0x69) { if(messages) return false; messages=p+at; mn=len; }
        if(tag==0xa7) { language=p+at; ln=len; }
        at+=len;
    }
    if(hn%8 || hn/8>1000000 || (hn && !messages)) return false;
    out.limit=doc_limit(f)/2;
    if(!doc_text(&out,"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<TS version=\"2.1\" language=\"") ||
       !doc_xml(&out,language,ln) || !doc_text(&out,"\">\n")) goto done;
    for(i=0;i<hn;i+=8) {
        uint32_t offset=pm_be32(hashes+i+4); size_t cursor=offset,j; bool end=false; char id[64];
        if(offset>=mn || (pd && xx_pd_is_stopped(pd))) goto done;
        m.count=0;
        while(cursor<mn) {
            uint8_t tag=messages[cursor++]; uint32_t len;
            if(tag==1) { end=true; break; }
            if(mn-cursor<4) { goto done; } len=pm_be32(messages+cursor); cursor+=4;
            if(tag==5) continue;
            if(len==UINT32_MAX) len=0;
            if(len>mn-cursor) goto done;
            if(tag==2 || tag==6) { m.source=messages+cursor; m.sn=len; m.s16=tag==2; }
            else if(tag==4 || tag==7) { m.context=messages+cursor; m.cn=len; m.c16=tag==4; }
            else if(tag==8) { m.comment=messages+cursor; m.mn=len; }
            else if(tag==3) { if(m.count>=64 || (len&1)) goto done; m.translations[m.count]=messages+cursor; m.tn[m.count++]=len; }
            else if(tag!=9) goto done;
            cursor+=len;
        }
        if(!end || !m.count) goto done;
        if(!doc_text(&out,"<context><name>") || !(m.c16?doc_utf16_xml(&out,m.context,m.cn):doc_xml(&out,m.context,m.cn)) || !doc_text(&out,"</name>\n")) goto done;
        xx_rt_snprintf(id,sizeof(id),"<message id=\"qm-%08x\"%s><source>",pm_be32(hashes+i),m.count>1?" numerus=\"yes\"":"");
        if(!doc_text(&out,id) || !(m.s16?doc_utf16_xml(&out,m.source,m.sn):doc_xml(&out,m.source,m.sn)) || !doc_text(&out,"</source>")) goto done;
        if(m.mn && (!doc_text(&out,"<comment>") || !doc_xml(&out,m.comment,m.mn) || !doc_text(&out,"</comment>"))) goto done;
        if(!doc_text(&out,"<translation>")) goto done;
        for(j=0;j<m.count;++j) {
            if(m.count>1 && !doc_text(&out,"<numerusform>")) goto done;
            if(!doc_utf16_xml(&out,m.translations[j],m.tn[j])) goto done;
            if(m.count>1 && !doc_text(&out,"</numerusform>")) goto done;
        }
        if(!doc_text(&out,"</translation></message></context>\n")) goto done;
    }
    if(!doc_text(&out,"</TS>\n")) goto done;
    ok=doc_member(f,s,"messages.ts",&out);
done: xx_mem_free(out.p); return ok;
}

typedef struct doc_mime_header { char type[256],encoding[64],disposition[1024],location[1024]; bool marker; } doc_mime_header;
static bool doc_ci(const uint8_t *p,size_t n,const char *text) {
    size_t i; if(n!=xx_rt_strlen(text)) return false;
    for(i=0;i<n;++i) { uint8_t c=p[i],d=(uint8_t)text[i]; if(c>='A' && c<='Z') c+=32; if(d>='A' && d<='Z') d+=32; if(c!=d) return false; } return true;
}
static void doc_trim(const uint8_t **p,size_t *n) {
    while(*n && (**p==' ' || **p=='\t')) { ++*p; --*n; }
    while(*n && ((*p)[*n-1]==' ' || (*p)[*n-1]=='\t' || (*p)[*n-1]=='\r')) --*n;
}
static bool doc_headers(const uint8_t *p,size_t n,size_t *body,doc_mime_header *h) {
    size_t at=0,field=0; char *target=NULL; size_t target_cap=0; bool any=false;
    while(at<n) {
        size_t start=at,end,len,colon; const uint8_t *value;
        while(at<n && p[at]!='\n') { if(p[at]==0 || (p[at]<32 && p[at]!=9 && p[at]!=13)) return false; ++at; }
        end=at; if(at<n) ++at; if(end>start && p[end-1]=='\r') --end;
        if(end==start) { *body=at; return any; }
        if(at>1024U*1024U || ++field>10000) return false;
        if(p[start]==' ' || p[start]=='\t') {
            if(!any) return false;
            value=p+start; len=end-start; doc_trim(&value,&len);
            if(target) { size_t old=xx_rt_strlen(target); if(old+1+len>=target_cap) return false; target[old]=' '; xx_rt_memcpy(target+old+1,value,len); target[old+1+len]=0; }
            continue;
        }
        colon=start; while(colon<end && p[colon]!=':') ++colon;
        if(colon==start || colon==end) return false;
        target=NULL; target_cap=0; any=true;
        if(doc_ci(p+start,colon-start,"content-type")) { target=h->type; target_cap=sizeof(h->type); h->marker=true; }
        else if(doc_ci(p+start,colon-start,"content-transfer-encoding")) { target=h->encoding; target_cap=sizeof(h->encoding); }
        else if(doc_ci(p+start,colon-start,"content-disposition")) { target=h->disposition; target_cap=sizeof(h->disposition); }
        else if(doc_ci(p+start,colon-start,"content-location")) { target=h->location; target_cap=sizeof(h->location); }
        else if(doc_ci(p+start,colon-start,"mime-version") || doc_ci(p+start,colon-start,"from") || doc_ci(p+start,colon-start,"subject")) h->marker=true;
        value=p+colon+1; len=end-colon-1; doc_trim(&value,&len);
        if(target) { if(len>=target_cap) return false; xx_rt_memcpy(target,value,len); target[len]=0; }
    }
    return false;
}
static int doc_hex(uint8_t c) { if(c>='0' && c<='9') return c-'0'; if(c>='a' && c<='f') return c-'a'+10; if(c>='A' && c<='F') return c-'A'+10; return -1; }
static bool doc_parameter(const char *field,const char *key,char *out,size_t cap) {
    const uint8_t *p=(const uint8_t *)field; size_t n=xx_rt_strlen(field),at=0;
    while(at<n) {
        size_t start,len,k=0; bool quoted=false;
        while(at<n && p[at]!=';') ++at;
        if(at==n) { return false; } ++at;
        while(at<n && (p[at]==' ' || p[at]=='\t')) ++at;
        start=at; while(at<n && p[at]!='=' && p[at]!=';' && p[at]!=' ' && p[at]!='\t') ++at; len=at-start;
        while(at<n && (p[at]==' ' || p[at]=='\t')) ++at;
        if(at==n || p[at]!='=') { continue; } ++at;
        while(at<n && (p[at]==' ' || p[at]=='\t')) ++at;
        if(at<n && p[at]=='"') { quoted=true; ++at; }
        if(!doc_ci(p+start,len,key)) { if(quoted) { while(at<n && p[at]!='"') { if(p[at]=='\\' && at+1<n) ++at; ++at; } if(at<n) ++at; } continue; }
        while(at<n && (quoted?p[at]!='"':p[at]!=';' && p[at]!=' ' && p[at]!='\t')) {
            uint8_t c=p[at++]; if(quoted && c=='\\') { if(at==n) return false; c=p[at++]; }
            if(c<32 || k+1>=cap) { return false; } out[k++]=(char)c;
        }
        if(quoted && (at==n || p[at]!='"')) return false;
        out[k]=0; return k!=0;
    }
    return false;
}
static int doc_b64(uint8_t c) { if(c>='A' && c<='Z') return c-'A'; if(c>='a' && c<='z') return c-'a'+26; if(c>='0' && c<='9') return c-'0'+52; return c=='+'?62:c=='/'?63:-1; }
static bool doc_decode(doc_bytes *out,const uint8_t *p,size_t n,const char *encoding) {
    size_t i; bool b64=doc_ci((const uint8_t *)encoding,xx_rt_strlen(encoding),"base64");
    bool qp=doc_ci((const uint8_t *)encoding,xx_rt_strlen(encoding),"quoted-printable");
    if(b64) {
        uint8_t q[4]; size_t used=0; bool end=false;
        for(i=0;i<n;++i) {
            uint8_t c=p[i],v[3]; int a,b,d,e;
            if(c==' ' || c=='\t' || c=='\r' || c=='\n') continue;
            if(end || (c!='=' && doc_b64(c)<0)) return false;
            q[used++]=c; if(used<4) continue;
            a=doc_b64(q[0]); b=doc_b64(q[1]); d=doc_b64(q[2]); e=doc_b64(q[3]);
            if(a<0 || b<0 || (q[2]=='=' && q[3]!='=') || (q[2]=='=' && (b&15)) || (q[3]=='=' && q[2]!='=' && (d&3))) return false;
            v[0]=(uint8_t)((a<<2)|(b>>4)); v[1]=(uint8_t)((b<<4)|((d<0?0:d)>>2)); v[2]=(uint8_t)(((d<0?0:d)<<6)|(e<0?0:e));
            if(!doc_append(out,v,q[2]=='='?1:q[3]=='='?2:3)) return false;
            end=q[3]=='='; used=0;
        }
        return used==0;
    }
    if(qp) {
        for(i=0;i<n;++i) {
            uint8_t c=p[i];
            if(c=='=') {
                int a,b;
                if(i+1<n && p[i+1]=='\n') { ++i; continue; }
                if(i+2<n && p[i+1]=='\r' && p[i+2]=='\n') { i+=2; continue; }
                if(i+2>=n || (a=doc_hex(p[i+1]))<0 || (b=doc_hex(p[i+2]))<0) return false;
                c=(uint8_t)((a<<4)|b); i+=2;
            }
            if(!doc_append(out,&c,1)) return false;
        }
        return true;
    }
    if(encoding[0] && !doc_ci((const uint8_t *)encoding,xx_rt_strlen(encoding),"7bit") &&
       !doc_ci((const uint8_t *)encoding,xx_rt_strlen(encoding),"8bit") &&
       !doc_ci((const uint8_t *)encoding,xx_rt_strlen(encoding),"binary")) return false;
    return doc_append(out,p,n);
}
static bool doc_mime_part(Abstractformat *f,pm_stream *s,const uint8_t *p,size_t n,unsigned depth,size_t *budget,xx_pd_struct *pd) {
    doc_mime_header h={0}; size_t body=0; char boundary[256],name[1024]; doc_bytes data={0}; bool ok=false;
    if(depth>32 || (pd && xx_pd_is_stopped(pd)) || !doc_headers(p,n,&body,&h) || (!depth && !h.marker)) return false;
    if(xx_rt_strlen(h.type)==9 && doc_ci((const uint8_t *)h.type,9,"multipart")) return false; /* bare token has no subtype */
    if(xx_rt_strlen(h.type)>10 && doc_ci((const uint8_t *)h.type,10,"multipart/")) {
        size_t at=body,part=SIZE_MAX; bool closed=false;
        if(!doc_parameter(h.type,"boundary",boundary,sizeof(boundary))) return false;
        while(at<n) {
            size_t start=at,end,trim,len=xx_rt_strlen(boundary); bool closing;
            while(at<n && p[at]!='\n') { ++at; } end=at; if(at<n) ++at; if(end>start && p[end-1]=='\r') --end;
            if(end-start<len+2 || p[start]!='-' || p[start+1]!='-' || xx_rt_memcmp(p+start+2,boundary,len)) continue;
            trim=start+2+len; closing=trim+2<=end && p[trim]=='-' && p[trim+1]=='-'; if(closing) trim+=2;
            while(trim<end && (p[trim]==' ' || p[trim]=='\t')) ++trim;
            if(trim!=end) continue;
            if(part!=SIZE_MAX) { size_t stop=start; if(stop>part && p[stop-1]=='\n') { --stop; if(stop>part && p[stop-1]=='\r') --stop; }
                if(!doc_mime_part(f,s,p+part,stop-part,depth+1,budget,pd)) return false;
            }
            if(closing) { closed=true; break; } part=at;
        }
        return closed;
    }
    data.limit=*budget;
    if(!doc_decode(&data,p+body,n-body,h.encoding)) goto done;
    if(xx_rt_strlen(h.type)>=14 && doc_ci((const uint8_t *)h.type,14,"message/rfc822")) {
        ok=doc_mime_part(f,s,data.p,data.n,depth+1,budget,pd); goto done;
    }
    name[0]=0;
    if(!doc_parameter(h.disposition,"filename",name,sizeof(name)) && !doc_parameter(h.type,"name",name,sizeof(name))) {
        if(h.location[0]) { const char *leaf=h.location,*q=h.location; while(*q) { if(*q=='/' || *q=='\\') leaf=q+1; ++q; } xx_rt_snprintf(name,sizeof(name),"%s",leaf); }
        if(!name[0]) xx_rt_snprintf(name,sizeof(name),"body.%s",xx_rt_strlen(h.type)>=9 && doc_ci((const uint8_t *)h.type,9,"text/html")?"html":"txt");
    }
    if(data.n>*budget) { goto done; } *budget-=data.n;
    ok=doc_member(f,s,name,&data);
done: xx_mem_free(data.p); return ok;
}
static bool doc_hlp(Abstractformat *f,pm_stream *s,const uint8_t *p,size_t n,xx_pd_struct *pd) {
    uint32_t dir,size,reserved,used;const uint8_t *h,*pages;uint16_t page_size,count,root,levels,cur,previous=0xffff;
    uint8_t *seen=NULL;size_t i,listed=0;char last[256]={0};bool ok=false;
    if(n<16 || pm_le32(p)!=0x00035f3fU)return false;
    dir=pm_le32(p+4);size=pm_le32(p+12);
    if(size>n || size<16 || dir<16 || dir>size || size-dir<47)return false;
    reserved=pm_le32(p+dir);used=pm_le32(p+dir+4);
    if(reserved>size-dir || reserved<47 || used>reserved-9 || used<38)return false;
    h=p+dir+9;if(pm_le16(h)!=0x293b)return false;
    page_size=pm_le16(h+4);root=pm_le16(h+26);count=pm_le16(h+30);levels=pm_le16(h+32);
    if(page_size<64 || page_size>32768 || !count || root>=count || !levels || levels>64 || (uint64_t)page_size*count>used-38)return false;
    pages=h+38;seen=(uint8_t *)xx_mem_calloc(count,1);if(!seen)return false;
    cur=root;
    for(i=1;i<levels;++i) {
        const uint8_t *page=pages+(size_t)cur*page_size;
        if(cur>=count || seen[cur] || pm_le16(page)>page_size-6)goto done;
        seen[cur]=1;cur=pm_le16(page+4);if(cur>=count)goto done;
    }
    while(cur!=0xffff) {
        const uint8_t *page;size_t at=8,end;uint16_t entries,next,j;
        if(cur>=count || seen[cur] || (pd && xx_pd_is_stopped(pd)))goto done;
        seen[cur]=1;page=pages+(size_t)cur*page_size;entries=pm_le16(page+2);next=pm_le16(page+6);
        if(pm_le16(page)>page_size-8 || pm_le16(page+4)!=previous) {goto done; } end=page_size-pm_le16(page);
        for(j=0;j<entries;++j) {
            size_t start=at,len;uint32_t offset,allocation,bytes;char name[256];
            while(at<end && page[at]) {++at; } len=at-start;
            if(!len || len>=sizeof(name) || at>=end || end-at<5)goto done;
            xx_rt_memcpy(name,page+start,len);name[len]=0;offset=pm_le32(page+at+1);at+=5;
            if(last[0] && xx_rt_strcmp(last,name)>=0) {goto done; } xx_rt_memcpy(last,name,len+1);
            if(offset<16 || offset>size || size-offset<9)goto done;
            allocation=pm_le32(p+offset);bytes=pm_le32(p+offset+4);
            if(allocation<9 || allocation>size-offset || bytes>allocation-9 || !pm_add(f,s,name,offset+9,bytes))goto done;
            if(++listed>1000000)goto done;
        }
        if(at!=end) {goto done; } previous=cur;cur=next;
    }
    if(listed!=pm_le32(h+34))goto done;
    s->size=size;ok=true;
done:xx_mem_free(seen);return ok;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    int64_t available=pm_available(f); size_t limit=doc_limit(f),n,budget; uint8_t *p; bool ok=false;
    if(available<0 || (uint64_t)available>limit/2 || (pd && xx_pd_is_stopped(pd))) return false;
    n=(size_t)available; p=(uint8_t *)xx_mem_alloc(n?n:1); if(!p || !pm_read(f,0,p,n)) { xx_mem_free(p); return false; }
    if(f->file_type==XX_FILE_TYPE_GNU_GETTEXT_MO) ok=doc_mo(f,s,p,n,pd);
    else if(f->file_type==XX_FILE_TYPE_QT_QM) ok=doc_qm(f,s,p,n,pd);
    else if(f->file_type==XX_FILE_TYPE_MIME_MESSAGE) { budget=limit-n; ok=doc_mime_part(f,s,p,n,0,&budget,pd); }
    else if(f->file_type==XX_FILE_TYPE_WINDOWS_HELP) ok=doc_hlp(f,s,p,n,pd);
    if(f->file_type!=XX_FILE_TYPE_WINDOWS_HELP) {s->size=available; } xx_mem_free(p); return ok;
}
xx_ue2_documents *xx_ue2_documents_create(xx_io_device *d,int64_t b,xx_file_type_t type) {
    xx_ue2_documents *r=(xx_ue2_documents *)xx_mem_alloc(sizeof(*r));
    if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,type,type==XX_FILE_TYPE_GNU_GETTEXT_MO?"mo":type==XX_FILE_TYPE_QT_QM?"qm":type==XX_FILE_TYPE_WINDOWS_HELP?"hlp":"eml"); }
    return r;
}
void xx_ue2_documents_free(xx_ue2_documents *r) { if(r) { xx_format_cleanup_extra_parameters(&r->format); xx_mem_free(r); } }
xx_file_type_t xx_ue2_documents_detect_device(xx_io_device *d) {
    uint8_t h[1024]; int64_t cursor,size; size_t n; xx_file_type_t type=XX_FILE_TYPE_UNKNOWN; xx_ue2_documents *r;
    if(!d || (size=xx_io_size(d))<16) return type;
    cursor=xx_io_tell(d); n=size<(int64_t)sizeof(h)?(size_t)size:sizeof(h);
    if(xx_io_seek64(d,0,SEEK_SET)!=0 || xx_io_read(d,h,n)!=(ssize_t)n) goto done;
    if(pm_le32(h)==0x950412deU || pm_be32(h)==0x950412deU) type=XX_FILE_TYPE_GNU_GETTEXT_MO;
    else if(pm_le32(h)==0x00035f3fU)type=XX_FILE_TYPE_WINDOWS_HELP;
    else if(pm_be32(h)==0x3cb86418U && pm_be32(h+4)==0xcaef9c95U && pm_be32(h+8)==0xcd211cbfU && pm_be32(h+12)==0x60a1bdddU) type=XX_FILE_TYPE_QT_QM;
    else {
        size_t at=0;
        while(at<n) {
            size_t start=at; while(at<n && h[at]!='\n') ++at;
            if(at-start>=5 && (doc_ci(h+start,5,"from:") || (at-start>=8 && doc_ci(h+start,8,"subject:")) ||
               (at-start>=13 && doc_ci(h+start,13,"mime-version:")) || (at-start>=13 && doc_ci(h+start,13,"content-type:")))) { type=XX_FILE_TYPE_MIME_MESSAGE; break; }
            if(at<n) ++at;
        }
    }
    if(type!=XX_FILE_TYPE_UNKNOWN) {
        r=xx_ue2_documents_create(d,0,type);
        if(!r || !r->format.check_is_valid(&r->format,NULL)) type=XX_FILE_TYPE_UNKNOWN;
        xx_ue2_documents_free(r);
    }
done: (void)xx_io_seek64(d,cursor,SEEK_SET); return type;
}
