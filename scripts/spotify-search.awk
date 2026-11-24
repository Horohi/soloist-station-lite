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

    if (collection != "") {
        uri = field(member(root, "item"), "uri")
        if (field(member(root, "context"), "uri") != collection ||
            uri !~ /^spotify:track:[A-Za-z0-9]+$/ || length(uri) != length("spotify:track:") + 22) exit 1
        exit 0
    }

    raw_term = ENVIRON["SOLOIST_SEARCH_TERM"]
    raw_artist = ENVIRON["SOLOIST_SEARCH_ARTIST"]
    query = normalize(raw_term)
    q_comp = compact(query)
    q_phon = phonetic(raw_term)
    q_pcomp = compact(q_phon)
    q_pcount = split(q_phon, q_pwords, " ")

    artist_hint = normalize(raw_artist)
    a_hint_comp = compact(artist_hint)
    a_hint_phon = phonetic(raw_artist)
    a_hint_pcomp = compact(a_hint_phon)
    a_hint_pcount = split(a_hint_phon, a_hint_pwords, " ")

    count = split(query, words, " ")
    if (contexts) index_track_artists(root)

    if (!contexts || type != "track") {
        result = select_item(root, type)
        if (result == "" && contexts) result = select_item(root, "artist")
        if (result == "" && contexts) result = select_item(root, "album")
    } else {
        t_uri = select_item(root, "track")
        t_score = sel_best_score
        a_uri = select_item(root, "artist")
        a_score = sel_best_score
        a_base = sel_best_base
        a_corr = sel_best_corr
        if (t_score >= 50) {
            result = t_uri
        } else if (a_uri != "" && a_base >= 40 && (t_score < 45 || a_corr > 0)) {
            result = a_uri
        } else if (t_uri != "") {
            result = t_uri
        } else if (a_uri != "") {
            result = a_uri
        } else {
            result = select_item(root, "album")
        }
    }
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

function lower_utf8(s,    i, upper, lower) {
    s = tolower(s)
    upper = "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ"
    lower = "абвгдеежзийклмнопрстуфхцчшщъыьэюя"
    for (i = 1; i < length(upper); i += 2) {
        gsub(substr(upper, i, 2), substr(lower, i, 2), s)
    }
    gsub(/ё/, "е", s)
    gsub(/ā|á|à|ä|â|ã|å|Ā|Á|À|Ä|Â|Ã|Å/, "a", s)
    gsub(/ē|é|è|ë|ê|ė|Ē|É|È|Ë|Ê|Ė/, "e", s)
    gsub(/ī|í|ì|ï|î|Ī|Í|Ì|Ï|Î/, "i", s)
    gsub(/ō|ó|ò|ö|ô|õ|ø|Ō|Ó|Ò|Ö|Ô|Õ|Ø/, "o", s)
    gsub(/ū|ú|ù|ü|û|Ū|Ú|Ù|Ü|Û/, "u", s)
    gsub(/ñ|Ñ/, "n", s)
    gsub(/ç|ć|č|Ç|Ć|Č/, "c", s)
    gsub(/ý|ÿ|Ý|Ÿ/, "y", s)
    gsub(/ł|Ł/, "l", s)
    gsub(/ś|š|Ś|Š/, "s", s)
    gsub(/ź|ž|Ź|Ž/, "z", s)
    return s
}

