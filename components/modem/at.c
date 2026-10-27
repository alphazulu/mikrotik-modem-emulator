#include "modem.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static bool reply(char *out, size_t cap, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int n = vsnprintf(out, cap, fmt, ap); va_end(ap);
    if (n < 0 || (size_t)n >= cap) { if (cap) out[0] = 0; return false; } return true;
}

/* One bounded CSV parser; quoted separators are data, not further commands. */
static bool csv(const char *s, char f[4][96], size_t *count) {
    *count = 0;
    while (*count < 4) {
        size_t used = 0; bool quoted = *s == '"';
        if (quoted) ++s;
        while (*s) {
            unsigned char c = (unsigned char)*s++;
            if (quoted && c == '"') {
                if (*s == '"') { ++s; c = '"'; }
                else { quoted = false; break; }
            } else if (!quoted && c == ',') { --s; break; }
            if (c < 32 || c == 127 || used >= 95 || (!quoted && c == '"')) return false;
            f[*count][used++] = (char)c;
        }
        if (quoted) return false;
        f[*count][used] = 0; ++*count;
        if (!*s) return true;
        if (*s++ != ',' || !*s) return false;
    }
    return false;
}

bool emu_at(emu_modem *m, const char *cmd, char *out, size_t cap, emu_action *action) {
    if (!m || !cmd || !out || !action) return false;
    *action = EMU_NO_ACTION;
    if (cap < EMU_AT_RESPONSE_MAX) { if (cap) out[0] = 0; return false; }
    if (strlen(cmd) > EMU_AT_LINE_MAX) return reply(out, cap, "\r\nERROR\r\n");
    if (!strcmp(cmd, "AT") || !strcmp(cmd, "ATE0") || !strcmp(cmd, "ATE1")) {
        /* Echo deliberately stays off, including credential-bearing commands. */
        return reply(out, cap, "\r\nOK\r\n");
    }
    if (!strcmp(cmd, "ATI") || !strcmp(cmd, "AT+CGMM")) return reply(out, cap, "\r\nSDWAN ESP32-S3 USB modem emulator\r\nOK\r\n");
    if (!strcmp(cmd, "AT+CGMI")) return reply(out, cap, "\r\nSDWAN laboratory\r\nOK\r\n");
    if (!strcmp(cmd, "AT+CGMR")) return reply(out, cap, "\r\nesp32-rndis-at-dev1\r\nOK\r\n");
    if (!strcmp(cmd, "AT+CPIN?")) return reply(out, cap, "\r\n+CPIN: %s\r\nOK\r\n", m->sim_ready ? "READY" : "NOT INSERTED");
    if (!strcmp(cmd, "AT+CFUN?")) return reply(out, cap, "\r\n+CFUN: %u\r\nOK\r\n", m->radio_on ? 1u : 0u);
    if (!strcmp(cmd, "AT+CFUN=0") || !strcmp(cmd, "AT+CFUN=1")) {
        m->radio_on = cmd[8] == '1';
        if (!m->radio_on) m->wifi_ip = false;
        return reply(out, cap, "\r\nOK\r\n");
    }
    if (!strcmp(cmd, "AT+CEREG?") || !strcmp(cmd, "AT+CREG?")) {
        const char *name = !strcmp(cmd, "AT+CEREG?") ? "CEREG" : "CREG";
        unsigned state = !m->radio_on || !m->sim_ready ? 0 : m->wifi_ip ? 1 : 2;
        return reply(out, cap, "\r\n+%s: 0,%u\r\nOK\r\n", name, state);
    }
    if (!strcmp(cmd, "AT+CSQ")) return reply(out, cap, "\r\n+CSQ: %u,99\r\nOK\r\n", m->rssi);
    if (!strcmp(cmd, "AT+CGATT?")) return reply(out, cap, "\r\n+CGATT: %u\r\nOK\r\n", m->sim_ready && m->radio_on && m->wifi_ip ? 1u : 0u);
    if (!strcmp(cmd, "AT+CGACT?")) return reply(out, cap, "\r\n+CGACT: 1,%u\r\nOK\r\n", emu_session_ready(m) ? 1u : 0u);
    if (!strcmp(cmd, "AT+CGACT=0,1")) { m->requested = false; return reply(out, cap, "\r\nOK\r\n"); }
    if (!strcmp(cmd, "AT+CGACT=1,1")) {
        if (!m->radio_on || !m->sim_ready || !emu_set_profile(m, &m->staged)) return reply(out, cap, "\r\n+CME ERROR: 30\r\n");
        m->requested = true; *action = EMU_APPLY_WIFI; return reply(out, cap, "\r\nOK\r\n");
    }
    if (!strcmp(cmd, "AT+CGDCONT?")) return reply(out, cap, "\r\n+CGDCONT: 1,\"IP\",\"%s\"\r\nOK\r\n", m->staged.apn);
    if (!strcmp(cmd, "AT+CGAUTH?")) return reply(out, cap, "\r\n+CGAUTH: 1,%u,\"\",\"\"\r\nOK\r\n", m->staged.auth);
    if (!strncmp(cmd, "AT+CGDCONT=", 11) || !strncmp(cmd, "AT+CGAUTH=", 10)) {
        char f[4][96] = {{0}}; size_t n = 0;
        bool pdp = !strncmp(cmd, "AT+CGDCONT=", 11), valid = csv(cmd + (pdp ? 11 : 10), f, &n);
        emu_profile p = m->staged;
        if (pdp) {
            valid = valid && n == 3 && !strcmp(f[0], "1") && !strcmp(f[1], "IP") && strlen(f[2]) <= EMU_APN_MAX;
            if (valid) for (size_t i = 0; f[2][i]; ++i) {
                unsigned char c = (unsigned char)f[2][i];
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '.' || c == '-')) valid = false;
            }
            if (valid) { memset(p.apn, 0, sizeof(p.apn)); memcpy(p.apn, f[2], strlen(f[2])); }
        } else {
            valid = valid && n == 4 && !strcmp(f[0], "1") && strlen(f[1]) == 1 &&
                    f[1][0] >= '0' && f[1][0] <= '2' && strlen(f[2]) <= EMU_SSID_MAX && strlen(f[3]) <= EMU_KEY_MAX;
            if (valid) {
                p.auth = (unsigned)(f[1][0] - '0');
                memset(p.user, 0, sizeof(p.user)); memset(p.password, 0, sizeof(p.password));
                memcpy(p.user, f[2], strlen(f[2])); memcpy(p.password, f[3], strlen(f[3]));
            }
        }
        if (valid) m->staged = p;
        emu_wipe(&p, sizeof(p)); emu_wipe(f, sizeof(f));
        return reply(out, cap, valid ? "\r\nOK\r\n" : "\r\nERROR\r\n");
    }
    if (!strcmp(cmd, "AT&W")) {
        if (!m->configured) return reply(out, cap, "\r\nERROR\r\n");
        *action = EMU_SAVE; return reply(out, cap, "\r\nOK\r\n");
    }
    if (!strcmp(cmd, "AT+EMUFORGET")) {
        uint32_t next=m->generation==UINT32_MAX?UINT32_MAX:m->generation+1; emu_init(m); m->generation=next;
        *action = EMU_FORGET; return reply(out, cap, "\r\nOK\r\n");
    }
    if (!strcmp(cmd, "AT+EMUGATE=0") || !strcmp(cmd, "AT+EMUGATE=1")) { m->blackhole = cmd[11] == '1'; return reply(out, cap, "\r\nOK\r\n"); }
    if (!strcmp(cmd, "AT+EMUSTATUS?")) return reply(out, cap, "\r\n+EMUSTATUS: configured=%u,wifi_ip=%u,data=%u,generation=%lu\r\nOK\r\n", m->configured ? 1u : 0u, m->wifi_ip ? 1u : 0u, emu_data_ready(m) ? 1u : 0u, (unsigned long)m->generation);
    return reply(out, cap, "\r\nERROR\r\n");
}
