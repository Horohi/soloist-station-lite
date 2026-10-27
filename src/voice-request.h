#ifndef SOLOIST_VOICE_REQUEST_H
#define SOLOIST_VOICE_REQUEST_H
#include <stdint.h>

static uint64_t parse_varint(const uint8_t *data, unsigned long len, unsigned long *offset) {
    uint64_t res = 0;
    int shift = 0;
    while (*offset < len && shift < 64) {
        uint8_t b = data[(*offset)++];
        res |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) break;
        shift += 7;
    }
    return res;
}

static int find_field(const uint8_t *data, unsigned long len, uint32_t field_num, int expected_wire, const uint8_t **val_ptr, unsigned long *val_len, uint64_t *varint_val) {
    unsigned long offset = 0;
    while (offset < len) {
        uint64_t tag = parse_varint(data, len, &offset);
        uint32_t fnum = (uint32_t)(tag >> 3);
        int wire = (int)(tag & 7);
        if (fnum == 0) return 0;
        if (wire == 0) {
            uint64_t v = parse_varint(data, len, &offset);
            if (fnum == field_num && expected_wire == 0) {
                if (varint_val) *varint_val = v;
                return 1;
            }
        } else if (wire == 2) {
            uint64_t flen = parse_varint(data, len, &offset);
            if (flen > len - offset) return 0;
            if (fnum == field_num && expected_wire == 2) {
                if (val_ptr) *val_ptr = &data[offset];
                if (val_len) *val_len = flen;
                return 1;
            }
            offset += flen;
        } else if (wire == 1) {
            offset += 8;
        } else if (wire == 5) {
            offset += 4;
        } else {
            return 0;
        }
    }
    return 0;
}

/* Firmware supports direct AliceState and a typed field-252 envelope. */
static int voice_state_payload(const uint8_t *frame, unsigned long len,
                               const uint8_t **state, unsigned long *state_len) {
    if (find_field(frame, len, 3, 2, state, state_len, 0)) return 1;
    uint64_t type = 0;
    return find_field(frame, len, 253, 0, 0, 0, &type) && type == 3 &&
           find_field(frame, len, 252, 2, state, state_len, 0);
}

struct voice_request_state {
    /* shortcut: 16 recent IDs in memory; use persistent storage for replay across restarts. */
    uint8_t ids[16][128];
    unsigned long lengths[16];
    unsigned next;
    uint64_t previous_started;
    int armed;
};

static int voice_request_read(struct voice_request_state *cache,
                              const uint8_t *frame, unsigned long len,
                              const uint8_t **text, unsigned long *text_len) {
    const uint8_t *state = 0, *request = 0, *id = 0;
    unsigned long state_len = 0, request_len = 0, id_len = 0;
    uint64_t mode = 0, final = 0, started = 0;
    if (!voice_state_payload(frame, len, &state, &state_len) ||
        !find_field(state, state_len, 1, 0, 0, 0, &mode)) return 0;

    if (find_field(state, state_len, 15, 2, &request, &request_len, 0)) {
        find_field(request, request_len, 1, 2, text, text_len, 0);
        find_field(request, request_len, 2, 0, 0, 0, &final);
        find_field(request, request_len, 4, 0, 0, 0, &started);
    } else if (find_field(state, state_len, 13, 2, &request, &request_len, 0)) {
        find_field(request, request_len, 1, 2, text, text_len, 0);
        find_field(request, request_len, 4, 0, 0, 0, &final);
    }
    if (mode == 0) cache->armed = 0;
    else if (final != 1 && (mode == 1 ||
                           (started == 1 && cache->previous_started != 1))) cache->armed = 1;
    cache->previous_started = started;
    if (mode == 0 || final != 1 || !*text || !*text_len || *text_len > 507) return 0;
    for (unsigned long i = 0; i < *text_len; i++) if (!(*text)[i]) return 0;

    /* RequestState field 3 is dialog_id, not an individual recognition ID. */
    find_field(state, state_len, 7, 2, &id, &id_len, 0);
    if (id_len > sizeof(cache->ids[0])) return 0;
    if (id_len) {
        for (unsigned n = 0; n < 16; n++) {
            if (cache->lengths[n] != id_len) continue;
            unsigned long i = 0;
            while (i < id_len && cache->ids[n][i] == id[i]) i++;
            if (i == id_len) return 0;
        }
        unsigned n = cache->next++ % 16;
        cache->lengths[n] = id_len;
        for (unsigned long i = 0; i < id_len; i++) cache->ids[n][i] = id[i];
    } else if (!cache->armed) return 0;
    cache->armed = 0;
    return 1;
}
#endif
