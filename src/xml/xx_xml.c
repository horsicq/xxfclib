/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * See xx_xml.h. A pull parser over the text, matching xx_json's shape.
 *
 * The scope is deliberate. Manifests and configuration files use elements,
 * attributes, text, CDATA and the predefined entities, so those are handled
 * exactly. DTDs, entity declarations and namespace resolution are not: a
 * DOCTYPE is skipped rather than interpreted, which also closes the billion
 * laughs entity-expansion attack by construction rather than by a limit that
 * has to be tuned.
 *
 * Well-formedness is checked as far as a reader needs to trust what it sees:
 * every end tag must match the start tag it closes, and the nesting depth is
 * bounded. What is NOT checked is anything requiring a schema, since a reader
 * asking whether a file is an APK manifest has no schema to check against.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/xml/xx_xml.h"

#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Per-cursor state. Everything the walk needs lives in the cursor, so two
 * documents can be walked at once and nothing is shared between threads. */

static void xx_xml_clear_node(xx_xml *xml) {
    size_t index;

    xx_str_free(xml->name);
    xx_str_free(xml->text);
    xml->name = NULL;
    xml->text = NULL;
    for (index = 0U; index < xml->attribute_count; ++index) {
        xx_str_free(xml->attributes[index].name);
        xx_str_free(xml->attributes[index].value);
        xml->attributes[index].name = NULL;
        xml->attributes[index].value = NULL;
    }
    xml->attribute_count = 0U;
    xml->self_closing = false;
    xml->type = XX_XML_NONE;
}

void xx_xml_init(xx_xml *xml, const void *data, size_t size) {
    if (!xml) return;
    xx_mem_zero(xml, sizeof(*xml));
    xml->data = (const uint8_t *)data;
    xml->size = data ? size : 0U;
}

void xx_xml_cleanup(xx_xml *xml) {
    int index;

    if (!xml) return;
    xx_xml_clear_node(xml);
    xx_mem_free(xml->attributes);
    xml->attributes = NULL;
    xml->attribute_capacity = 0U;
    for (index = 0; index < xml->depth; ++index) {
        xx_str_free(xml->open_names[index]);
    }
    xx_mem_free(xml->open_names);
    xml->open_names = NULL;
    xml->depth = 0;
    xml->open_capacity = 0;
}

static bool xx_xml_reserve_attribute(xx_xml *xml) {
    xx_xml_attribute *grown;
    size_t wanted;

    if (xml->attribute_count < xml->attribute_capacity) return true;
    if (xml->attribute_count >= XX_XML_MAX_ATTRIBUTES) return false;
    wanted = xml->attribute_capacity ? xml->attribute_capacity * 2U : 8U;
    if (wanted > XX_XML_MAX_ATTRIBUTES) wanted = XX_XML_MAX_ATTRIBUTES;
    grown = (xx_xml_attribute *)xx_mem_realloc(xml->attributes,
                                               sizeof(*grown) * wanted);
    if (!grown) return false;
    xml->attributes = grown;
    xml->attribute_capacity = wanted;
    return true;
}

static bool xx_xml_push(xx_xml *xml, const char *name) {
    if (xml->depth >= XX_XML_MAX_DEPTH) return false;
    if (xml->depth >= xml->open_capacity) {
        char **grown;
        int wanted = xml->open_capacity ? xml->open_capacity * 2 : 16;
        if (wanted > XX_XML_MAX_DEPTH) wanted = XX_XML_MAX_DEPTH;
        grown = (char **)xx_mem_realloc(xml->open_names,
                                        sizeof(*grown) * (size_t)wanted);
        if (!grown) return false;
        xml->open_names = grown;
        xml->open_capacity = wanted;
    }
    xml->open_names[xml->depth] = xx_str_dup(name);
    if (!xml->open_names[xml->depth]) return false;
    ++xml->depth;
    return true;
}

