#ifndef SOLOIST_TEXT_JSON_H
#define SOLOIST_TEXT_JSON_H

#include <stdint.h>

/* Text and command classification helpers */

static void to_lower_ru(char *dst, const char *src, unsigned long max) {
    unsigned long si = 0, di = 0;
    while (src[si] && di + 4 < max) {
        uint8_t c1 = (uint8_t)src[si];
        if (c1 < 0x80) {
            if (c1 >= 'A' && c1 <= 'Z') dst[di++] = c1 + 32;
            else dst[di++] = c1;
            si++;
        } else if (c1 == 0xd0) {
            uint8_t c2 = (uint8_t)src[si + 1];
            if (c2 == 0x81) { // Ё -> ё
                dst[di++] = 0xd1;
                dst[di++] = 0x91;
            } else if (c2 >= 0x90 && c2 <= 0x9f) { // А-П -> а-п
                dst[di++] = 0xd0;
                dst[di++] = c2 + 0x20;
            } else if (c2 >= 0xa0 && c2 <= 0xaf) { // Р-Я -> р-я
                dst[di++] = 0xd1;
                dst[di++] = c2 - 0x20;
            } else {
                dst[di++] = c1;
                dst[di++] = c2;
            }
            si += 2;
        } else if (c1 == 0xd1) {
            dst[di++] = c1;
            dst[di++] = src[si + 1];
            si += 2;
        } else {
            dst[di++] = c1;
            si++;
        }
    }
    dst[di] = 0;
}

static int str_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    unsigned long hlen = str_len(haystack);
    unsigned long nlen = str_len(needle);
    if (nlen == 0) return 1;
    if (hlen < nlen) return 0;
    for (unsigned long i = 0; i <= hlen - nlen; i++) {
        unsigned long j = 0;
        while (j < nlen && haystack[i + j] == needle[j]) j++;
        if (j == nlen) return 1;
    }
    return 0;
}

