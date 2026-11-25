#ifndef SOLOIST_QUASAR_VOLUME_H
#define SOLOIST_QUASAR_VOLUME_H

#include <stdint.h>
#include "voice-request.h"

/* Format unsigned int into decimal ASCII; returns character count written (excluding NUL). */
static unsigned long format_uint_ascii(char *dst, int val) {
    if (val <= 0) {
        dst[0] = '0';
        dst[1] = 0;
        return 1;
    }
    char digits[12];
    int dcnt = 0;
    int temp = val;
    while (temp > 0 && dcnt < 12) {
        digits[dcnt++] = (char)('0' + (temp % 10));
        temp /= 10;
    }
    unsigned long len = 0;
    while (dcnt > 0) dst[len++] = digits[--dcnt];
    dst[len] = 0;
    return len;
}

/* Pure bitwise IEEE-754 float <-> int (0..100) conversions without libm/libgcc. */
static uint32_t int_to_f32_bits(int v) {
    if (v <= 0) return 0;
    if (v > 100) v = 100;
    uint32_t u = (uint32_t)v;
    int msb = 0;
    uint32_t tmp = u;
    while (tmp >>= 1) msb++;
    uint32_t mant = (u << (23 - msb)) & 0x7fffffu;
    uint32_t exp = (uint32_t)(127 + msb);
    return (exp << 23) | mant;
}

static int f32_bits_to_int(uint32_t bits) {
    if (bits & 0x80000000u) return 0;
    int exp = (int)((bits >> 23) & 0xffu) - 127;
    if (exp < -1) return 0;
    if (exp == -1) return 1;
    if (exp > 7) return 100;
    uint32_t sig = (1u << 23) | (bits & 0x7fffffu);
    int shift = 23 - exp;
    int val = (int)((sig + (1u << (shift - 1))) >> shift);
    if (val > 100) val = 100;
    return val;
}

static int find_fixed32(const uint8_t *data, unsigned long len, uint32_t field_num, uint32_t *val_out) {
    unsigned long offset = 0;
    while (offset < len) {
        uint64_t tag = parse_varint(data, len, &offset);
        uint32_t fnum = (uint32_t)(tag >> 3);
        int wire = (int)(tag & 7);
        if (fnum == 0) return 0;
        if (wire == 0) {
            parse_varint(data, len, &offset);
        } else if (wire == 2) {
            uint64_t flen = parse_varint(data, len, &offset);
            if (flen > len - offset) return 0;
            offset += flen;
        } else if (wire == 1) {
            if (8 > len - offset) return 0;
            offset += 8;
        } else if (wire == 5) {
            if (4 > len - offset) return 0;
            if (fnum == field_num) {
                if (val_out) {
                    *val_out = (uint32_t)data[offset] |
                               ((uint32_t)data[offset + 1] << 8) |
                               ((uint32_t)data[offset + 2] << 16) |
                               ((uint32_t)data[offset + 3] << 24);
                }
                return 1;
            }
            offset += 4;
        } else {
            return 0;
        }
    }
    return 0;
}

/* Parse Quasar StereoPair (field 198 -> 1 -> 11 VolumeManager.State) on port 9921. */
static int parse_quasar_vol_frame(const uint8_t *frame, unsigned long frame_len, int *vol_out, uint64_t *src_out) {
    const uint8_t *sp_body = 0;
    unsigned long sp_len = 0;
    uint64_t msg_type = 0;
    if (find_field(frame, frame_len, 198, 2, &sp_body, &sp_len, 0)) {
        quasar_uses_f252 = 0;
    } else if (find_field(frame, frame_len, 253, 0, 0, 0, &msg_type) && msg_type == 198 &&
               find_field(frame, frame_len, 252, 2, &sp_body, &sp_len, 0)) {
        quasar_uses_f252 = 1;
    } else {
        return 0;
    }
    const uint8_t *state_body = 0;
    unsigned long state_len = 0;
    if (!find_field(sp_body, sp_len, 1, 2, &state_body, &state_len, 0)) return 0;

    const uint8_t *vol_body = 0;
    unsigned long vol_len = 0;
    if (!find_field(state_body, state_len, 11, 2, &vol_body, &vol_len, 0)) return 0;

    uint32_t pct_bits = 0;
    int has_pct = find_fixed32(vol_body, vol_len, 6, &pct_bits);
    uint64_t is_muted = 0;
    int has_mute = find_field(vol_body, vol_len, 3, 0, 0, 0, &is_muted);
    if (!has_pct && !has_mute) return 0;

    uint64_t src = 0;
    find_field(vol_body, vol_len, 7, 0, 0, 0, &src);

    int v = (has_mute && is_muted) ? 0 : (has_pct ? f32_bits_to_int(pct_bits) : 0);
    if (vol_out) *vol_out = v;
    if (src_out) *src_out = src;
    return 1;
}

