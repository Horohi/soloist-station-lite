# Read Spotify's search response and print at most one playable catalog URI.
# Run with LC_ALL=C. JSON strings are decoded to UTF-8 before comparing names.
{
    if (length(json) + length($0) >= 524288) { oversized = 1; exit 2 }
    json = json $0 "\n"
}
END {
    if (oversized) exit 2
    pos = 1
    root = read_value(0)
    skip_space()
    if (!root || pos <= length(json) || !is_object(root) || member(root, "error")) exit 2

    query = normalize(ENVIRON["SOLOIST_SEARCH_TERM"])
    artist_hint = normalize(ENVIRON["SOLOIST_SEARCH_ARTIST"])
    count = split(query, words, " ")
    result = select_item(root, type)
    if (result == "" && contexts) result = select_item(root, "artist")
    if (result == "" && contexts) result = select_item(root, "album")
    if (result != "") print result
}

function skip_space(    c) {
    while ((c = substr(json, pos, 1)) != "" && c ~ /^[ \r\n\t]$/) pos++
}

function read_value(depth,    c, node, close_char, child, val) {
    if (depth > 32 || nodes >= 50000) return 0
    skip_space()
    c = substr(json, pos, 1)
    node = ++nodes
    kind[node] = c
    if (c == "{" || c == "[") {
        close_char = c == "{" ? "}" : "]"
        pos++
        skip_space()
        if (substr(json, pos, 1) != close_char) {
            while (1) {
                child = read_value(depth + 1)
                if (!child) return 0
                if (c == "{") {
                    skip_space()
                    if (kind[child] != "\"" || substr(json, pos++, 1) != ":") return 0
                    if (!read_value(depth + 1)) return 0
                }
                skip_space()
                if (substr(json, pos, 1) == close_char) break
                if (substr(json, pos++, 1) != ",") return 0
            }
        }
        pos++
    } else if (c == "\"") {
        if (!read_string()) return 0
        value[node] = decoded
    } else {
        if (!match(substr(json, pos), /^(false|true|null|-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][-+]?[0-9]+)?)/)) return 0
        value[node] = substr(json, pos, RLENGTH)
        pos += RLENGTH
    }
    after[node] = nodes + 1
    return node
}

function hex4(    chars, i, digit, cp) {
    chars = tolower(substr(json, pos, 4))
    if (length(chars) != 4 || chars ~ /[^0-9a-f]/) return -1
    for (i = 1; i <= 4; i++) {
        digit = index("0123456789abcdef", substr(chars, i, 1)) - 1
        cp = cp * 16 + digit
    }
    pos += 4
    return cp
}

function utf8(cp) {
    if (cp <= 127) return sprintf("%c", cp)
    if (cp <= 2047) return sprintf("%c%c", 192 + int(cp / 64), 128 + cp % 64)
    if (cp <= 65535) return sprintf("%c%c%c", 224 + int(cp / 4096), 128 + int(cp / 64) % 64, 128 + cp % 64)
    return sprintf("%c%c%c%c", 240 + int(cp / 262144), 128 + int(cp / 4096) % 64, 128 + int(cp / 64) % 64, 128 + cp % 64)
}

function read_string(    c, cp, low) {
    decoded = ""
    pos++
    while (pos <= length(json)) {
        c = substr(json, pos++, 1)
        if (c == "\"") return 1
        if (c ~ /[[:cntrl:]]/) return 0
        if (c == "\\") {
            c = substr(json, pos++, 1)
            if (c == "u") {
                cp = hex4()
                if (cp < 0) return 0
                if (cp >= 55296 && cp <= 56319) {
                    if (substr(json, pos, 2) != "\\u") return 0
                    pos += 2
                    low = hex4()
                    if (low < 56320 || low > 57343) return 0
                    cp = 65536 + (cp - 55296) * 1024 + low - 56320
                } else if (cp >= 56320 && cp <= 57343) return 0
                c = utf8(cp)
            } else if (c == "n") c = "\n"
            else if (c == "r") c = "\r"
            else if (c == "t") c = "\t"
            else if (c == "b") c = "\b"
            else if (c == "f") c = "\f"
            else if (c != "\"" && c != "/" && c != "\\") return 0
        }
        decoded = decoded c
    }
    return 0
}