static int str_equal(const char *a, const char *b) {
    unsigned long i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static int mem_equal(const void *a, const void *b, unsigned long n) {
    const uint8_t *p1 = (const uint8_t *)a;
    const uint8_t *p2 = (const uint8_t *)b;
    for (unsigned long i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return 0;
    }
    return 1;
}

static void str_copy(char *dst, const char *src, unsigned long max_dst) {
    if (!dst || max_dst == 0) return;
    unsigned long i = 0;
    while (src && src[i] && i + 1 < max_dst) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int json_find_str_bounded(const char *json, const char *end, const char *key,
                                 char *out, unsigned long max_out) {
    if (!json || !key || !out || max_out == 0) return 0;
    unsigned long klen = str_len(key);
    for (const char *p = json; *p && (!end || p < end); p++) {
        if (*p == '"' && (!end || p + 1 + klen < end) &&
            mem_equal(p + 1, key, klen) && p[1 + klen] == '"') {
            const char *v = p + 1 + klen + 1;
            while (*v && (!end || v < end) && (*v == ' ' || *v == ':')) v++;
            if (*v == '"') {
                v++;
                unsigned long o = 0;
                while (*v && (!end || v < end) && *v != '"') {
                    if (*v == '\\' && v[1] && (!end || v + 1 < end)) v++;
                    if (o + 1 < max_out) out[o++] = *v;
                    v++;
                }
                out[o] = 0;
                return 1;
            }
        }
    }
    return 0;
}

static int json_find_str(const char *json, const char *key, char *out, unsigned long max_out) {
    return json_find_str_bounded(json, 0, key, out, max_out);
}

/* Advances past a JSON string starting at opening '"'; returns pointer after closing '"'. */
static const char *json_skip_str(const char *p, const char *end) {
    if (!p || *p != '"') return p;
    p++;
    while (*p && (!end || p < end)) {
        if (*p == '\\' && p[1] && (!end || p + 1 < end)) {
            p += 2;
            continue;
        }
        if (*p == '"') return p + 1;
        p++;
    }
    return p;
}

/* Given pointer at '{' or '[', returns pointer just after the matching '}' or ']'. */
static const char *json_span_end(const char *p, const char *end) {
    if (!p || (*p != '{' && *p != '[')) return p;
    int depth = 0;
    while (*p && (!end || p < end)) {
        if (*p == '"') {
            p = json_skip_str(p, end);
            continue;
        }
        if (*p == '{' || *p == '[') depth++;
        else if (*p == '}' || *p == ']') {
            depth--;
            if (depth == 0) return p + 1;
        }
        p++;
    }
    return p;
}

/* Finds value pointer for direct child key inside JSON object [obj, end). */
static const char *json_find_key_in_obj(const char *obj, const char *end, const char *key) {
    if (!obj || *obj != '{') return 0;
    unsigned long klen = str_len(key);
    int depth = 0;
    const char *p = obj;
    while (*p && (!end || p < end)) {
        if (*p == '"') {
            int match = (depth == 1 && (!end || p + 1 + klen < end) &&
                         mem_equal(p + 1, key, klen) && p[1 + klen] == '"');
            p = json_skip_str(p, end);
            if (match) {
                while (*p && (!end || p < end) && (*p == ' ' || *p == ':' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
                return p;
            }
            continue;
        }
        if (*p == '{' || *p == '[') depth++;
        else if (*p == '}' || *p == ']') {
            depth--;
            if (depth <= 0) break;
        }
        p++;
    }
    return 0;
}

static int json_find_bool(const char *json, const char *key, int *val) {
    if (!json || !key || !val) return 0;
    unsigned long klen = str_len(key);
    for (unsigned long i = 0; json[i]; i++) {
        if (json[i] == '"' && mem_equal(json + i + 1, key, klen) && json[i + 1 + klen] == '"') {
            unsigned long idx = i + 1 + klen + 1;
            while (json[idx] == ' ' || json[idx] == ':') idx++;
            if (mem_equal(json + idx, "true", 4)) {
                *val = 1;
                return 1;
            }
            if (mem_equal(json + idx, "false", 5)) {
                *val = 0;
                return 1;
            }
        }
    }
    return 0;
}

static int json_find_uint64(const char *json, const char *key, uint64_t *val) {
    if (!json || !key || !val) return 0;
    unsigned long klen = str_len(key);
    for (unsigned long i = 0; json[i]; i++) {
        if (json[i] == '"' && mem_equal(json + i + 1, key, klen) && json[i + 1 + klen] == '"') {
            unsigned long idx = i + 1 + klen + 1;
            while (json[idx] == ' ' || json[idx] == ':') idx++;
            if (json[idx] >= '0' && json[idx] <= '9') {
                uint64_t v = 0;
                while (json[idx] >= '0' && json[idx] <= '9') {
                    v = v * 10 + (uint64_t)(json[idx] - '0');
                    idx++;
                }
                *val = v;
                return 1;
            }
        }
    }
    return 0;
}

static int json_find_int(const char *json, const char *key, int *val) {
    if (!json || !key || !val) return 0;
    unsigned long klen = str_len(key);
    for (unsigned long i = 0; json[i]; i++) {
        if (json[i] == '"' && mem_equal(json + i + 1, key, klen) && json[i + 1 + klen] == '"') {
            unsigned long idx = i + 1 + klen + 1;
            while (json[idx] == ' ' || json[idx] == ':') idx++;
            int sign = 1;
            if (json[idx] == '-') {
                sign = -1;
                idx++;
            }
            if (json[idx] >= '0' && json[idx] <= '9') {
                int v = 0;
                while (json[idx] >= '0' && json[idx] <= '9') {
                    v = v * 10 + (int)(json[idx] - '0');
                    idx++;
                }
                *val = sign * v;
                return 1;
            }
        }
    }
    return 0;
}

/* Word-boundary helpers. Cyrillic bytes (>= 0x80) count as word characters, so
 * "тв" no longer matches inside "молитва" and "бра" no longer matches "брат". */
static int is_word_byte(uint8_t c) {
    return c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

/* needle occurs at the start of a word (prefix match: "свет" matches "свет", "света", "светильник") */
static int str_has_word_prefix(const char *haystack, const char *needle) {
    unsigned long hlen = str_len(haystack);
    unsigned long nlen = str_len(needle);
    if (nlen == 0 || hlen < nlen) return 0;
    for (unsigned long i = 0; i + nlen <= hlen; i++) {
        if (i > 0 && is_word_byte((uint8_t)haystack[i - 1])) continue;
        if (mem_equal(haystack + i, needle, nlen)) return 1;
    }
    return 0;
}

/* needle occurs as a whole word */
static int str_has_word(const char *haystack, const char *needle) {
    unsigned long hlen = str_len(haystack);
    unsigned long nlen = str_len(needle);
    if (nlen == 0 || hlen < nlen) return 0;
    for (unsigned long i = 0; i + nlen <= hlen; i++) {
        if (i > 0 && is_word_byte((uint8_t)haystack[i - 1])) continue;
        if (i + nlen < hlen && is_word_byte((uint8_t)haystack[i + nlen])) continue;
        if (mem_equal(haystack + i, needle, nlen)) return 1;
    }
    return 0;
}

static const char *after_voice_word(const char *text, const char *word) {
    unsigned long len = str_len(word);
    if (str_len(text) < len || !mem_equal(text, word, len)) return 0;
    text += len;
    if (*text && *text != ' ' && *text != '\t') return 0;
    while (*text == ' ' || *text == '\t') text++;
    return text;
}

static const char *skip_voice_fillers(const char *text) {
    static const char *const fillers[] = {"алиса,", "алиса", "пожалуйста", "давай"};
    while (*text == ' ' || *text == '\t') text++;
    for (;;) {
        const char *rest = 0;
        for (unsigned long i = 0; i < sizeof(fillers) / sizeof(fillers[0]); i++) {
            rest = after_voice_word(text, fillers[i]);
            if (rest) break;
        }
        if (!rest) break;
        text = rest;
    }
    return text;
}

/* AirPlay receiver commands are owned by Quasar Bridge (see its sendAlExtDirective
 * handling), not by this bridge: Soloist only plays Spotify. Recognised by the
 * receiver's aliases, because Alice's ASR may spell "эйрплей" differently. An
 * explicit media word keeps the phrase a real Spotify search. */
static int is_airplay_command(const char *lower) {
    if (str_contains(lower, "песн") || str_contains(lower, "трек") ||
        str_contains(lower, "музык") || str_contains(lower, "альбом") ||
        str_contains(lower, "плейлист") || str_contains(lower, "исполнител") ||
        str_contains(lower, "групп")) {
        return 0;
    }
    return str_contains(lower, "airplay") || str_contains(lower, "эйрплей") ||
           str_contains(lower, "эирплей") || str_contains(lower, "аирплей") ||
           str_contains(lower, "эйр плей") || str_contains(lower, "эир плей") ||
           str_contains(lower, "аир плей") || str_contains(lower, "air play");
}

static int is_named_playlist_request(const char *text) {
    static const char *const verbs[] = {"включи", "поставь", "сыграй", "запусти", "найди", "вруби", "слушать"};
    text = skip_voice_fillers(text);
    for (unsigned long i = 0; i < sizeof(verbs) / sizeof(verbs[0]); i++) {
        const char *rest = after_voice_word(text, verbs[i]);
        if (rest) return after_voice_word(rest, "плейлист") != 0;
    }
    return 0;
}

static unsigned long trim_voice_span_end(const char *s, unsigned long len) {
    while (len > 0) {
        char c = s[len - 1];
        if (c == ' ' || c == '\t' || c == '.' || c == '!' ||
            c == '?' || c == ',' || c == ';' || c == ':') {
            len--;
        } else {
            break;
        }
    }
    return len;
}

/* Standalone smart-home lighting color/mode requests (e.g. "включи красный",
 * "поставь фиолетовый в спальне", "включи в спальне зелёный", "включи ночной режим")
 * stay with Alice, while track titles containing color adjectives (e.g.
 * "включи синий трактор", "включи белый снег") still route to Spotify search. */
static int is_smart_color_command(const char *lower) {
    static const char *const verbs[] = {
        "включи", "поставь", "сделай", "запусти", "вруби"
    };
    static const char *const colors[] = {
        "красный", "синий", "зелёный", "зеленый", "жёлтый", "желтый",
        "белый", "оранжевый", "фиолетовый", "розовый", "голубой",
        "бирюзовый", "пурпурный", "сиреневый", "салатовый", "малиновый",
        "янтарный", "лиловый",
        "тёплый", "теплый", "холодный", "дневной",
        "тёплый белый", "теплый белый", "холодный белый",
        "дневной белый", "нейтральный белый",
        "ночной режим", "режим чтения", "режим сна"
    };
    static const char *const loc_preps[] = {" в ", " на ", " у "};
    static const char *const preps[] = {"в", "на", "у"};

    if (!lower) return 0;
    const char *p = skip_voice_fillers(lower);
    const char *after_verb = 0;
    for (unsigned long i = 0; i < sizeof(verbs) / sizeof(verbs[0]); i++) {
        after_verb = after_voice_word(p, verbs[i]);
        if (after_verb) break;
    }
    if (!after_verb) return 0;

    p = skip_voice_fillers(after_verb);
    unsigned long len = trim_voice_span_end(p, str_len(p));
    const char *please = " пожалуйста";
    unsigned long please_len = str_len(please);
    if (len > please_len && mem_equal(p + len - please_len, please, please_len)) {
        len = trim_voice_span_end(p, len - please_len);
    }
    if (len == 0) return 0;

    for (unsigned long i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        unsigned long clen = str_len(colors[i]);
        if (len == clen && mem_equal(p, colors[i], clen)) return 1;
        if (len > clen && mem_equal(p, colors[i], clen)) {
            for (unsigned long j = 0; j < sizeof(loc_preps) / sizeof(loc_preps[0]); j++) {
                unsigned long plen = str_len(loc_preps[j]);
                if (len >= clen + plen && mem_equal(p + clen, loc_preps[j], plen)) {
                    return 1;
                }
            }
        }
    }

    for (unsigned long j = 0; j < sizeof(preps) / sizeof(preps[0]); j++) {
        const char *q = after_voice_word(p, preps[j]);
        if (!q || (unsigned long)(q - p) >= len) continue;
        while ((unsigned long)(q - p) < len && *q != ' ' && *q != '\t') q++;
        while ((unsigned long)(q - p) < len && (*q == ' ' || *q == '\t')) q++;
        unsigned long rlen = (unsigned long)((p + len) - q);
        for (unsigned long i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
            unsigned long clen = str_len(colors[i]);
            if (rlen == clen && mem_equal(q, colors[i], clen)) return 1;
        }
    }
    return 0;
}

static int has_explicit_media_marker(const char *lower) {
    return str_has_word_prefix(lower, "песн") ||
           str_has_word_prefix(lower, "трек") ||
           str_has_word_prefix(lower, "альбом") ||
           str_has_word_prefix(lower, "групп") ||
           str_has_word_prefix(lower, "исполнител") ||
           str_has_word_prefix(lower, "артист") ||
           str_has_word_prefix(lower, "плейлист");
}

/* Explicit persistent mode commands ("режим яндекс", "режим пульта", "режим спотифай",
 * "какой сейчас режим"). Returns -1 when unrelated, 0=spotify, 1=yandex, 2=remote, 3=query. */
static int parse_voice_mode_command(const char *lower) {
    static const char *const query_phrases[] = {
        "какой режим",
        "какой сейчас режим",
        "текущий режим",
        "какой режим музыки",
        "какой музыкальный режим",
        "какой режим спотифай",
        "какой режим spotify"
    };
    static const char *const exact_spotify[] = {
        "верни спотифай",
        "включи спотифай",
        "включи spotify"
    };
    if (!lower || has_explicit_media_marker(lower)) return -1;

    const char *p = skip_voice_fillers(lower);
    unsigned long len = trim_voice_span_end(p, str_len(p));

    for (unsigned long i = 0; i < sizeof(query_phrases) / sizeof(query_phrases[0]); i++) {
        unsigned long qlen = str_len(query_phrases[i]);
        if (len == qlen && mem_equal(p, query_phrases[i], qlen)) return 3;
    }

    if ((str_has_word_prefix(p, "режим") &&
         (str_contains(p, "яндекс") || str_contains(p, "я.музык") || str_has_word_prefix(p, "алис"))) ||
        ((str_contains(p, "переключ") || str_contains(p, "перейд") || str_contains(p, "верн")) &&
         (str_contains(p, "на яндекс") || str_contains(p, "на я.музык") || str_contains(p, "на алис")))) {
        return 1;
    }

    if ((str_has_word_prefix(p, "режим") &&
         (str_has_word_prefix(p, "пульт") || str_contains(p, "удалён") ||
          str_contains(p, "удален") || str_contains(p, "ремоут") || str_has_word(p, "remote"))) ||
        ((str_contains(p, "переключ") || str_contains(p, "перейд") || str_contains(p, "включ")) &&
         (str_contains(p, "на пульт") || str_contains(p, "пульт спотифай") || str_contains(p, "пульт spotify")))) {
        return 2;
    }

    if ((str_has_word_prefix(p, "режим") &&
         (str_contains(p, "спотифай") || str_contains(p, "spotify") || str_has_word_prefix(p, "колонк"))) ||
        ((str_contains(p, "переключ") || str_contains(p, "перейд") || str_contains(p, "верн")) &&
         (str_contains(p, "на спотифай") || str_contains(p, "на spotify")))) {
        return 0;
    }

    for (unsigned long i = 0; i < sizeof(exact_spotify) / sizeof(exact_spotify[0]); i++) {
        unsigned long slen = str_len(exact_spotify[i]);
        if (len == slen && mem_equal(p, exact_spotify[i], slen)) return 0;
    }
    return -1;
}

/* Explicit requests to transfer Spotify Connect playback to this speaker ("играй здесь",
 * "переключи на колонку", "перенеси сюда"). Track/playlist words keep catalog searches intact. */
static int is_claim_speaker_command(const char *lower) {
    static const char *const phrases[] = {
        "играй здесь", "играй тут", "играй на колонке",
        "включи здесь", "включи тут", "включи на колонке",
        "переключи на колонку", "переведи на колонку", "перенеси на колонку",
        "передай на колонку", "перекинь на колонку",
        "звук на колонку", "музыку на колонку",
        "переключи сюда", "переведи сюда", "перенеси сюда",
        "переключи звук сюда", "переведи звук сюда", "перенеси звук сюда",
        "переключи музыку сюда", "переведи музыку сюда", "перенеси музыку сюда"
    };
    if (!lower || has_explicit_media_marker(lower)) return 0;
    const char *p = skip_voice_fillers(lower);
    for (unsigned long i = 0; i < sizeof(phrases) / sizeof(phrases[0]); i++) {
        if (str_contains(p, phrases[i])) return 1;
    }
    return 0;
}

/* "включи песню Назад в СССР" / "поставь группу Стоп" / "включи Кино" etc.: a play verb
 * means a catalog search, even if the title contains control words such as "назад",
 * "дальше", "стоп" or "пауза". */
static int is_explicit_search(const char *lower) {
    if (str_has_word_prefix(lower, "выключи") || str_has_word_prefix(lower, "отключи")) {
        return 0;
    }
    int has_verb = str_has_word_prefix(lower, "включи") || str_has_word_prefix(lower, "поставь") ||
                   str_has_word_prefix(lower, "сыграй") || str_has_word_prefix(lower, "запусти") ||
                   str_has_word_prefix(lower, "найди") || str_has_word_prefix(lower, "вруби") ||
                   str_has_word_prefix(lower, "слушать");
    if (!has_verb) return 0;

    // Check if it is a simple resume command rather than a specific track/artist query.
    // Polite fillers are allowed at the start ("алиса, давай") and between the verb and
    // "музыку" ("включи пожалуйста музыку"). Anything after "музыку" keeps the phrase a search.
    {
        const char *p = lower;
        int ok = 1;
        unsigned long n;

        for (;;) {
            unsigned long rest;
            while (*p == ' ') p++;
            rest = str_len(p);
            n = str_len("алиса");
            if (rest > n && mem_equal(p, "алиса", n) && !is_word_byte((uint8_t)p[n])) {
                p += n;
                if (*p == ',') p++;
                continue;
            }
            n = str_len("пожалуйста");
            if (rest > n && mem_equal(p, "пожалуйста", n) && !is_word_byte((uint8_t)p[n])) { p += n; continue; }
            n = str_len("давай");
            if (rest > n && mem_equal(p, "давай", n) && !is_word_byte((uint8_t)p[n])) { p += n; continue; }
            break;
        }

        n = str_len("включи");
        if (str_len(p) >= n && mem_equal(p, "включи", n) && !is_word_byte((uint8_t)p[n])) p += n;
        else {
            n = str_len("вруби");
            if (str_len(p) >= n && mem_equal(p, "вруби", n) && !is_word_byte((uint8_t)p[n])) p += n;
            else {
                n = str_len("включить");
                if (str_len(p) >= n && mem_equal(p, "включить", n) && !is_word_byte((uint8_t)p[n])) p += n;
                else {
                    n = str_len("слушать");
                    if (str_len(p) >= n && mem_equal(p, "слушать", n) && !is_word_byte((uint8_t)p[n])) p += n;
                    else ok = 0;
                }
            }
        }

        if (ok) {
            for (;;) {
                unsigned long rest;
                while (*p == ' ') p++;
                rest = str_len(p);
                n = str_len("пожалуйста");
                if (rest > n && mem_equal(p, "пожалуйста", n) && !is_word_byte((uint8_t)p[n])) { p += n; continue; }
                n = str_len("давай");
                if (rest > n && mem_equal(p, "давай", n) && !is_word_byte((uint8_t)p[n])) { p += n; continue; }
                break;
            }
            while (*p == ' ') p++;
            n = str_len("музыку");
            if (str_len(p) == n && mem_equal(p, "музыку", n)) return 0;
        }
    }
    return 1;
}

static void trim_trailing_voice_punct(char *buf) {
    unsigned long n = str_len(buf);
    while (n > 0) {
        char c = buf[n - 1];
        if (c == ' ' || c == '\t' || c == '.' || c == ',' || c == '!' || c == '?' || c == ':') {
            buf[--n] = 0;
        } else {
            break;
        }
    }
}

static int is_valid_queue_query(const char *q) {
    while (*q == ' ' || *q == '\t') q++;
    if (!*q) return 0;
    // Reject empty or filler-only queue requests such as "поставь следующим трек"
    if (str_equal(q, "трек") || str_equal(q, "песню") || str_equal(q, "песня") ||
        str_equal(q, "музыку") || str_equal(q, "что-нибудь")) {
        return 0;
    }
    return 1;
}

/* Extracts `<query>` from voice queue commands:
 *   "поставь следующим <X>", "включи следующей <X>", "добавь в очередь <X>",
 *   "поставь <X> в очередь", etc. Returns 1 and writes clean query if matched. */
static int has_enqueue_query(const char *lower, char *query_out, unsigned long max_out) {
    if (!lower || !query_out || max_out == 0) return 0;
    query_out[0] = 0;

    static const char *const prefixes[] = {
        "поставь следующим треком",
        "поставь следующей песней",
        "поставь следующим",
        "поставь следующей",
        "поставь следующее",
        "включи следующим треком",
        "включи следующей песней",
        "включи следующим",
        "включи следующей",
        "включи следующее",
        "добавь в конец очереди",
        "поставь в конец очереди",
        "добавь в очередь",
        "поставь в очередь",
        "добавь следующим треком",
        "добавь следующей песней",
        "добавь следующим",
        "добавь следующей",
        "в очередь",
    };

    const char *p = skip_voice_fillers(lower);
    for (unsigned long i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        const char *rest = after_voice_word(p, prefixes[i]);
        if (rest) {
            str_copy(query_out, rest, max_out);
            trim_trailing_voice_punct(query_out);
            if (!is_valid_queue_query(query_out)) {
                query_out[0] = 0;
                return 0;
            }
            return 1;
        }
    }

    // Handle trailing placement: "добавь <X> в очередь" / "поставь <X> следующим"
    static const char *const verbs[] = {"добавь", "поставь", "включи"};
    const char *body = 0;
    for (unsigned long i = 0; i < sizeof(verbs) / sizeof(verbs[0]); i++) {
        body = after_voice_word(p, verbs[i]);
        if (body) break;
    }
    if (!body) return 0;

    char buf[512];
    str_copy(buf, body, sizeof(buf));
    trim_trailing_voice_punct(buf);

    static const char *const suffixes[] = {
        " в конец очереди",
        " в очередь",
        " следующим треком",
        " следующей песней",
        " следующим",
        " следующей",
    };
    unsigned long blen = str_len(buf);
    for (unsigned long i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        unsigned long slen = str_len(suffixes[i]);
        if (blen > slen && mem_equal(buf + blen - slen, suffixes[i], slen)) {
            buf[blen - slen] = 0;
            trim_trailing_voice_punct(buf);
            if (!is_valid_queue_query(buf)) return 0;
            str_copy(query_out, buf, max_out);
            return 1;
        }
    }
    return 0;
}

/* Matches whole-utterance restart commands without hijacking track titles
 * ("включи песню Сначала") or non-playback phrases ("сначала включи свет"). */
static int is_restart_track_request(const char *lower) {
    if (!lower) return 0;
    char norm[256];
    str_copy(norm, skip_voice_fillers(lower), sizeof(norm));
    trim_trailing_voice_punct(norm);
    if (!norm[0]) return 0;

    static const char *const phrases[] = {
        "сначала",
        "заново",
        "начни сначала",
        "начни с начала",
        "начни заново",
        "начать сначала",
        "начать заново",
        "начни трек сначала",
        "начни песню сначала",
        "начни этот трек сначала",
        "начни эту песню сначала",
        "трек сначала",
        "включи сначала",
        "поставь сначала",
        "запусти сначала",
        "сыграй сначала",
        "повтори сначала",
        "включи с начала",
        "поставь с начала",
        "включи заново",
        "поставь заново",
        "запусти заново",
        "сыграй заново",
        "перемотай в начало",
        "перемотай на начало",
        "мотай в начало",
        "в начало",
        "в начало трека",
        "в начало песни",
        "с самого начала",
        "начни с самого начала",
        "включи с самого начала",
        "поставь с самого начала",
    };
    for (unsigned long i = 0; i < sizeof(phrases) / sizeof(phrases[0]); i++) {
        if (str_equal(norm, phrases[i])) return 1;
    }
    return 0;
}

static void append_artist_name(char *dst, unsigned long max_dst, const char *name) {
    if (!name || !*name) return;
    unsigned long len = str_len(dst);
    unsigned long nlen = str_len(name);
    if (len > 0) {
        if (len + 2 + nlen >= max_dst) return;
        dst[len++] = ',';
        dst[len++] = ' ';
    } else if (nlen >= max_dst) {
        nlen = max_dst - 1;
    }
    for (unsigned long i = 0; i < nlen && len + 1 < max_dst; i++) {
        dst[len++] = name[i];
    }
    dst[len] = 0;
}

static void extract_track_info(const char *json) {
    if (!json || *json != '{') return;

    const char *root_end = json_span_end(json, 0);
    const char *item_val = json_find_key_in_obj(json, root_end, "item");

    if (item_val) {
        if (mem_equal(item_val, "null", 4)) {
            current_track_name[0] = 0;
            current_artist_name[0] = 0;
            return;
        }
        if (*item_val == '{') {
            const char *item_end = json_span_end(item_val, root_end);
            char buf[128] = {0};

            // Reset metadata only when an item payload carries decorations/name
            const char *dec = json_find_key_in_obj(item_val, item_end, "decorations");
            if (dec && *dec == '{') {
                const char *dec_end = json_span_end(dec, item_end);
                const char *id = json_find_key_in_obj(dec, dec_end, "identity");
                if (id && *id == '{') {
                    const char *id_end = json_span_end(id, dec_end);
                    if (json_find_str_bounded(id, id_end, "name", buf, sizeof(buf))) {
                        str_copy(current_track_name, buf, sizeof(current_track_name));
                    }
                }

                current_artist_name[0] = 0;
                const char *creators = json_find_key_in_obj(dec, dec_end, "creators");
                if (creators && *creators == '[') {
                    const char *cr_end = json_span_end(creators, dec_end);
                    const char *p = creators + 1;
                    int artist_count = 0;
                    while (p < cr_end && artist_count < 3) {
                        while (p < cr_end && *p != '{' && *p != ']') p++;
                        if (p >= cr_end || *p != '{') break;
                        const char *el_end = json_span_end(p, cr_end);
                        const char *ent = json_find_key_in_obj(p, el_end, "entity");
                        const char *ent_end = (ent && *ent == '{') ? json_span_end(ent, el_end) : el_end;
                        const char *ent_obj = (ent && *ent == '{') ? ent : p;
                        const char *cdec = json_find_key_in_obj(ent_obj, ent_end, "decorations");
                        const char *cid_scope = (cdec && *cdec == '{') ? cdec : ent_obj;
                        const char *cid_end = (cdec && *cdec == '{') ? json_span_end(cdec, ent_end) : ent_end;
                        const char *cid = json_find_key_in_obj(cid_scope, cid_end, "identity");
                        if (cid && *cid == '{') {
                            const char *cid_obj_end = json_span_end(cid, cid_end);
                            char abuf[128] = {0};
                            if (json_find_str_bounded(cid, cid_obj_end, "name", abuf, sizeof(abuf))) {
                                append_artist_name(current_artist_name, sizeof(current_artist_name), abuf);
                                artist_count++;
                            }
                        }
                        p = el_end;
                    }
                }
            } else if (json_find_str_bounded(item_val, item_end, "name", buf, sizeof(buf))) {
                str_copy(current_track_name, buf, sizeof(current_track_name));
                if (json_find_str_bounded(item_val, item_end, "artist", buf, sizeof(buf))) {
                    str_copy(current_artist_name, buf, sizeof(current_artist_name));
                }
            }
            return;
        }
    }

    // Fallback for flat diagnostic payloads without nested "item"
    char name_buf[128] = {0};
    char artist_buf[128] = {0};
    if (json_find_str_bounded(json, root_end, "name", name_buf, sizeof(name_buf)) &&
        !str_contains(json, "\"device_name\"")) {
        str_copy(current_track_name, name_buf, sizeof(current_track_name));
    }
    if (json_find_str_bounded(json, root_end, "artist", artist_buf, sizeof(artist_buf))) {
        str_copy(current_artist_name, artist_buf, sizeof(current_artist_name));
    }
}

#endif
