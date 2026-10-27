#ifndef SDWAN_EMU_MODEM_H
#define SDWAN_EMU_MODEM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EMU_AT_LINE_MAX 320
#define EMU_AT_RESPONSE_MAX 512
#define EMU_APN_MAX 63
#define EMU_SSID_MAX 32
#define EMU_KEY_MAX 64

typedef struct {
    char apn[EMU_APN_MAX + 1];
    char user[EMU_SSID_MAX + 1];
    char password[EMU_KEY_MAX + 1];
    unsigned auth; /* CGAUTH: 0 none, 1 PAP, 2 CHAP. Transport mapping, not WPA auth. */
} emu_profile;

typedef struct {
    uint8_t ssid[EMU_SSID_MAX];
    uint8_t ssid_len;
    char password[EMU_KEY_MAX + 1];
} emu_wifi;

typedef struct {
    emu_profile staged;
    emu_profile active;
    emu_wifi wifi;
    uint32_t generation;
    bool configured;
    bool radio_on;
    bool sim_ready;
    bool wifi_ip;
    bool requested;
    bool blackhole;
    unsigned rssi; /* Synthetic CSQ 0..31 or 99; never Wi-Fi RSSI. */
} emu_modem;

typedef enum { EMU_NO_ACTION, EMU_APPLY_WIFI, EMU_SAVE, EMU_FORGET } emu_action;
typedef struct { char line[EMU_AT_LINE_MAX + 1]; size_t used; bool overflow; } emu_line;

void emu_init(emu_modem *m);
void emu_wipe(void *p, size_t size);
bool emu_profile_wifi(const emu_profile *p, emu_wifi *out);
bool emu_set_profile(emu_modem *m, const emu_profile *p);
bool emu_session_ready(const emu_modem *m);
bool emu_data_ready(const emu_modem *m);
typedef struct { bool pending; uint32_t generation,started_ms; } emu_activation;
typedef enum { EMU_ACT_WAIT, EMU_ACT_OK, EMU_ACT_ERROR } emu_activation_result;
void emu_activation_begin(emu_activation *a,const emu_modem *m,uint32_t now_ms);
emu_activation_result emu_activation_poll(emu_activation *a,emu_modem *m,uint32_t now_ms);
/* Complete lines only, no echo or secret-bearing error text. */
bool emu_at(emu_modem *m, const char *command, char *out, size_t cap, emu_action *action);
/* Returns true exactly once per nonempty line/overflow; strips CR/LF framing. */
bool emu_line_byte(emu_line *line, uint8_t byte, char *command, size_t cap);
/* Always permit only local ARP/DHCP services when Internet gate is closed. */
bool emu_frame_allowed(const emu_modem *m, const uint8_t *frame, size_t len, bool from_usb);
#endif