static bool xx_xml_is_space(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* XML names are broader than this in principle, but every byte above 0x7F is
 * accepted so a UTF-8 name passes through intact. */
static bool xx_xml_is_name_byte(uint8_t c, bool first) {
    if (c >= 0x80U) return true;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
        c == ':') {
        return true;
    }
    if (first) return false;
    return (c >= '0' && c <= '9') || c == '-' || c == '.';
}

static void xx_xml_skip_space(xx_xml *xml) {
    while (xml->position < xml->size &&
           xx_xml_is_space(xml->data[xml->position])) {
        ++xml->position;
    }
}

static bool xx_xml_starts_with(const xx_xml *xml, const char *text) {
    size_t length = xx_str_len(text);

    return xml->position + length <= xml->size &&
           xx_rt_memcmp(xml->data + xml->position, text, length) == 0;
}

/* Find @p text at or after the cursor; returns its offset, or the size when
 * absent. */
static size_t xx_xml_find(const xx_xml *xml, const char *text) {
    size_t length = xx_str_len(text);
    size_t index;

    if (length == 0U || xml->size < length) return xml->size;
    for (index = xml->position; index + length <= xml->size; ++index) {
        if (xx_rt_memcmp(xml->data + index, text, length) == 0) return index;
    }
    return xml->size;
}

static char *xx_xml_dup_range(const xx_xml *xml, size_t start, size_t end) {
    char *result;
    size_t length;

    if (end < start) return NULL;
    length = end - start;
    result = (char *)xx_mem_alloc(length + 1U);
    if (!result) return NULL;
    if (length != 0U) xx_rt_memcpy(result, xml->data + start, length);
    result[length] = '\0';
    return result;
}

static bool xx_xml_append(char **buffer, size_t *length, size_t *capacity,
                          const char *bytes, size_t count) {
    if (*length + count + 1U > *capacity) {
        char *grown;
        size_t wanted = *capacity ? *capacity : 32U;
        while (*length + count + 1U > wanted) wanted *= 2U;
        grown = (char *)xx_mem_realloc(*buffer, wanted);
        if (!grown) return false;
        *buffer = grown;
        *capacity = wanted;
    }
    if (count != 0U) xx_rt_memcpy(*buffer + *length, bytes, count);
    *length += count;
    (*buffer)[*length] = '\0';
    return true;
}

static bool xx_xml_append_codepoint(char **buffer, size_t *length,
                                    size_t *capacity, uint32_t code) {
    uint8_t encoded[4];
    size_t encoded_length;

    if (code < 0x80U) {
        encoded[0] = (uint8_t)code;
        encoded_length = 1U;
    } else if (code < 0x800U) {
        encoded[0] = (uint8_t)(0xC0U | (code >> 6));
        encoded[1] = (uint8_t)(0x80U | (code & 0x3FU));
        encoded_length = 2U;
    } else if (code < 0x10000U) {
        encoded[0] = (uint8_t)(0xE0U | (code >> 12));
        encoded[1] = (uint8_t)(0x80U | ((code >> 6) & 0x3FU));
        encoded[2] = (uint8_t)(0x80U | (code & 0x3FU));
        encoded_length = 3U;
    } else if (code <= 0x10FFFFU) {
        encoded[0] = (uint8_t)(0xF0U | (code >> 18));
        encoded[1] = (uint8_t)(0x80U | ((code >> 12) & 0x3FU));
        encoded[2] = (uint8_t)(0x80U | ((code >> 6) & 0x3FU));
        encoded[3] = (uint8_t)(0x80U | (code & 0x3FU));
        encoded_length = 4U;
    } else {
        return false;
    }
    return xx_xml_append(buffer, length, capacity, (const char *)encoded,
                         encoded_length);
}

/*
 * Resolve one entity reference beginning at the ampersand. Only the five
 * predefined names and numeric references are recognised, because anything
 * else would need a DTD -- and resolving DTD-declared entities is exactly the
 * expansion attack this parser avoids by not implementing them.
 */
