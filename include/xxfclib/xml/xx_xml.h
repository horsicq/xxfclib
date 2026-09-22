/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_xml.h
 * @brief A cursor over an XML document, and escaping for writing one.
 *
 * The companion to xx_json.h, and the same shape for the same reason: format
 * readers walk manifests and configuration files to pull out a handful of
 * attributes, so a cursor over the text costs far less than a document tree.
 *
 * This reads the subset real manifests use -- elements, attributes, text,
 * CDATA, comments, processing instructions and the five predefined entities
 * plus numeric character references. It deliberately does NOT do DTDs, entity
 * declarations, or namespace resolution: a prefix stays part of the element
 * name, because a reader looking for `android:name` wants exactly that string
 * and resolving it would only get in the way.
 *
 * A walk looks like:
 *
 * @code
 * xx_xml xml;
 * xx_xml_init(&xml, data, size);
 * while (xx_xml_next(&xml)) {
 *     if (xx_xml_type(&xml) == XX_XML_START &&
 *         xx_str_cmp(xx_xml_name(&xml), "manifest") == 0) {
 *         const char *pkg = xx_xml_attribute_value(&xml, "package");
 *         ...
 *     }
 * }
 * @endcode
 *
 * The name, attributes and text of the current node stay valid until the next
 * call to xx_xml_next(), and are owned by the cursor.
 */

#ifndef XX_XML_H
#define XX_XML_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief How deeply elements may nest.
 *
 * Manifests are shallow; anything approaching this is an attack rather than a
 * document, and the bound belongs here rather than in each reader.
 */
#define XX_XML_MAX_DEPTH 256

/** @brief The most attributes one element may carry. */
#define XX_XML_MAX_ATTRIBUTES 4096

/** @brief What the cursor is sitting on. */
typedef enum xx_xml_type_e {
    XX_XML_NONE = 0,  /**< Before the first node, or past the last. */
    XX_XML_START,     /**< <element ...>, possibly self-closing. */
    XX_XML_END,       /**< </element> */
    XX_XML_TEXT,      /**< Character data between elements, entities resolved. */
    XX_XML_COMMENT,   /**< <!-- ... --> */
    XX_XML_PI,        /**< <?target ... ?> */
    XX_XML_DOCTYPE    /**< <!DOCTYPE ...> */
} xx_xml_type_t;

/** @brief One attribute of the current element. */
typedef struct xx_xml_attribute_s {
    char *name;
    char *value;
} xx_xml_attribute;

/** @brief A position in an XML document. */
typedef struct xx_xml_s {
    const uint8_t *data;
    size_t size;
    size_t position;

    xx_xml_type_t type;
    char *name;  /**< Element or PI target; NULL for text and comments. */
    char *text;  /**< Text, comment or PI body; NULL for elements. */

    /* Attributes of the current start element. Grown on demand rather than
     * held as a fixed array: a fixed one large enough for real documents puts
     * several kilobytes in every caller's stack frame. */
    xx_xml_attribute *attributes;
    size_t attribute_count;
    size_t attribute_capacity;

    /* Names of the elements still open, so an end tag can be checked against
     * the start tag it claims to close. Per cursor, so two documents can be
     * walked at once and nothing is shared between threads. */
    char **open_names;
    int depth;
    int open_capacity;

    bool self_closing; /**< The start element closed itself, as in <br/>. */
    bool failed;       /**< The document is malformed; the walk has stopped. */
} xx_xml;

typedef xx_xml xx_xml_t;

/** @brief Point a cursor at @p size bytes of XML text. */
XXFC_API void xx_xml_init(xx_xml *xml, const void *data, size_t size);

/** @brief Release anything the cursor still owns. */
XXFC_API void xx_xml_cleanup(xx_xml *xml);

/**
 * @brief Advance to the next node.
 *
 * @return false at the end of the document, or on malformed input -- which
 *         xx_xml_failed() distinguishes.
 */
XXFC_API bool xx_xml_next(xx_xml *xml);

/** @brief True when the walk stopped because the document is malformed. */
XXFC_API bool xx_xml_failed(const xx_xml *xml);

/** @brief The kind of the current node. */
XXFC_API xx_xml_type_t xx_xml_type(const xx_xml *xml);

/** @brief The current element's name, or the PI target. NULL otherwise. */
XXFC_API const char *xx_xml_name(const xx_xml *xml);

/** @brief The current text, comment or PI body. NULL otherwise. */
XXFC_API const char *xx_xml_text(const xx_xml *xml);

/** @brief How deeply nested the current node is; the root element is 1. */
XXFC_API int xx_xml_depth(const xx_xml *xml);

/** @brief How many attributes the current start element carries. */
XXFC_API size_t xx_xml_attribute_count(const xx_xml *xml);

/** @brief One attribute by position, or NULL. */
XXFC_API const xx_xml_attribute *xx_xml_attribute_at(const xx_xml *xml,
                                                     size_t index);

/**
 * @brief One attribute value by name, or NULL when absent.
 *
 * The name is matched literally, prefix included: a document using
 * `android:name` is asked for `android:name`, because this parser does not
 * resolve namespaces.
 */
XXFC_API const char *xx_xml_attribute_value(const xx_xml *xml, const char *name);

/* ------------------------------------------------------------- writing -- */

/**
 * @brief Length of @p text once XML-escaped, excluding the terminator.
 *
 * Lets a caller size a buffer before escaping into it.
 */
XXFC_API size_t xx_xml_escaped_size(const char *text);

/**
 * @brief Escape @p text into @p buffer.
 *
 * Replaces `<`, `>`, `&` and `"` with their entities and copies everything
 * else unchanged -- the set every XML writer needs, and no more: escaping the
 * apostrophe as well is legal but changes output that callers may be
 * comparing byte for byte.
 *
 * @return The number of bytes written, excluding the terminator, or 0 when
 *         @p capacity is too small.
 */
XXFC_API size_t xx_xml_escape(const char *text, char *buffer,
                              size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* XX_XML_H */
