/* The Kepler browser's HTML engine: a forgiving tokenizer, a flat item list,
 * and line layout. Scripts and styles are skipped, not run. */

#include "html.h"
#include "http.h"

/* ---- document building ------------------------------------------------------------------- */

static void *grow(void *p, int *cap, int need, int elem)
{
    if (need <= *cap)
        return p;
    int cap2 = MAX(*cap * 2, 64);
    while (cap2 < need)
        cap2 *= 2;
    void *n = kmalloc((size_t)cap2 * elem);
    if (p)
        memcpy(n, p, (size_t)*cap * elem);
    kfree(p);
    *cap = cap2;
    return n;
}

static int pool_add(doc_t *d, const char *s, int n)
{
    d->pool = grow(d->pool, &d->pool_cap, d->pool_len + n + 1, 1);
    memcpy(d->pool + d->pool_len, s, n);
    int at = d->pool_len;
    d->pool_len += n;
    d->pool[d->pool_len] = 0;
    return at;
}

static item_t *add_item(doc_t *d, int kind)
{
    d->items = grow(d->items, &d->cap_items, d->n_items + 1, sizeof(item_t));
    item_t *it = &d->items[d->n_items++];
    memset(it, 0, sizeof *it);
    it->kind = kind;
    it->link = -1;
    it->field = -1;
    return it;
}

const char *html_link(const doc_t *d, int link)
{
    if (link < 0 || link >= d->n_links)
        return "";
    return d->pool + d->links[link];
}

void html_free(doc_t *d)
{
    if (!d)
        return;
    kfree(d->items);
    kfree(d->pool);
    kfree(d->links);
    kfree(d->fields);
    kfree(d->forms);
    kfree(d);
}

/* ---- characters ------------------------------------------------------------------------------- */

/* The fonts are ASCII, so everything else is spelled with it. */
static const char *translit(uint32_t cp, char *tmp)
{
    static const char latin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    static const char ext_a[] = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiJjJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTt"
                                "UuUuUuUuUuUuWwYyYZzZzZzs";
    if (cp < 128) {
        tmp[0] = (char)cp;
        tmp[1] = 0;
        return tmp;
    }
    switch (cp) {
    case 0xA0: case 0x2002: case 0x2003: case 0x2009: case 0x200A: case 0x202F: case 0x205F: case 0x3000: return " ";
    case 0xAD: case 0x200B: case 0x200C: case 0x200D: case 0x2060: case 0xFEFF: return "";
    case 0xA9: return "(c)";
    case 0xAE: return "(R)";
    case 0x2122: return "(TM)";
    case 0xB0: return " deg";
    case 0xB7: case 0x2022: case 0x2023: case 0x25CF: case 0x25AA: return "*";
    case 0xAB: return "<<";
    case 0xBB: return ">>";
    case 0xD7: return "x";
    case 0xF7: return "/";
    case 0xBF: return "?";
    case 0xA1: return "!";
    case 0xA3: return "GBP ";
    case 0xA5: return "JPY ";
    case 0xA2: return "c";
    case 0xA7: return "S";
    case 0xB6: return "P";
    case 0xB1: return "+/-";
    case 0xBC: return "1/4";
    case 0xBD: return "1/2";
    case 0xBE: return "3/4";
    case 0xB2: return "2";
    case 0xB3: return "3";
    case 0xB9: return "1";
    case 0xC6: return "AE";
    case 0xE6: return "ae";
    case 0xDF: return "ss";
    case 0xDE: return "Th";
    case 0xFE: return "th";
    case 0x152: return "OE";
    case 0x153: return "oe";
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2212: case 0x2043: return "-";
    case 0x2014: case 0x2015: return "--";
    case 0x2018: case 0x2019: case 0x201A: case 0x201B: case 0x2032: case 0x2B9: case 0x2BC: return "'";
    case 0x201C: case 0x201D: case 0x201E: case 0x201F: case 0x2033: return "\"";
    case 0x2026: return "...";
    case 0x2039: return "<";
    case 0x203A: return ">";
    case 0x2020: return "+";
    case 0x20AC: return "EUR ";
    case 0x2190: return "<-";
    case 0x2192: return "->";
    case 0x2191: return "^";
    case 0x2193: return "v";
    case 0x2264: return "<=";
    case 0x2265: return ">=";
    case 0x2260: return "!=";
    case 0x2248: return "~";
    case 0x221E: return "inf";
    case 0x2713: case 0x2714: return "v";
    case 0x2715: case 0x2716: case 0x2717: return "x";
    case 0x25B2: case 0x25B4: return "^";
    case 0x25BC: case 0x25BE: return "v";
    case 0x25B6: case 0x25B8: return ">";
    case 0x25C0: case 0x25C2: return "<";
    }
    if (cp >= 0xC0 && cp <= 0xFF) {
        tmp[0] = latin1[cp - 0xC0];
        tmp[1] = 0;
        return tmp;
    }
    if (cp >= 0x100 && cp < 0x100 + sizeof ext_a - 1) {
        tmp[0] = ext_a[cp - 0x100];
        tmp[1] = 0;
        return tmp;
    }
    if (cp >= 0x300 && cp < 0x370)
        return ""; /* combining accents */
    if (cp >= 0x2000 && cp < 0x2E80)
        return ""; /* symbols, arrows, dingbats */
    if (cp >= 0xE000 && cp < 0xF900)
        return ""; /* private use: icon fonts */
    if (cp >= 0x1F000)
        return ""; /* emoji */
    return "?";
}

