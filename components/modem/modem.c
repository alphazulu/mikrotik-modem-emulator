#include "modem.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

void emu_wipe(void *p, size_t n) { volatile uint8_t *v = p; while (n--) *v++ = 0; }

void emu_init(emu_modem *m) {
    memset(m, 0, sizeof(*m));
    m->radio_on = true; m->sim_ready = true; m->rssi = 24;
}

static bool terminated(const char *s, size_t max) {
    return memchr(s, 0, max + 1) != NULL;
}

static bool key_valid(const char *s, unsigned auth) {
    size_t n = strlen(s);
    if (auth == 0) return n == 0;
    if (auth > 2 || n < 8 || n > 64) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (n == 64 ? !isxdigit(c) : c < 32 || c > 126) return false;
    }
    return true;
}

static int base32(uint8_t c) {
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

bool emu_profile_wifi(const emu_profile *p, emu_wifi *out) {
    emu_wifi w = {0};
    if (!p || !out || !terminated(p->apn, EMU_APN_MAX) ||
        !terminated(p->user, EMU_SSID_MAX) || !terminated(p->password, EMU_KEY_MAX)) return false;
    if (!key_valid(p->password, p->auth)) return false;
    if (strcmp(p->apn, "wifi") == 0) {
        size_t n = strlen(p->user);
        if (!n || n > EMU_SSID_MAX) return false;
        for (size_t i = 0; i < n; ++i) if ((uint8_t)p->user[i] < 32 || (uint8_t)p->user[i] == 127) return false;
        memcpy(w.ssid, p->user, n); w.ssid_len = (uint8_t)n;
    } else if (strncmp(p->apn, "wifi.", 5) == 0 && p->user[0] == 0) {
        /* Canonical unpadded Base32. APN labels stay <=63 chars for a 32-byte SSID. */
        const char *s = p->apn + 5;
        size_t n = strlen(s); unsigned bits = 0; uint32_t acc = 0;
        if (!n || n > 52 || n % 8 == 1 || n % 8 == 3 || n % 8 == 6) return false;
        for (size_t i = 0; i < n; ++i) {
            int v = base32((uint8_t)s[i]); if (v < 0) return false;
            acc = (acc << 5) | (unsigned)v; bits += 5;
            if (bits >= 8) {
                bits -= 8;
                if (w.ssid_len == EMU_SSID_MAX) return false;
                w.ssid[w.ssid_len++] = (uint8_t)(acc >> bits);
                acc &= (1u << bits) - 1u;
            }
        }
        if (!w.ssid_len || acc != 0 || memchr(w.ssid,0,w.ssid_len)) return false;
    } else return false;
    memcpy(w.password, p->password, strlen(p->password) + 1);
    *out = w; emu_wipe(&w, sizeof(w)); return true;
}

bool emu_set_profile(emu_modem *m, const emu_profile *p) {
    emu_wifi w;
    if (!m) return false;
    if (!emu_profile_wifi(p, &w)) return false;
    bool changed = !m->configured || strcmp(m->active.apn, p->apn) ||
                   strcmp(m->active.user, p->user) || strcmp(m->active.password, p->password) || m->active.auth != p->auth;
    if (changed) {
        if (m->generation==UINT32_MAX) {emu_wipe(&w,sizeof(w));return false;}
        emu_wipe(&m->active, sizeof(m->active)); emu_wipe(&m->wifi, sizeof(m->wifi));
        m->active = *p; m->wifi = w; m->generation++; m->wifi_ip = false;
    }
    m->configured = true; emu_wipe(&w, sizeof(w)); return true;
}

bool emu_session_ready(const emu_modem *m) {
    return m->configured && m->radio_on && m->sim_ready && m->wifi_ip && m->requested;
}
bool emu_data_ready(const emu_modem *m) { return emu_session_ready(m) && !m->blackhole; }

void emu_activation_begin(emu_activation *a,const emu_modem *m,uint32_t now_ms) {
    a->pending=true; a->generation=m->generation; a->started_ms=now_ms;
}
emu_activation_result emu_activation_poll(emu_activation *a,emu_modem *m,uint32_t now_ms) {
    if (!a->pending) return EMU_ACT_WAIT;
    if (a->generation!=m->generation) {a->pending=false;return EMU_ACT_ERROR;}
    if (!m->radio_on || !m->sim_ready || !m->requested || (uint32_t)(now_ms-a->started_ms)>=30000u) {
        m->requested=false;a->pending=false;return EMU_ACT_ERROR;
    }
    if (emu_session_ready(m)) {a->pending=false;return EMU_ACT_OK;}
    return EMU_ACT_WAIT;
}

bool emu_line_byte(emu_line *l, uint8_t byte, char *out, size_t cap) {
    if (byte == '\r' || byte == '\n') {
        if (!l->used && !l->overflow) return false;
        if (l->overflow || l->used + 1 > cap) {
            if (cap) out[0] = 0;
        } else { memcpy(out, l->line, l->used); out[l->used] = 0; }
        emu_wipe(l, sizeof(*l)); return true;
    }
    if (!byte || byte == 127 || (byte < 32 && byte != '\t')) l->overflow = true;
    if (!l->overflow) {
        if (l->used == EMU_AT_LINE_MAX) l->overflow = true;
        else l->line[l->used++] = (char)byte;
    }
    return false;
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static bool local_ip(const uint8_t *p) { return p[0] == 192 && p[1] == 0 && p[2] == 2 && p[3] == 1; }

bool emu_frame_allowed(const emu_modem *m, const uint8_t *f, size_t n, bool from_usb) {
    if (!f || n < 14 || n > 1514) return false;
    uint16_t type = be16(f + 12);
    /* IPv6 and VLAN are outside the first profile, even when online. */
    if (type != 0x0800 && type != 0x0806) return false;
    if (type == 0x0806) return n >= 42 && be16(f + 14) == 1 && be16(f + 16) == 0x0800 && f[18] == 6 && f[19] == 4;
    if (n < 34 || f[14] >> 4 != 4 || (f[14] & 15) < 5) return false;
    size_t ihl = (size_t)(f[14] & 15) * 4, total = be16(f + 16);
    if (total < ihl || total > n - 14 || n < 14 + ihl) return false;
    if (emu_data_ready(m)) return true;
    if (local_ip(f + (from_usb ? 30 : 26))) return true;
    /* Closed gate still allows DHCP broadcasts, not arbitrary UDP port spoofing. */
    if (f[23] != 17 || (be16(f + 20) & 0x3fff) || total < ihl + 8) return false;
    const uint8_t *u = f + 14 + ihl;
    return from_usb ? be16(u) == 68 && be16(u + 2) == 67 &&
                     f[30] == 255 && f[31] == 255 && f[32] == 255 && f[33] == 255 :
                     be16(u) == 67 && be16(u + 2) == 68 && local_ip(f + 26);
}