function is_object(node) { return node && kind[node] == "{" }
function is_array(node) { return node && kind[node] == "[" }
function string_value(node) { return kind[node] == "\"" ? value[node] : "" }
function member(node, key,    i) {
    if (!is_object(node)) return 0
    for (i = node + 1; i < after[node]; i = after[i + 1]) {
        if (value[i] == key) return i + 1
    }
    return 0
}
function field(node, key) { return string_value(member(node, key)) }
function flag_is(node, key, expected,    f) {
    f = member(node, key)
    return kind[f] != "\"" && value[f] == expected
}

function normalize(s,    i, upper, lower) {
    s = tolower(s)
    upper = "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ"
    lower = "абвгдеежзийклмнопрстуфхцчшщъыьэюя"
    for (i = 1; i < length(upper); i += 2) {
        gsub(substr(upper, i, 2), substr(lower, i, 2), s)
    }
    gsub(/ё/, "е", s)
    gsub(/'|’|‘/, "", s)
    gsub(/«|»|“|”|—|–|…| /, " ", s)
    gsub(/[[:punct:][:space:][:cntrl:]]+/, " ", s)
    sub(/^ /, "", s)
    sub(/ $/, "", s)
    return s
}

function contains_words(text, phrase) {
    return phrase != "" && index(" " text " ", " " phrase " ") != 0
}

function transliterate(text,    source, target, replacements, i, replacement) {
    source = "абвгдежзийклмнопрстуфхцчшщъыьэюя"
    target = "a b v g d e zh z i y k l m n o p r s t u f kh ts ch sh shch - y - e yu ya"
    split(target, replacements, " ")
    for (i = 1; i < length(source); i += 2) {
        replacement = replacements[(i + 1) / 2]
        gsub(substr(source, i, 2), replacement == "-" ? "" : replacement, text)
    }
    return text
}

function artists_match(item, expected,    names, i, name, latin_expected, latin_name) {
    names = member(item, "artists")
    if (!is_array(names) || expected == "") return 0
    latin_expected = transliterate(expected)
    for (i = names + 1; i < after[names]; i = after[i]) {
        name = normalize(field(i, "name"))
        if (name == expected) return 1
        # Spotify lists some artists in Latin script, e.g. Кино appears as Kino.
        latin_name = transliterate(name)
        if (latin_name != "" && latin_name == latin_expected &&
            ((latin_name != name) != (latin_expected != expected))) return 1
    }
    return 0
}

function score_track(item, title,    i, a, n, artists, hits, w) {
    if (artist_hint != "" && !artists_match(item, artist_hint)) return 0
    if (query != "" && title == query) return 4
    if (artist_hint == "" && contains_words(query, title)) {
        a = query
        if (index(a, title " ") == 1) a = substr(a, length(title) + 2)
        else if (substr(a, length(a) - length(title)) == " " title) a = substr(a, 1, length(a) - length(title) - 1)
        if (artists_match(item, a)) return 5
    }
    a = member(item, "artists")
    if (is_array(a)) {
        for (i = a + 1; i < after[a]; i = after[i]) {
            n = normalize(field(i, "name"))
            if (n != "") artists = artists " " n
        }
    }
    for (i = 1; i <= count; i++) {
        w = words[i]
        if (contains_words(title, w)) hits++
        else if (!contains_words(artists, w)) return allow_generic ? 1 : 0
    }
    if (hits && contains_words(title, query)) return 3
    if (hits) return 2
    return allow_generic ? 1 : 0
}

function select_item(root, item_type,    items, i, uri, name, score, best_score, best) {
    items = member(member(root, item_type "s"), "items")
    if (!is_array(items)) return ""
    for (i = items + 1; i < after[items]; i = after[i]) {
        uri = field(i, "uri")
        if (uri !~ ("^spotify:" item_type ":[A-Za-z0-9]+$")) continue
        if (flag_is(i, "is_playable", "false") || flag_is(i, "is_local", "true")) continue
        if (item_type == "track" && censor && !flag_is(i, "explicit", "false")) continue
        name = normalize(field(i, "name"))
        if (name == "") continue
        if (item_type == "playlist") {
            # A fuzzy hit or two different playlists with the same name is unsafe to autoplay.
            if (name != query || length(uri) != length("spotify:playlist:") + 22) continue
            if (flag_is(i, "public", "false")) continue
            if (best != "" && uri != best) return ""
            best = uri
            continue
        }
        if (item_type == "track") score = score_track(i, name)
        else score = query != "" && query == name ? 2 : contains_words(name, query)
        if (score > best_score) {
            best_score = score
            best = uri
        }
    }
    return best
}