static uint32_t utf8(const char *s, int len, int *used)
{
    const uint8_t *p = (const uint8_t *)s;
    uint32_t c = p[0];
    int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (n == 1 || n > len) {
        *used = 1;
        return c < 0x80 ? c : 0xFFFD;
    }
    c &= 0x3F >> (n - 1);
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *used = 1;
            return 0xFFFD;
        }
        c = c << 6 | (p[i] & 0x3F);
    }
    *used = n;
    return c;
}

static const struct {
    const char *name;
    uint32_t cp;
} entities[] = {
    { "amp", '&' },      { "lt", '<' },        { "gt", '>' },        { "quot", '"' },      { "apos", '\'' },
    { "nbsp", 0xA0 },    { "copy", 0xA9 },     { "reg", 0xAE },      { "trade", 0x2122 },  { "hellip", 0x2026 },
    { "mdash", 0x2014 }, { "ndash", 0x2013 },  { "lsquo", 0x2018 },  { "rsquo", 0x2019 },  { "ldquo", 0x201C },
    { "rdquo", 0x201D }, { "sbquo", 0x201A },  { "bdquo", 0x201E },  { "laquo", 0xAB },    { "raquo", 0xBB },
    { "bull", 0x2022 },  { "middot", 0xB7 },   { "deg", 0xB0 },      { "times", 0xD7 },    { "divide", 0xF7 },
    { "euro", 0x20AC },  { "pound", 0xA3 },    { "yen", 0xA5 },      { "cent", 0xA2 },     { "sect", 0xA7 },
    { "para", 0xB6 },    { "shy", 0xAD },      { "plusmn", 0xB1 },   { "frac12", 0xBD },   { "frac14", 0xBC },
    { "frac34", 0xBE },  { "iexcl", 0xA1 },    { "iquest", 0xBF },   { "larr", 0x2190 },   { "rarr", 0x2192 },
    { "uarr", 0x2191 },  { "darr", 0x2193 },   { "minus", 0x2212 },  { "le", 0x2264 },     { "ge", 0x2265 },
    { "ne", 0x2260 },    { "zwj", 0x200D },    { "zwnj", 0x200C },   { "thinsp", 0x2009 }, { "ensp", 0x2002 },
    { "emsp", 0x2003 },  { "szlig", 0xDF },    { "aelig", 0xE6 },    { "AElig", 0xC6 },    { "prime", 0x2032 },
    { "lsaquo", 0x2039 }, { "rsaquo", 0x203A }, { "dagger", 0x2020 }, { "infin", 0x221E },  { "check", 0x2713 },
};

/* "&eacute;" and friends: the letter before the accent name */
static bool accented(const char *name, int n, uint32_t *cp)
{
    static const char *marks[] = { "acute", "grave", "circ", "uml", "tilde", "ring", "cedil", "slash", "caron" };
    if (n < 4)
        return false;
    for (int i = 0; i < ARRAY_LEN(marks); i++) {
        int ml = strlen(marks[i]);
        if (n == ml + 1 && !strncmp(name + 1, marks[i], ml)) {
            *cp = (unsigned char)name[0];
            return true;
        }
    }
    return false;
}

/* decodes the entity at s ('&'), returning its code point and length, or 0 */
static int entity(const char *s, int len, uint32_t *cp)
{
    int i = 1;
    if (i < len && s[i] == '#') {
        i++;
        uint32_t v = 0;
        bool hex = i < len && (s[i] == 'x' || s[i] == 'X');
        if (hex)
            i++;
        int start = i;
        while (i < len && i - start < 8) {
            char c = s[i];
            int d = kisdigit(c) ? c - '0' : hex && c >= 'a' && c <= 'f' ? c - 'a' + 10 : hex && c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0)
                break;
            v = v * (hex ? 16 : 10) + d;
            i++;
        }
        if (i == start)
            return 0;
        if (i < len && s[i] == ';')
            i++;
        *cp = v == 0x92 ? 0x2019 : v == 0x93 ? 0x201C : v == 0x94 ? 0x201D : v == 0x96 ? 0x2013 : v == 0x97 ? 0x2014 : v;
        return i;
    }
    int start = i;
    while (i < len && i - start < 10 && (kisalpha(s[i]) || kisdigit(s[i])))
        i++;
    int n = i - start;
    if (!n)
        return 0;
    for (int k = 0; k < ARRAY_LEN(entities); k++)
        if ((int)strlen(entities[k].name) == n && !strncmp(entities[k].name, s + start, n)) {
            *cp = entities[k].cp;
            return i + (i < len && s[i] == ';');
        }
    if (accented(s + start, n, cp))
        return i + (i < len && s[i] == ';');
    return 0;
}

/* ---- the parser --------------------------------------------------------------------------- */

typedef struct {
    doc_t *d;
    int bold, italic, mono, pre, small, quote;
    int heading;
    int link;
    int indent;
    struct {
        bool ordered;
        int counter;
    } lists[16];
    int depth;
    bool space;      /* a collapsed space is waiting */
    bool line_start; /* drop spaces at the start of a line */
    int form;
    int cell;
} parser_t;