function normalize(s) {
    s = lower_utf8(s)
    gsub(/'|’|‘/, "", s)
    gsub(/«|»|“|”|—|–|…| /, " ", s)
    gsub(/[[:punct:][:space:][:cntrl:]]+/, " ", s)
    sub(/^ /, "", s)
    sub(/ $/, "", s)
    return s
}

function compact(s) {
    gsub(/ /, "", s)
    return s
}

function contains_words(text, phrase) {
    return phrase != "" && index(" " text " ", " " phrase " ") != 0
}

function transliterate(text,    source, target, replacements, i, replacement) {
    gsub(/ье|ъе/, "ye", text)
    gsub(/ья|ъя/, "ya", text)
    gsub(/ью|ъю/, "yu", text)
    gsub(/ьи/, "yi", text)
    gsub(/ьо/, "yo", text)
    source = "абвгдежзийклмнопрстуфхцчшщъыьэюя"
    target = "a b v g d e zh z i y k l m n o p r s t u f kh ts ch sh shch - y - e yu ya"
    split(target, replacements, " ")
    for (i = 1; i < length(source); i += 2) {
        replacement = replacements[(i + 1) / 2]
        gsub(substr(source, i, 2), replacement == "-" ? "" : replacement, text)
    }
    return text
}

function collapse_digits(s,    i, n, ch, prev, nxt, out) {
    out = ""
    n = length(s)
    for (i = 1; i <= n; i++) {
        ch = substr(s, i, 1)
        if (ch == " " && i > 1 && i < n) {
            prev = substr(s, i - 1, 1)
            nxt = substr(s, i + 1, 1)
            if (prev ~ /^[0-9]$/ && nxt ~ /^[0-9]$/) continue
        }
        out = out ch
    }
    return out
}

function deleet_words(s,    i, n, ch, prev, nxt, out) {
    out = ""
    n = length(s)
    for (i = 1; i <= n; i++) {
        ch = substr(s, i, 1)
        if ((ch == "1" || ch == "0" || ch == "3" || ch == "4") && i > 1 && i < n) {
            prev = substr(s, i - 1, 1)
            nxt = substr(s, i + 1, 1)
            if (prev ~ /^[a-z]$/ && nxt ~ /^[a-z]$/) {
                if (ch == "1") ch = "i"
                else if (ch == "0") ch = "o"
                else if (ch == "3") ch = "e"
                else ch = "a"
            }
        }
        out = out ch
    }
    return out
}

function collapse_letter_repeats(s,    i, n, ch, prev, out) {
    out = ""
    prev = ""
    n = length(s)
    for (i = 1; i <= n; i++) {
        ch = substr(s, i, 1)
        if (ch ~ /^[a-z]$/ && ch == prev) continue
        out = out ch
        prev = ch
    }
    return out
}

function trim_trailing_word_e(s,    nw, ws, i, res, w) {
    nw = split(s, ws, " ")
    res = ""
    for (i = 1; i <= nw; i++) {
        w = ws[i]
        if (length(w) >= 4 && substr(w, length(w), 1) == "e") {
            w = substr(w, 1, length(w) - 1)
        }
        res = (res == "" ? w : res " " w)
    }
    return res
}

function phonetic(raw,    s) {
    s = lower_utf8(raw)
    gsub(/\$/, "s", s)
    gsub(/'|’|‘/, "", s)
    gsub(/«|»|“|”|—|–|…| /, " ", s)
    gsub(/[[:punct:][:space:][:cntrl:]]+/, " ", s)
    sub(/^ /, "", s)
    sub(/ $/, "", s)
    if (s == "") return ""

    s = transliterate(s)
    s = " " s " "
    gsub(/ (zero|nol) /, " 0 ", s)
    gsub(/ (one|van|odin) /, " 1 ", s)
    gsub(/ (two|dva) /, " 2 ", s)
    gsub(/ (three|tri) /, " 3 ", s)
    gsub(/ (four|chetyre) /, " 4 ", s)
    gsub(/ (five|fayv|pyat) /, " 5 ", s)
    gsub(/ (six|siks|shest) /, " 6 ", s)
    gsub(/ (seven|sem) /, " 7 ", s)
    gsub(/ (eight|eyt|vosem) /, " 8 ", s)
    gsub(/ (nine|nayn|devyat) /, " 9 ", s)
    gsub(/0 (percent|protsent|protsentov|protsenta) /, "0 ", s)
    gsub(/1 (percent|protsent|protsentov|protsenta) /, "1 ", s)
    gsub(/2 (percent|protsent|protsentov|protsenta) /, "2 ", s)
    gsub(/3 (percent|protsent|protsentov|protsenta) /, "3 ", s)
    gsub(/4 (percent|protsent|protsentov|protsenta) /, "4 ", s)
    gsub(/5 (percent|protsent|protsentov|protsenta) /, "5 ", s)
    gsub(/6 (percent|protsent|protsentov|protsenta) /, "6 ", s)
    gsub(/7 (percent|protsent|protsentov|protsenta) /, "7 ", s)
    gsub(/8 (percent|protsent|protsentov|protsenta) /, "8 ", s)
    gsub(/9 (percent|protsent|protsentov|protsenta) /, "9 ", s)
    gsub(/ +/, " ", s)
    sub(/^ /, "", s)
    sub(/ $/, "", s)

    s = collapse_digits(s)
    s = deleet_words(s)
    s = " " s " "
    gsub(/ 5opka /, " pyaterka ", s)
    sub(/^ /, "", s)
    sub(/ $/, "", s)

    gsub(/ph/, "f", s)
    gsub(/th/, "t", s)
    gsub(/shch/, "sh", s)
    gsub(/sch/, "sh", s)
    gsub(/dzh/, "j", s)
    gsub(/zh/, "j", s)
    gsub(/kh/, "h", s)
    gsub(/ck/, "k", s)
    gsub(/x/, "ks", s)
    gsub(/q/, "k", s)
    gsub(/w/, "v", s)

    gsub(/ce/, "se", s)
    gsub(/ci/, "si", s)
    gsub(/cy/, "si", s)
    gsub(/ch/, "\001", s)
    gsub(/c/, "k", s)
    gsub(/\001/, "ch", s)

    gsub(/ge/, "je", s)
    gsub(/ou/, "o", s)
    gsub(/ow/, "o", s)
    gsub(/oo/, "u", s)
    gsub(/ee/, "i", s)
    gsub(/ea/, "i", s)
    gsub(/ie/, "i", s)
    gsub(/y/, "i", s)

    s = trim_trailing_word_e(s)
    gsub(/e/, "a", s)
    gsub(/u/, "o", s)
    s = collapse_letter_repeats(s)
    gsub(/ +/, " ", s)
    sub(/^ /, "", s)
    sub(/ $/, "", s)
    return s
}

function edit_dist(a, b, max_d,    la, lb, i, j, cost, v, prev, cur) {
    if (a == b) return 0
    la = length(a)
    lb = length(b)
    if (la > 48 || lb > 48) return max_d + 1
    if (la - lb > max_d || lb - la > max_d) return max_d + 1
    for (j = 0; j <= lb; j++) prev[j] = j
    for (i = 1; i <= la; i++) {
        cur[0] = i
        for (j = 1; j <= lb; j++) {
            cost = (substr(a, i, 1) == substr(b, j, 1)) ? 0 : 1
            v = prev[j] + 1
            if (cur[j - 1] + 1 < v) v = cur[j - 1] + 1
            if (prev[j - 1] + cost < v) v = prev[j - 1] + cost
            cur[j] = v
        }
        for (j = 0; j <= lb; j++) prev[j] = cur[j]
    }
    return prev[lb]
}

function fuzzy_eq(a, b,    la, lb, minl) {
    if (a == "" || b == "") return 0
    if (a == b) return 1
    la = length(a)
    lb = length(b)
    minl = (la < lb) ? la : lb
    if (minl >= 5 && edit_dist(a, b, 1) <= 1) return 1
    if (minl >= 4 && substr(a, 1, 1) ~ /^[0-9]$/ && substr(a, 1, 1) == substr(b, 1, 1) && edit_dist(a, b, 1) <= 1) return 1
    if (minl >= 9 && edit_dist(a, b, 2) <= 2) return 1
    return 0
}

function word_in_list(w, arr, n,    i, c1, c2, m1, m2) {
    if (w == "") return 0
    for (i = 1; i <= n; i++) {
        if (arr[i] == w) return 2
    }
    for (i = 1; i <= n; i++) {
        if (fuzzy_eq(w, arr[i])) return 1
        if (length(w) == 3 && length(arr[i]) == 3) {
            c1 = substr(w, 1, 1); c2 = substr(w, 3, 1)
            m1 = substr(w, 2, 1); m2 = substr(arr[i], 2, 1)
            if (c1 == substr(arr[i], 1, 1) && c2 == substr(arr[i], 3, 1) &&
                (m1 == "a" || m1 == "o") && (m2 == "a" || m2 == "o")) return 1
        }
    }
    return 0
}

function all_words_match(q_ws, q_n, t_ws, t_n,    i) {
    if (q_n <= 0 || t_n <= 0) return 0
    for (i = 1; i <= q_n; i++) {
        if (!word_in_list(q_ws[i], t_ws, t_n)) return 0
    }
    return 1
}

function score_against(cand_raw, exp_norm, exp_comp, exp_phon, exp_pcomp, exp_ws, exp_n,
                       norm, comp, phon, pcomp, cand_ws, cand_n) {
    if (exp_norm == "" && exp_phon == "") return 0
    norm = normalize(cand_raw)
    if (norm == "") return 0
    comp = compact(norm)
    phon = phonetic(cand_raw)
    pcomp = compact(phon)
    if (norm == exp_norm || (exp_comp != "" && comp == exp_comp) ||
        (exp_phon != "" && (phon == exp_phon || pcomp == exp_pcomp))) {
        return 40
    }
    cand_n = split(phon, cand_ws, " ")
    if (contains_words(norm, exp_norm) || contains_words(phon, exp_phon) ||
        (exp_n >= 2 && all_words_match(exp_ws, exp_n, cand_ws, cand_n))) {
        return 30
    }
    if (fuzzy_eq(pcomp, exp_pcomp)) return 20
    return 0
}

function artists_match_query(item, exp_norm, exp_comp, exp_phon, exp_pcomp, exp_ws, exp_n,
                             names, i) {
    names = member(item, "artists")
    if (!is_array(names) || (exp_norm == "" && exp_phon == "")) return 0
    for (i = names + 1; i < after[names]; i = after[i]) {
        if (score_against(field(i, "name"), exp_norm, exp_comp, exp_phon, exp_pcomp, exp_ws, exp_n) >= 20) {
            return 1
        }
    }
    return 0
}

function index_track_artists(root,    items, i, names, a, auri) {
    items = member(member(root, "tracks"), "items")
    if (!is_array(items)) return
    for (i = items + 1; i < after[items]; i = after[i]) {
        if (flag_is(i, "is_playable", "false") || flag_is(i, "is_local", "true")) continue
        names = member(i, "artists")
        if (!is_array(names)) continue
        for (a = names + 1; a < after[names]; a = after[a]) {
            auri = field(a, "uri")
            if (auri != "") track_artist_hits[auri]++
        }
    }
}

function score_track(item, raw_title, track_idx,
                     title, p_title, tcomp, tpcomp, a, i, r_name, r_norm, r_phon,
                     a_list, a_plist, t_ws, t_n, a_ws, a_n, hit_kind, title_hits, missed,
                     rem, r_comp, r_pcomp, r_ws, r_n, p_ws, p_n, a_tmp, k) {
    if (artist_hint != "" &&
        !artists_match_query(item, artist_hint, a_hint_comp, a_hint_phon, a_hint_pcomp, a_hint_pwords, a_hint_pcount)) {
        return 0
    }

    title = normalize(raw_title)
    if (title == "") return 0
    if (query != "" && title == query) return 45

    p_title = phonetic(raw_title)
    tcomp = compact(title)
    tpcomp = compact(p_title)

    if (artist_hint == "") {
        rem = ""
        if (contains_words(query, title)) {
            if (index(query, title " ") == 1) rem = substr(query, length(title) + 2)
            else if (substr(query, length(query) - length(title)) == " " title) rem = substr(query, 1, length(query) - length(title) - 1)
        } else if (p_title != "" && contains_words(q_phon, p_title)) {
            if (index(q_phon, p_title " ") == 1) rem = substr(q_phon, length(p_title) + 2)
            else if (substr(q_phon, length(q_phon) - length(p_title)) == " " p_title) rem = substr(q_phon, 1, length(q_phon) - length(p_title) - 1)
        }
        if (rem != "") {
            r_comp = compact(rem)
            r_phon = phonetic(rem)
            r_pcomp = compact(r_phon)
            r_n = split(r_phon, r_ws, " ")
            if (artists_match_query(item, rem, r_comp, r_phon, r_pcomp, r_ws, r_n)) return 50
        }
    }

    if ((q_comp != "" && tcomp == q_comp) ||
        (q_phon != "" && (p_title == q_phon || tpcomp == q_pcomp))) {
        return 40
    }

    t_n = split(p_title, t_ws, " ")
    a_plist = ""
    a = member(item, "artists")
    if (is_array(a)) {
        for (i = a + 1; i < after[a]; i = after[i]) {
            r_name = field(i, "name")
            r_phon = phonetic(r_name)
            if (r_phon != "") a_plist = (a_plist == "" ? r_phon : a_plist " " r_phon)
        }
    }
    a_n = split(a_plist, a_ws, " ")
    # Keep compacted artist token too so spaced ASR words can match stylized names like 5opka/9mice.
    if (a_n > 1) {
        a_ws[++a_n] = compact(a_plist)
    }

    title_hits = 0
    missed = 0
    for (i = 1; i <= q_pcount; i++) {
        if (word_in_list(q_pwords[i], t_ws, t_n)) title_hits++
        else if (!word_in_list(q_pwords[i], a_ws, a_n)) missed++
    }
    if (title_hits > 0 && missed == 0) {
        if (contains_words(title, query) || contains_words(p_title, q_phon)) return 35
        return 25
    }
    if (title_hits >= 2 && missed == 1 && q_pcount >= 3 && track_idx == 1) {
        return 15
    }
    if (fuzzy_eq(tpcomp, q_pcomp)) return 20
    return allow_generic ? 1 : 0
}

function select_item(root, item_type,    items, i, idx, uri, raw_name, score, base, corr, best_score, best_base, best_corr, best) {
    sel_best_score = 0
    sel_best_base = 0
    sel_best_corr = 0
    items = member(member(root, item_type "s"), "items")
    if (!is_array(items)) return ""
    idx = 0
    for (i = items + 1; i < after[items]; i = after[i]) {
        idx++
        uri = field(i, "uri")
        if (uri !~ ("^spotify:" item_type ":[A-Za-z0-9]+$")) continue
        if (flag_is(i, "is_playable", "false") || flag_is(i, "is_local", "true")) continue
        if (item_type == "track" && censor && !flag_is(i, "explicit", "false")) continue
        raw_name = field(i, "name")
        if (normalize(raw_name) == "") continue
        if (item_type == "playlist") {
            # A fuzzy hit or two different playlists with the same name is unsafe to autoplay.
            if (normalize(raw_name) != query || length(uri) != length("spotify:playlist:") + 22) continue
            if (flag_is(i, "public", "false")) continue
            if (best != "" && uri != best) return ""
            best = uri
            continue
        }
        corr = 0
        if (item_type == "track") {
            score = score_track(i, raw_name, idx)
            base = score
        } else {
            base = score_against(raw_name, query, q_comp, q_phon, q_pcomp, q_pwords, q_pcount)
            score = base
            if (item_type == "artist" && base > 0) {
                corr = track_artist_hits[uri] + 0
                if (corr > 5) corr = 5
                score += corr
            }
        }
        if (score > best_score) {
            best_score = score
            best_base = base
            best_corr = corr
            best = uri
        }
    }
    sel_best_score = best_score
    sel_best_base = best_base
    sel_best_corr = best_corr
    return best
}