static bool xx_xml_entity(xx_xml *xml, char **buffer, size_t *length,
                          size_t *capacity) {
    size_t start = xml->position + 1U; /* past '&' */
    size_t end = start;
    size_t name_length;

    while (end < xml->size && xml->data[end] != ';') {
        if (xx_xml_is_space(xml->data[end]) || xml->data[end] == '<') {
            return false; /* unterminated reference */
        }
        ++end;
    }
    if (end >= xml->size) return false;
    name_length = end - start;
    if (name_length == 0U) return false;

    if (xml->data[start] == '#') {
        uint32_t code = 0U;
        size_t index = start + 1U;
        bool hex = index < end && (xml->data[index] == 'x' ||
                                   xml->data[index] == 'X');

        if (hex) ++index;
        if (index >= end) return false;
        for (; index < end; ++index) {
            uint8_t c = xml->data[index];
            uint32_t digit;
            if (c >= '0' && c <= '9') {
                digit = (uint32_t)(c - '0');
            } else if (hex && c >= 'a' && c <= 'f') {
                digit = (uint32_t)(c - 'a' + 10);
            } else if (hex && c >= 'A' && c <= 'F') {
                digit = (uint32_t)(c - 'A' + 10);
            } else {
                return false;
            }
            if (code > (0x10FFFFU - digit) / (hex ? 16U : 10U)) return false;
            code = code * (hex ? 16U : 10U) + digit;
        }
        if (!xx_xml_append_codepoint(buffer, length, capacity, code)) {
            return false;
        }
    } else {
        const char *replacement = NULL;
        if (name_length == 2U &&
            xx_rt_memcmp(xml->data + start, "lt", 2U) == 0) {
            replacement = "<";
        } else if (name_length == 2U &&
                   xx_rt_memcmp(xml->data + start, "gt", 2U) == 0) {
            replacement = ">";
        } else if (name_length == 3U &&
                   xx_rt_memcmp(xml->data + start, "amp", 3U) == 0) {
            replacement = "&";
        } else if (name_length == 4U &&
                   xx_rt_memcmp(xml->data + start, "quot", 4U) == 0) {
            replacement = "\"";
        } else if (name_length == 4U &&
                   xx_rt_memcmp(xml->data + start, "apos", 4U) == 0) {
            replacement = "'";
        } else {
            return false; /* needs a DTD, which is out of scope */
        }
        if (!xx_xml_append(buffer, length, capacity, replacement, 1U)) {
            return false;
        }
    }
    xml->position = end + 1U;
    return true;
}

/* Read characters until @p stop, resolving entities. */
static char *xx_xml_read_text(xx_xml *xml, char stop) {
    char *buffer = NULL;
    size_t length = 0U;
    size_t capacity = 0U;

    if (!xx_xml_append(&buffer, &length, &capacity, "", 0U)) return NULL;
    while (xml->position < xml->size && xml->data[xml->position] != (uint8_t)stop) {
        if (xml->data[xml->position] == '&') {
            if (!xx_xml_entity(xml, &buffer, &length, &capacity)) goto fail;
            continue;
        }
        if (!xx_xml_append(&buffer, &length, &capacity,
                           (const char *)(xml->data + xml->position), 1U)) {
            goto fail;
        }
        ++xml->position;
    }
    return buffer;

fail:
    xx_str_free(buffer);
    return NULL;
}

static char *xx_xml_read_name(xx_xml *xml) {
    size_t start = xml->position;

    if (xml->position >= xml->size ||
        !xx_xml_is_name_byte(xml->data[xml->position], true)) {
        return NULL;
    }
    ++xml->position;
    while (xml->position < xml->size &&
           xx_xml_is_name_byte(xml->data[xml->position], false)) {
        ++xml->position;
    }
    return xx_xml_dup_range(xml, start, xml->position);
}

static bool xx_xml_fail(xx_xml *xml) {
    xx_xml_clear_node(xml);
    xml->failed = true;
    return false;
}