/* Send silent volume_set directive (ESourceOther=1000, IsLedSilent=1) via Quasar iohub_clients (port 9910). */
static int send_quasar_volume(int io_fd, int vol_pct) {
    if (io_fd < 0) return -1;
    if (vol_pct < 0) vol_pct = 0;
    if (vol_pct > 100) vol_pct = 100;

    uint32_t fbits = int_to_f32_bits(vol_pct);

    uint8_t frame[60] = {
        53, 0x00, 0x00, 0x00, // [0..3] frame length LE (53 for field 88, 56 for field 252/253)
        0xc2, 0x05, 50,       // [4..6] QuasarMessage field 88 (or 252: 0xe2, 0x0f), len 50
        0xfa, 0x05, 47,       // IOControlCommand field 95 (external_directives), len 47
        0x0a, 45,             // ExternalDirectives field 1 (directive_list), len 45
        0x0a, 43,             // DirectiveList field 1 (directives), len 43
        0x0a, 13, 'c','l','i','e','n','t','_','a','c','t','i','o','n', // TDirective.Type
        0x12, 10, 'v','o','l','u','m','e','_','s','e','t',             // TDirective.Name
        0x20, 0x01,           // TDirective.IgnoreAnswer = 1
        0x40, 0x01,           // TDirective.IsLedSilent = 1
        0x7a, 10,             // TDirective.PayloadRaw, len 10
        0x0d,                 // TVolumeSetDirective field 1 (PercentLevel, wire 5)
        (uint8_t)(fbits & 0xff),
        (uint8_t)((fbits >> 8) & 0xff),
        (uint8_t)((fbits >> 16) & 0xff),
        (uint8_t)((fbits >> 24) & 0xff),
        0x10, 0x00,           // TVolumeSetDirective field 2 (Mode = 0 Default)
        0x18, 0xe8, 0x07,     // TVolumeSetDirective field 3 (Source = 1000 ESourceOther)
        0xe8, 0x0f, 0x58      // [57..59] SharedMessage field 253 (message_type = 88 io_control)
    };

    if (quasar_uses_f252 != 1) {
        frame[0] = 53;
        frame[4] = 0xc2;
        frame[5] = 0x05;
        if (sys_write(io_fd, frame, 57) != 57) return -1;
    }
    if (quasar_uses_f252 != 0) {
        frame[0] = 56;
        frame[4] = 0xe2;
        frame[5] = 0x0f;
        if (sys_write(io_fd, frame, 60) != 60) return -1;
    }
    return 0;
}