static uint8_t style_of(parser_t *p)
{
    return (p->bold ? ST_BOLD : 0) | (p->italic ? ST_ITALIC : 0) | (p->mono ? ST_MONO : 0) | (p->pre ? ST_PRE : 0) |
           (p->small ? ST_SMALL : 0) | (p->quote ? ST_QUOTE : 0);
}

static void put_text(parser_t *p, const char *s, int n)
{
    doc_t *d = p->d;
    uint8_t st = style_of(p);
    item_t *last = d->n_items ? &d->items[d->n_items - 1] : NULL;
    if (last && last->kind == IT_TEXT && last->style == st && last->heading == p->heading && last->link == p->link &&
        last->indent == p->indent && last->text + last->len == d->pool_len) {
        pool_add(d, s, n);
        last->len += n;
        return;
    }
    item_t *it = add_item(d, IT_TEXT);
    it->style = st;
    it->heading = p->heading;
    it->link = p->link;
    it->indent = p->indent;
    it->text = pool_add(d, s, n);
    it->len = n;
}

static void block(parser_t *p, int margin)
{
    doc_t *d = p->d;
    p->space = false;
    p->line_start = true;
    if (!d->n_items)
        return;
    item_t *last = &d->items[d->n_items - 1];
    if (last->kind == IT_BLOCK) {
        last->margin = MAX(last->margin, margin);
        last->indent = p->indent;
        return;
    }
    item_t *it = add_item(d, IT_BLOCK);
    it->margin = margin;
    it->indent = p->indent;
}

static void line_break(parser_t *p)
{
    item_t *it = add_item(p->d, IT_BREAK);
    it->indent = p->indent;
    p->space = false;
    p->line_start = true;
}

/* one visible character (already ASCII) */
static void put_char(parser_t *p, char c)
{
    if (p->pre) {
        if (c == '\n') {
            line_break(p);
            return;
        }
        if (c == '\t') {
            put_text(p, "    ", 4);
            return;
        }
        if (c == '\r')
            return;
        put_text(p, &c, 1);
        p->line_start = false;
        return;
    }
    if (c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f') {
        p->space = true;
        return;
    }
    if (p->space && !p->line_start)
        put_text(p, " ", 1);
    p->space = false;
    p->line_start = false;
    put_text(p, &c, 1);
}

static void put_codepoint(parser_t *p, uint32_t cp)
{
    char tmp[4];
    for (const char *s = translit(cp, tmp); *s; s++)
        put_char(p, *s);
}

/* text between tags: entities and UTF-8 */
static void text_run(parser_t *p, const char *s, int len)
{
    for (int i = 0; i < len;) {
        uint32_t cp;
        int used;
        if (s[i] == '&' && (used = entity(s + i, len - i, &cp)) > 0) {
            put_codepoint(p, cp);
            i += used;
        } else if ((unsigned char)s[i] >= 0x80) {
            cp = utf8(s + i, len - i, &used);
            put_codepoint(p, cp);
            i += used;
        } else {
            put_char(p, s[i]);
            i++;
        }
    }
}

/* plain decoded text of a span (titles, button labels, attribute values) */
static void decode_plain(const char *s, int len, char *out, int n)
{
    int o = 0;
    bool space = false;
    for (int i = 0; i < len && o < n - 4;) {
        uint32_t cp;
        int used = 0;
        if (s[i] == '<') {
            while (i < len && s[i] != '>')
                i++;
            i++;
            space = true;
            continue;
        }
        if (s[i] == '&' && (used = entity(s + i, len - i, &cp)) > 0)
            i += used;
        else if ((unsigned char)s[i] >= 0x80) {
            cp = utf8(s + i, len - i, &used);
            i += used;
        } else
            cp = (unsigned char)s[i++];
        char tmp[4];
        for (const char *t = translit(cp, tmp); *t && o < n - 2; t++) {
            if (*t == ' ' || *t == '\n' || *t == '\t' || *t == '\r') {
                space = true;
                continue;
            }
            if (space && o)
                out[o++] = ' ';
            space = false;
            out[o++] = *t;
        }
    }
    out[o] = 0;
}

typedef struct {
    char name[16];
    bool close, self_close;
    char href[1024], src[16], alt[200], type[24], name_attr[48], value[256], action[512], http_equiv[24],
        content[1100], width[8], size[8];
    bool has_href;
} tag_t;

static void attr_set(tag_t *t, const char *name, const char *v, int vlen)
{
    struct {
        const char *n;
        char *dst;
        int cap;
    } slots[] = {
        { "href", t->href, sizeof t->href },
        { "alt", t->alt, sizeof t->alt },
        { "type", t->type, sizeof t->type },
        { "name", t->name_attr, sizeof t->name_attr },
        { "value", t->value, sizeof t->value },
        { "action", t->action, sizeof t->action },
        { "http-equiv", t->http_equiv, sizeof t->http_equiv },
        { "content", t->content, sizeof t->content },
        { "width", t->width, sizeof t->width },
        { "size", t->size, sizeof t->size },
        { "src", t->src, sizeof t->src },
    };
    for (int i = 0; i < ARRAY_LEN(slots); i++)
        if (!strcmp(name, slots[i].n)) {
            decode_plain(v, vlen, slots[i].dst, slots[i].cap);
            if (slots[i].dst == t->href)
                t->has_href = true;
            return;
        }
}