static bool xx_xml_read_attributes(xx_xml *xml) {
    for (;;) {
        char *name;
        char *value;
        char quote;

        xx_xml_skip_space(xml);
        if (xml->position >= xml->size) return false;
        if (xml->data[xml->position] == '>' ||
            xml->data[xml->position] == '/') {
            return true;
        }
        if (!xx_xml_reserve_attribute(xml)) return false;

        name = xx_xml_read_name(xml);
        if (!name) return false;
        xx_xml_skip_space(xml);
        if (xml->position >= xml->size || xml->data[xml->position] != '=') {
            xx_str_free(name);
            return false;
        }
        ++xml->position;
        xx_xml_skip_space(xml);
        if (xml->position >= xml->size) {
            xx_str_free(name);
            return false;
        }
        quote = (char)xml->data[xml->position];
        if (quote != '"' && quote != '\'') {
            xx_str_free(name);
            return false;
        }
        ++xml->position;
        value = xx_xml_read_text(xml, quote);
        if (!value || xml->position >= xml->size) {
            xx_str_free(name);
            xx_str_free(value);
            return false;
        }
        ++xml->position; /* closing quote */
        xml->attributes[xml->attribute_count].name = name;
        xml->attributes[xml->attribute_count].value = value;
        ++xml->attribute_count;
    }
}

bool xx_xml_next(xx_xml *xml) {
    if (!xml || xml->failed) return false;
    xx_xml_clear_node(xml);

    if (xml->position >= xml->size) {
        /* An element left open at the end of the document is malformed. */
        if (xml->depth != 0) return xx_xml_fail(xml);
        return false;
    }

    /* Character data between markup. */
    if (xml->data[xml->position] != '<') {
        size_t start = xml->position;
        char *text = xx_xml_read_text(xml, '<');
        if (!text) return xx_xml_fail(xml);
        if (xml->position == start) {
            xx_str_free(text);
            return xx_xml_fail(xml);
        }
        xml->type = XX_XML_TEXT;
        xml->text = text;
        return true;
    }

    if (xx_xml_starts_with(xml, "<!--")) {
        size_t start;
        size_t end;
        xml->position += 4U;
        start = xml->position;
        end = xx_xml_find(xml, "-->");
        if (end >= xml->size) return xx_xml_fail(xml);
        xml->text = xx_xml_dup_range(xml, start, end);
        if (!xml->text) return xx_xml_fail(xml);
        xml->position = end + 3U;
        xml->type = XX_XML_COMMENT;
        return true;
    }
    if (xx_xml_starts_with(xml, "<![CDATA[")) {
        size_t start;
        size_t end;
        xml->position += 9U;
        start = xml->position;
        end = xx_xml_find(xml, "]]>");
        if (end >= xml->size) return xx_xml_fail(xml);
        /* CDATA is character data with no entity resolution, so it is
         * reported as text rather than as a kind of its own. */
        xml->text = xx_xml_dup_range(xml, start, end);
        if (!xml->text) return xx_xml_fail(xml);
        xml->position = end + 3U;
        xml->type = XX_XML_TEXT;
        return true;
    }
    if (xx_xml_starts_with(xml, "<!DOCTYPE")) {
        /* Skipped rather than interpreted: see the file comment. Internal
         * subsets nest brackets, so those are tracked. */
        int brackets = 0;
        xml->position += 9U;
        while (xml->position < xml->size) {
            uint8_t c = xml->data[xml->position++];
            if (c == '[') ++brackets;
            else if (c == ']') --brackets;
            else if (c == '>' && brackets <= 0) {
                xml->type = XX_XML_DOCTYPE;
                return true;
            }
        }
        return xx_xml_fail(xml);
    }
    if (xx_xml_starts_with(xml, "<?")) {
        size_t start;
        size_t end;
        xml->position += 2U;
        start = xml->position;
        end = xx_xml_find(xml, "?>");
        if (end >= xml->size) return xx_xml_fail(xml);
        xml->text = xx_xml_dup_range(xml, start, end);
        if (!xml->text) return xx_xml_fail(xml);
        xml->position = end + 2U;
        xml->type = XX_XML_PI;
        return true;
    }
    if (xx_xml_starts_with(xml, "</")) {
        char *name;
        xml->position += 2U;
        name = xx_xml_read_name(xml);
        if (!name) return xx_xml_fail(xml);
        xx_xml_skip_space(xml);
        if (xml->position >= xml->size || xml->data[xml->position] != '>') {
            xx_str_free(name);
            return xx_xml_fail(xml);
        }
        ++xml->position;
        /* The end tag must close the element actually open. */
        if (xml->depth == 0 ||
            xx_str_cmp(xml->open_names[xml->depth - 1], name) != 0) {
            xx_str_free(name);
            return xx_xml_fail(xml);
        }
        --xml->depth;
        xx_str_free(xml->open_names[xml->depth]);
        xml->open_names[xml->depth] = NULL;
        xml->type = XX_XML_END;
        xml->name = name;
        return true;
    }

    /* A start element. */
    ++xml->position; /* past '<' */
    xml->name = xx_xml_read_name(xml);
    if (!xml->name) return xx_xml_fail(xml);
    if (!xx_xml_read_attributes(xml)) return xx_xml_fail(xml);
    if (xml->position < xml->size && xml->data[xml->position] == '/') {
        ++xml->position;
        xml->self_closing = true;
    }
    if (xml->position >= xml->size || xml->data[xml->position] != '>') {
        return xx_xml_fail(xml);
    }
    ++xml->position;
    /* A self-closing element is reported without being pushed: it opens and
     * closes in one node, so nothing is left for an end tag to match. */
    if (!xml->self_closing && !xx_xml_push(xml, xml->name)) {
        return xx_xml_fail(xml);
    }
    xml->type = XX_XML_START;
    return true;
}