/* Send masked WebSocket set_volume JSON frame to Soloist WS (port 9090). */
static int send_soloist_ws_volume(int ws_fd, int vol_pct) {
    if (ws_fd < 0) return -1;
    if (vol_pct < 0) vol_pct = 0;
    if (vol_pct > 100) vol_pct = 100;

    char json[64];
    const char *hdr = "{\"type\":\"command\",\"command\":\"set_volume\",\"volume\":";
    unsigned long pos = 0;
    while (hdr[pos]) { json[pos] = hdr[pos]; pos++; }
    pos += format_uint_ascii(json + pos, vol_pct);
    json[pos++] = '}';

    uint8_t frame[72];
    frame[0] = 0x81; // FIN + text frame
    frame[1] = (uint8_t)(0x80 | pos); // MASK + payload length (<126)
    static const uint8_t mask[4] = { 0x12, 0x34, 0x56, 0x78 };
    for (int i = 0; i < 4; i++) frame[2 + i] = mask[i];
    for (unsigned long i = 0; i < pos; i++) frame[6 + i] = (uint8_t)json[i] ^ mask[i & 3];

    long n = sys_write(ws_fd, frame, 6 + pos);
    return (n == (long)(6 + pos)) ? 0 : -1;
}

static int quasar_vol = -1;
static int quasar_vol_known = 0;
static int last_quasar_reported_vol = -1;
static int current_soloist_vol = -1;
static int ws_initial_vol_synced = 0;
static int need_soloist_vol_sync = 0;
static int pending_quasar_vol = -1;
static int64_t pending_quasar_vol_due_ms = 0;
static int last_sent_to_quasar_vol = -1;
static int64_t last_sent_to_quasar_ms = 0;
static int last_sent_to_soloist_vol = -1;
static int64_t last_sent_to_soloist_ms = 0;
static int64_t last_user_ws_vol_ms = 0;
static int vol_verify_target = -1;
static int vol_verify_prev_qvol = -1;
static int64_t vol_verify_sent_ms = 0;
static int quasar_vol_fallback_amixer = 0;

static int abs_diff_int(int a, int b) {
    return a >= b ? (a - b) : (b - a);
}

static int check_quasar_vol_fallback(int64_t now, int *target_out) {
    if (vol_verify_target < 0 || vol_verify_sent_ms <= 0) return 0;
    if (now - vol_verify_sent_ms < 900) return 0;
    int target = vol_verify_target;
    vol_verify_target = -1;
    vol_verify_sent_ms = 0;
    quasar_vol_fallback_amixer = 1;
    if (target_out) *target_out = target;
    return 1;
}

static void on_quasar_volume_state(int qvol, uint64_t src, int64_t now) {
    if (qvol < 0) qvol = 0;
    if (qvol > 100) qvol = 100;

    if (!quasar_vol_known) {
        quasar_vol_known = 1;
        quasar_vol = qvol;
        last_quasar_reported_vol = qvol;
        if (vol_verify_target >= 0 && abs_diff_int(qvol, vol_verify_target) <= 6) {
            vol_verify_target = -1;
            vol_verify_sent_ms = 0;
            quasar_vol_fallback_amixer = 0;
        }
        if (last_user_ws_vol_ms == 0 && (current_soloist_vol != qvol || !ws_initial_vol_synced)) {
            need_soloist_vol_sync = 1;
        }
        return;
    }

    quasar_vol = qvol;
    if (vol_verify_target >= 0 &&
        (abs_diff_int(qvol, vol_verify_target) <= 6 ||
         (vol_verify_prev_qvol >= 0 && qvol != vol_verify_prev_qvol))) {
        vol_verify_target = -1;
        vol_verify_sent_ms = 0;
        quasar_vol_fallback_amixer = 0;
    }
    if (qvol == last_quasar_reported_vol) {
        return;
    }
    last_quasar_reported_vol = qvol;

    int is_our_echo = (src == 1000 &&
                       last_sent_to_quasar_vol >= 0 &&
                       last_sent_to_quasar_ms > 0 &&
                       (now - last_sent_to_quasar_ms) < 1200 &&
                       abs_diff_int(qvol, last_sent_to_quasar_vol) <= 6);
    if (!is_our_echo) {
        if (pending_quasar_vol >= 0 && now >= pending_quasar_vol_due_ms - 60) {
            pending_quasar_vol = -1;
        }
        if (qvol != current_soloist_vol) {
            need_soloist_vol_sync = 1;
        }
    }
}

#endif