/* parses the tag starting at s ('<'); returns its length */
static int read_tag(const char *s, int len, tag_t *t)
{
    memset(t, 0, sizeof *t);
    int i = 1;
    if (i < len && s[i] == '/') {
        t->close = true;
        i++;
    }
    int n = 0;
    while (i < len && (kisalpha(s[i]) || kisdigit(s[i]) || s[i] == '-' || s[i] == ':')) {
        if (n < (int)sizeof t->name - 1)
            t->name[n++] = ktolower(s[i]);
        i++;
    }
    t->name[n] = 0;
    while (i < len && s[i] != '>') {
        while (i < len && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r'))
            i++;
        if (i >= len || s[i] == '>')
            break;
        if (s[i] == '/') {
            t->self_close = true;
            i++;
            continue;
        }
        char an[16];
        int al = 0;
        while (i < len && s[i] != '=' && s[i] != '>' && s[i] != ' ' && s[i] != '/' && s[i] != '\n' && s[i] != '\t') {
            if (al < (int)sizeof an - 1)
                an[al++] = ktolower(s[i]);
            i++;
        }
        an[al] = 0;
        while (i < len && (s[i] == ' ' || s[i] == '\n'))
            i++;
        if (i < len && s[i] == '=') {
            i++;
            while (i < len && (s[i] == ' ' || s[i] == '\n'))
                i++;
            const char *v;
            int vl = 0;
            if (i < len && (s[i] == '"' || s[i] == '\'')) {
                char q = s[i++];
                v = s + i;
                while (i < len && s[i] != q)
                    i++, vl++;
                i++;
            } else {
                v = s + i;
                while (i < len && s[i] != ' ' && s[i] != '>' && s[i] != '\n' && s[i] != '\t')
                    i++, vl++;
            }
            if (al)
                attr_set(t, an, v, vl);
        } else if (!al) {
            i++;
        }
    }
    return MIN(i + 1, len);
}

static int add_link(doc_t *d, const char *href)
{
    char abs[1100];
    url_resolve(d->url, href, abs, sizeof abs);
    d->links = grow(d->links, &d->cap_links, d->n_links + 1, sizeof(int));
    d->links[d->n_links] = pool_add(d, abs, strlen(abs) + 1);
    return d->n_links++;
}

static int add_field(parser_t *p, int type, const char *name, const char *value, int width)
{
    doc_t *d = p->d;
    d->fields = grow(d->fields, &d->cap_fields, d->n_fields + 1, sizeof(field_t));
    field_t *f = &d->fields[d->n_fields];
    memset(f, 0, sizeof *f);
    f->form = p->form;
    f->type = type;
    strlcpy(f->name, name, sizeof f->name);
    strlcpy(f->value, value, sizeof f->value);
    f->width = width;
    return d->n_fields++;
}

static bool is(const tag_t *t, const char *name) { return !strcmp(t->name, name); }

static int heading_level(const tag_t *t)
{
    return t->name[0] == 'h' && t->name[1] >= '1' && t->name[1] <= '6' && !t->name[2] ? t->name[1] - '0' : 0;
}

static const char *const block_tags[] = { "div", "section", "article", "header", "footer", "nav", "main", "aside", "figure",
                                          "figcaption", "address", "fieldset", "details", "summary", "center", "dl",
                                          "caption", "legend", "noscript", "body", "hgroup", "menu" };

static bool is_block(const tag_t *t)
{
    for (int i = 0; i < ARRAY_LEN(block_tags); i++)
        if (is(t, block_tags[i]))
            return true;
    return false;
}

/* tags whose content is never shown */
static bool is_raw_skip(const tag_t *t)
{
    static const char *const skip[] = { "script", "style", "svg", "template", "iframe", "object", "canvas",
                                        "noembed", "select", "textarea", "video", "audio", "math" };
    for (int i = 0; i < ARRAY_LEN(skip); i++)
        if (is(t, skip[i]))
            return true;
    return false;
}

static void handle_tag(parser_t *p, tag_t *t, const char *src, int len, int *pos)
{
    doc_t *d = p->d;
    int h = heading_level(t);
    if (t->close) {
        if (h) {
            p->heading = 0;
            block(p, 6);
        } else if (is(t, "p")) {
            block(p, 10);
        } else if (is(t, "a")) {
            p->link = -1;
        } else if (is(t, "b") || is(t, "strong") || is(t, "th")) {
            p->bold = MAX(0, p->bold - 1);
            if (is(t, "th"))
                p->cell++;
        } else if (is(t, "i") || is(t, "em") || is(t, "cite") || is(t, "var") || is(t, "dfn")) {
            p->italic = MAX(0, p->italic - 1);
        } else if (is(t, "code") || is(t, "kbd") || is(t, "samp") || is(t, "tt")) {
            p->mono = MAX(0, p->mono - 1);
        } else if (is(t, "pre")) {
            p->pre = MAX(0, p->pre - 1);
            p->mono = MAX(0, p->mono - 1);
            block(p, 10);
        } else if (is(t, "small") || is(t, "sub") || is(t, "sup")) {
            p->small = MAX(0, p->small - 1);
        } else if (is(t, "q")) {
            put_char(p, '"');
        } else if (is(t, "ul") || is(t, "ol")) {
            if (p->depth > 0)
                p->depth--;
            p->indent = MAX(0, p->indent - 1);
            block(p, p->depth ? 2 : 8);
        } else if (is(t, "blockquote") || is(t, "dd")) {
            p->indent = MAX(0, p->indent - 1);
            p->quote = MAX(0, p->quote - (is(t, "blockquote") ? 1 : 0));
            block(p, 8);
        } else if (is(t, "li") || is(t, "dt") || is(t, "tr")) {
            block(p, 2);
        } else if (is(t, "table")) {
            block(p, 8);
        } else if (is(t, "form")) {
            p->form = -1;
            block(p, 4);
        } else if (is(t, "td")) {
            p->cell++;
        } else if (is_block(t)) {
            block(p, 0);
        }
        return;
    }

    if (is_raw_skip(t) && !t->self_close) {
        /* jump past the matching end tag */
        char end[24];
        ksnprintf(end, sizeof end, "</%s", t->name);
        const char *e = strcasestr(src + *pos, end);
        *pos = e ? (int)(e - src) : len;
        if (e) {
            const char *gt = strchr(e, '>');
            *pos = gt ? (int)(gt - src) + 1 : len;
        }
        return;
    }
    if (is(t, "title")) {
        const char *e = strcasestr(src + *pos, "</title");
        int n = e ? (int)(e - (src + *pos)) : 0;
        decode_plain(src + *pos, n, d->title, sizeof d->title);
        if (e)
            *pos = (int)(e - src);
        return;
    }
    if (h) {
        block(p, h <= 2 ? 16 : 12);
        p->heading = h;
    } else if (is(t, "p")) {
        block(p, 10);
    } else if (is(t, "br")) {
        line_break(p);
    } else if (is(t, "hr")) {
        block(p, 4);
        add_item(d, IT_HR)->indent = p->indent;
        block(p, 4);
    } else if (is(t, "a")) {
        p->link = t->has_href && t->href[0] && strncmp(t->href, "javascript:", 11) ? add_link(d, t->href) : -1;
    } else if (is(t, "b") || is(t, "strong")) {
        p->bold++;
    } else if (is(t, "i") || is(t, "em") || is(t, "cite") || is(t, "var") || is(t, "dfn")) {
        p->italic++;
    } else if (is(t, "code") || is(t, "kbd") || is(t, "samp") || is(t, "tt")) {
        p->mono++;
    } else if (is(t, "pre")) {
        block(p, 10);
        p->pre++;
        p->mono++;
        /* a newline right after <pre> doesn't count */
        if (*pos < len && src[*pos] == '\n')
            (*pos)++;
    } else if (is(t, "small") || is(t, "sub") || is(t, "sup")) {
        p->small++;
    } else if (is(t, "q")) {
        put_char(p, '"');
    } else if (is(t, "ul") || is(t, "ol")) {
        block(p, p->depth ? 2 : 8);
        if (p->depth < 16) {
            p->lists[p->depth].ordered = is(t, "ol");
            p->lists[p->depth].counter = 0;
        }
        p->depth++;
        p->indent++;
    } else if (is(t, "li")) {
        block(p, 3);
        item_t *it = add_item(d, IT_BULLET);
        it->indent = p->indent;
        char mark[12] = "*";
        if (p->depth > 0 && p->depth <= 16 && p->lists[p->depth - 1].ordered)
            ksnprintf(mark, sizeof mark, "%d.", ++p->lists[p->depth - 1].counter);
        it->text = pool_add(d, mark, strlen(mark));
        it->len = strlen(mark);
        p->line_start = true;
    } else if (is(t, "blockquote") || is(t, "dd")) {
        block(p, is(t, "dd") ? 2 : 10);
        p->indent++;
        if (is(t, "blockquote"))
            p->quote++;
    } else if (is(t, "dt")) {
        block(p, 6);
    } else if (is(t, "table")) {
        block(p, 8);
    } else if (is(t, "tr")) {
        block(p, 2);
        p->cell = 0;
    } else if (is(t, "td") || is(t, "th")) {
        if (p->cell > 0 && !p->line_start) {
            put_text(p, "   ", 3);
            p->space = false;
        }
        if (is(t, "th"))
            p->bold++;
    } else if (is(t, "img")) {
        if (t->alt[0]) {
            item_t *it = add_item(d, IT_IMAGE);
            it->indent = p->indent;
            it->link = p->link;
            it->text = pool_add(d, t->alt, strlen(t->alt));
            it->len = strlen(t->alt);
            p->line_start = false;
        }
    } else if (is(t, "form")) {
        block(p, 4);
        d->forms = grow(d->forms, &d->cap_forms, d->n_forms + 1, sizeof(form_t));
        form_t *f = &d->forms[d->n_forms];
        url_resolve(d->url, t->action[0] ? t->action : d->url, f->action, sizeof f->action);
        p->form = d->n_forms++;
    } else if (is(t, "input")) {
        const char *ty = t->type;
        if (!strcasecmp(ty, "hidden")) {
            add_field(p, F_HIDDEN, t->name_attr, t->value, 0);
        } else if (!strcasecmp(ty, "submit") || !strcasecmp(ty, "image")) {
            item_t *it = add_item(d, IT_BUTTON);
            it->indent = p->indent;
            it->field = add_field(p, F_SUBMIT, t->name_attr, t->value[0] ? t->value : "Submit", 0);
            p->line_start = false;
        } else if (!ty[0] || !strcasecmp(ty, "text") || !strcasecmp(ty, "search") || !strcasecmp(ty, "email") ||
                   !strcasecmp(ty, "url") || !strcasecmp(ty, "tel") || !strcasecmp(ty, "number") ||
                   !strcasecmp(ty, "password")) {
            if (p->space && !p->line_start)
                put_text(p, " ", 1);
            item_t *it = add_item(d, IT_INPUT);
            it->indent = p->indent;
            it->field = add_field(p, strcasecmp(ty, "password") ? F_TEXT : F_PASSWORD, t->name_attr, t->value,
                                  t->size[0] ? katoi(t->size) : 0);
            p->line_start = false;
            p->space = true;
        }
    } else if (is(t, "button")) {
        const char *e = strcasestr(src + *pos, "</button");
        char label[64];
        decode_plain(src + *pos, e ? (int)(e - (src + *pos)) : 0, label, sizeof label);
        if (e)
            *pos = (int)(e - src);
        if (strcasecmp(t->type, "button") && p->form >= 0) {
            item_t *it = add_item(d, IT_BUTTON);
            it->indent = p->indent;
            it->field = add_field(p, F_SUBMIT, t->name_attr, label[0] ? label : "Submit", 0);
            p->line_start = false;
        }
    } else if (is(t, "base")) {
        if (t->has_href) {
            char abs[1100];
            url_resolve(d->url, t->href, abs, sizeof abs);
            strlcpy(d->url, abs, sizeof d->url);
        }
    } else if (is(t, "meta")) {
        if (!strcasecmp(t->http_equiv, "refresh")) {
            const char *u = strcasestr(t->content, "url=");
            if (u) {
                u += 4;
                while (*u == '\'' || *u == '"' || *u == ' ')
                    u++;
                char tmp[1100];
                strlcpy(tmp, u, sizeof tmp);
                int n = strlen(tmp);
                while (n && (tmp[n - 1] == '\'' || tmp[n - 1] == '"'))
                    tmp[--n] = 0;
                url_resolve(d->url, tmp, d->refresh, sizeof d->refresh);
            }
        }
    } else if (is_block(t)) {
        block(p, 0);
    }
}

doc_t *html_parse(const char *src, int len, const char *url)
{
    doc_t *d = kzalloc(sizeof *d);
    strlcpy(d->url, url, sizeof d->url);
    parser_t p = { 0 };
    p.d = d;
    p.link = -1;
    p.form = -1;
    p.line_start = true;
    int i = 0;
    while (i < len) {
        if (src[i] == '<') {
            if (i + 3 < len && !strncmp(src + i, "<!--", 4)) {
                const char *e = strstr(src + i + 4, "-->");
                i = e ? (int)(e - src) + 3 : len;
                continue;
            }
            if (i + 1 < len && (src[i + 1] == '!' || src[i + 1] == '?')) {
                while (i < len && src[i] != '>')
                    i++;
                i++;
                continue;
            }
            if (i + 1 < len && (kisalpha(src[i + 1]) || src[i + 1] == '/')) {
                static tag_t t;
                i += read_tag(src + i, len - i, &t);
                handle_tag(&p, &t, src, len, &i);
                continue;
            }
        }
        int start = i;
        i++;
        while (i < len && src[i] != '<')
            i++;
        text_run(&p, src + start, i - start);
    }
    if (!d->title[0]) {
        const char *slash = strrchr(url, '/');
        strlcpy(d->title, slash && slash[1] ? slash + 1 : url, sizeof d->title);
    }
    return d;
}

doc_t *html_plain(const char *src, int len, const char *url)
{
    doc_t *d = kzalloc(sizeof *d);
    strlcpy(d->url, url, sizeof d->url);
    const char *slash = strrchr(url, '/');
    strlcpy(d->title, slash && slash[1] ? slash + 1 : url, sizeof d->title);
    parser_t p = { 0 };
    p.d = d;
    p.link = -1;
    p.form = -1;
    p.pre = 1;
    p.mono = 1;
    for (int i = 0; i < len;) {
        int used;
        uint32_t cp = utf8(src + i, len - i, &used);
        put_codepoint(&p, cp);
        i += used;
    }
    return d;
}

void html_form_url(const doc_t *d, int form, char *out, int n)
{
    strlcpy(out, d->forms[form].action, n);
    char *hash = strchr(out, '#');
    if (hash)
        *hash = 0;
    char *q = strchr(out, '?');
    if (q)
        *q = 0; /* a GET form replaces the query */
    bool first = true;
    for (int i = 0; i < d->n_fields; i++) {
        const field_t *f = &d->fields[i];
        if (f->form != form || !f->name[0] || f->type == F_SUBMIT)
            continue;
        char name[160], value[800];
        url_encode(f->name, name, sizeof name);
        url_encode(f->value, value, sizeof value);
        strlcat(out, first ? "?" : "&", n);
        strlcat(out, name, n);
        strlcat(out, "=", n);
        strlcat(out, value, n);
        first = false;
    }
}

/* ---- layout ------------------------------------------------------------------------------------- */

#define MARGIN   28
#define INDENT   26
#define LINE_GAP 5

static const font_t *font_for(const item_t *it)
{
    switch (it->heading) {
    case 1: return &font_big_bold;
    case 2: return &font_date;
    case 3: return &font_heading;
    case 4:
    case 5:
    case 6: return &font_menu_bold;
    }
    if (it->style & ST_MONO)
        return &font_mono;
    if (it->style & ST_SMALL)
        return (it->style & ST_BOLD) ? &font_ui_bold : &font_ui;
    return (it->style & ST_BOLD) ? &font_menu_bold : &font_menu;
}

static uint32_t color_for(const item_t *it)
{
    if (it->link >= 0)
        return COL_LINK;
    if (it->kind == IT_IMAGE)
        return HEX(0x8A8A8E);
    if (it->style & ST_QUOTE)
        return HEX(0x4A4A4E);
    if (it->style & ST_ITALIC)
        return HEX(0x3A3A3E);
    return COL_TEXT;
}

typedef struct {
    layout_t *l;
    int x, y;
    int left, right;
    int line_start; /* first fragment of the current line */
    int pending;    /* collapsed vertical margin */
} lay_t;

static frag_t *add_frag(lay_t *s)
{
    layout_t *l = s->l;
    l->frags = grow(l->frags, &l->cap, l->n + 1, sizeof(frag_t));
    frag_t *f = &l->frags[l->n++];
    memset(f, 0, sizeof *f);
    f->link = -1;
    f->item = -1;
    return f;
}

/* closes the current line: align its fragments on a common baseline */
static void end_line(lay_t *s, bool force)
{
    layout_t *l = s->l;
    if (s->line_start == l->n) {
        if (force)
            s->y += font_menu.height + LINE_GAP;
        s->x = s->left;
        return;
    }
    if (s->pending) {
        s->y += s->pending;
        s->pending = 0;
    }
    int asc = 0, desc = 0;
    for (int i = s->line_start; i < l->n; i++) {
        frag_t *f = &l->frags[i];
        int a = f->font ? f->font->ascent : f->h - 4, dsc = f->font ? f->font->height - f->font->ascent : 4;
        asc = MAX(asc, a);
        desc = MAX(desc, dsc);
    }
    for (int i = s->line_start; i < l->n; i++) {
        frag_t *f = &l->frags[i];
        int a = f->font ? f->font->ascent : f->h - 4;
        f->y = s->y + asc - a;
    }
    s->y += asc + desc + LINE_GAP;
    s->line_start = l->n;
    s->x = s->left;
}

static void set_indent(lay_t *s, int indent, int width)
{
    s->left = MARGIN + indent * INDENT;
    s->right = MAX(s->left + 120, width - MARGIN);
    if (s->line_start == s->l->n)
        s->x = s->left;
}

static void place_text(lay_t *s, const doc_t *d, int idx, const item_t *it)
{
    const font_t *f = font_for(it);
    uint32_t color = color_for(it);
    int sp = char_width(f, ' ');
    const char *t = d->pool + it->text;
    int n = it->len;
    bool pre = it->style & ST_PRE;
    int i = 0;
    while (i < n) {
        if (t[i] == ' ' && !pre) {
            if (s->x > s->left)
                s->x += sp;
            i++;
            continue;
        }
        int j = i;
        if (pre)
            j = n;
        else
            while (j < n && t[j] != ' ')
                j++;
        int w = text_width_n(f, t + i, j - i);
        if (!pre && s->x + w > s->right && s->x > s->left)
            end_line(s, false);
        /* words wider than the whole line are cut */
        while (!pre && w > s->right - s->left) {
            int k = i, ww = 0;
            while (k < j && ww + char_width(f, t[k]) <= s->right - s->x)
                ww += char_width(f, t[k++]);
            if (k == i)
                k++;
            frag_t *fr = add_frag(s);
            *fr = (frag_t){ s->x, 0, text_width_n(f, t + i, k - i), f->height, idx, it->text + i, k - i, f, color, it->link, IT_TEXT };
            end_line(s, false);
            i = k;
            w = text_width_n(f, t + i, j - i);
        }
        if (i >= j)
            continue;
        /* join with the previous word of the same item on this line */
        layout_t *l = s->l;
        frag_t *prev = l->n > s->line_start ? &l->frags[l->n - 1] : NULL;
        if (prev && i > 0 && prev->item == idx && prev->start + prev->len + 1 == it->text + i && t[i - 1] == ' ' &&
            prev->x + prev->w + sp == s->x) {
            prev->len += 1 + (j - i);
            prev->w = s->x + w - prev->x;
        } else {
            frag_t *fr = add_frag(s);
            *fr = (frag_t){ s->x, 0, w, f->height, idx, it->text + i, j - i, f, color, it->link, IT_TEXT };
        }
        s->x += w;
        i = j;
    }
}

void html_layout(const doc_t *d, layout_t *l, int width)
{
    l->n = 0;
    l->width = width;
    lay_t s = { l, MARGIN, 18, MARGIN, width - MARGIN, 0, 0 };
    set_indent(&s, 0, width);
    for (int idx = 0; idx < d->n_items; idx++) {
        const item_t *it = &d->items[idx];
        switch (it->kind) {
        case IT_BLOCK:
            end_line(&s, false);
            s.pending = MAX(s.pending, it->margin);
            set_indent(&s, it->indent, width);
            break;
        case IT_BREAK:
            end_line(&s, true);
            set_indent(&s, it->indent, width);
            break;
        case IT_HR: {
            end_line(&s, false);
            s.y += s.pending + 6;
            s.pending = 0;
            frag_t *f = add_frag(&s);
            *f = (frag_t){ s.left, s.y, s.right - s.left, 1, idx, 0, 0, NULL, 0, -1, IT_HR };
            s.line_start = l->n;
            s.y += 8;
            break;
        }
        case IT_BULLET: {
            set_indent(&s, it->indent, width);
            const font_t *f = &font_menu;
            int w = text_width_n(f, d->pool + it->text, it->len);
            frag_t *fr = add_frag(&s);
            bool dot = it->len == 1 && d->pool[it->text] == '*';
            *fr = (frag_t){ s.left - (dot ? 14 : w + 6), 0, dot ? 6 : w, f->height, idx, it->text, it->len, f, COL_TEXT, -1, IT_BULLET };
            break;
        }
        case IT_INPUT:
        case IT_BUTTON: {
            set_indent(&s, it->indent, width);
            const field_t *fd = &d->fields[it->field];
            int w = it->kind == IT_BUTTON ? text_width(&font_ui, fd->value) + 26
                                          : CLAMP(fd->width ? fd->width * 8 : 260, 80, MIN(420, s.right - s.left));
            if (s.x + w > s.right && s.x > s.left)
                end_line(&s, false);
            frag_t *fr = add_frag(&s);
            *fr = (frag_t){ s.x, 0, w, 24, idx, 0, 0, NULL, 0, -1, it->kind };
            s.x += w + 6;
            break;
        }
        case IT_IMAGE:
        case IT_TEXT:
            set_indent(&s, it->indent, width);
            place_text(&s, d, idx, it);
            break;
        }
    }
    end_line(&s, false);
    l->height = s.y + s.pending + 30;
}

void html_layout_free(layout_t *l)
{
    kfree(l->frags);
    l->frags = NULL;
    l->n = l->cap = 0;
}

void html_paint(const doc_t *d, const layout_t *l, gfx_t *g, int scroll, int view_h, int hover_link, int focus_field,
                bool caret_on)
{
    for (int i = 0; i < l->n; i++) {
        const frag_t *f = &l->frags[i];
        int y = f->y - scroll;
        if (y + f->h < 0 || y > view_h)
            continue;
        switch (f->kind) {
        case IT_TEXT:
            gfx_text_n(g, f->font, f->x, y, d->pool + f->start, f->len, f->color);
            if (f->link >= 0 && f->link == hover_link)
                gfx_hline(g, f->x, y + f->font->ascent + 2, f->w, f->color);
            break;
        case IT_BULLET:
            if (f->len == 1 && d->pool[f->start] == '*')
                gfx_circle(g, f->x + 3, y + f->font->ascent - 4, 3, HEX(0x3A3A3C));
            else
                gfx_text_n(g, f->font, f->x, y, d->pool + f->start, f->len, COL_TEXT);
            break;
        case IT_HR: gfx_hline(g, f->x, y, f->w, HEX(0xD9D9DC)); break;
        case IT_INPUT: {
            const item_t *it = &d->items[f->item];
            const field_t *fd = &d->fields[it->field];
            bool focus = it->field == focus_field;
            if (focus)
                gfx_round_frame(g, f->x - 2, y - 2, f->w + 4, f->h + 4, 6, RGBA(10, 100, 222, 140), CORNER_ALL);
            gfx_round_rect(g, f->x, y, f->w, f->h, 5, HEX(0xFFFFFF), CORNER_ALL);
            gfx_round_frame(g, f->x, y, f->w, f->h, 5, focus ? COL_ACCENT : HEX(0xC4C4C8), CORNER_ALL);
            gfx_t inner = gfx_sub(g, f->x + 6, y, f->w - 12, f->h);
            int tx = 0;
            if (fd->type == F_PASSWORD) {
                for (int k = 0; fd->value[k]; k++, tx += 8)
                    gfx_circle(&inner, tx + 3, f->h / 2, 3, COL_TEXT);
            } else {
                int tw = text_width(&font_ui, fd->value);
                int off = MIN(0, (f->w - 16) - tw);
                tx = gfx_text(&inner, &font_ui, off, (f->h - font_ui.height) / 2, fd->value, COL_TEXT);
            }
            if (focus && caret_on)
                gfx_fill(&inner, MIN(tx, f->w - 14), 5, 1, f->h - 10, COL_TEXT);
            break;
        }
        case IT_BUTTON: {
            const item_t *it = &d->items[f->item];
            draw_button(g, f->x, y, f->w, f->h, d->fields[it->field].value, 0);
            break;
        }
        }
    }
}