bool xx_xml_failed(const xx_xml *xml) {
    return xml ? xml->failed : true;
}

xx_xml_type_t xx_xml_type(const xx_xml *xml) {
    return xml ? xml->type : XX_XML_NONE;
}

const char *xx_xml_name(const xx_xml *xml) {
    return xml ? xml->name : NULL;
}

const char *xx_xml_text(const xx_xml *xml) {
    return xml ? xml->text : NULL;
}

int xx_xml_depth(const xx_xml *xml) {
    return xml ? xml->depth : 0;
}

size_t xx_xml_attribute_count(const xx_xml *xml) {
    return xml ? xml->attribute_count : 0U;
}

const xx_xml_attribute *xx_xml_attribute_at(const xx_xml *xml, size_t index) {
    if (!xml || index >= xml->attribute_count) return NULL;
    return &xml->attributes[index];
}

const char *xx_xml_attribute_value(const xx_xml *xml, const char *name) {
    size_t index;

    if (!xml || !name) return NULL;
    for (index = 0U; index < xml->attribute_count; ++index) {
        if (xx_str_cmp(xml->attributes[index].name, name) == 0) {
            return xml->attributes[index].value;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------- writing -- */

static const char *xx_xml_entity_for(char c) {
    switch (c) {
        case '<': return "&lt;";
        case '>': return "&gt;";
        case '&': return "&amp;";
        case '"': return "&quot;";
        default: return NULL;
    }
}

size_t xx_xml_escaped_size(const char *text) {
    size_t total = 0U;
    size_t index;

    if (!text) return 0U;
    for (index = 0U; text[index]; ++index) {
        const char *entity = xx_xml_entity_for(text[index]);
        total += entity ? xx_str_len(entity) : 1U;
    }
    return total;
}

size_t xx_xml_escape(const char *text, char *buffer, size_t capacity) {
    size_t length = 0U;
    size_t index;

    if (!text || !buffer || capacity == 0U) return 0U;
    if (xx_xml_escaped_size(text) + 1U > capacity) return 0U;
    for (index = 0U; text[index]; ++index) {
        const char *entity = xx_xml_entity_for(text[index]);
        if (entity) {
            size_t entity_length = xx_str_len(entity);
            xx_rt_memcpy(buffer + length, entity, entity_length);
            length += entity_length;
        } else {
            buffer[length++] = text[index];
        }
    }
    buffer[length] = '\0';
    return length;
}
