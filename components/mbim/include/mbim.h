#ifndef EMU_MBIM_H
#define EMU_MBIM_H
#include "modem.h"
#define EMU_MBIM_CONTROL_MAX 1024
#define EMU_MBIM_NTB_MAX 2048
#define EMU_MBIM_SCAN_MAX 8
typedef struct { char ssid[33]; int16_t rssi; uint8_t channel,bssid[6]; } emu_mbim_ap;
typedef struct {
    bool opened, attached, connected, notify_ip, last_radio, last_sim, initial_sync, device_caps_seen;
    uint32_t subscription, pending_notifications, pending_tid, signal_interval, signal_threshold, error_threshold;
    bool scan_pending,scan_started,scan_ready,scan_ok,scan_cache_valid;
    uint32_t scan_tid,scan_start,scan_token,scan_kind,scan_lte_limit,scan_completed_ms;
    size_t scan_count;
    emu_mbim_ap scan_aps[EMU_MBIM_SCAN_MAX];
    bool current_valid;
    emu_mbim_ap current_ap;
    emu_activation activation;
    uint64_t rx_bytes,tx_bytes,rx_packets,tx_packets;
} emu_mbim;
extern const uint8_t emu_mbim_basic_uuid[16],emu_mbim_internet_uuid[16],emu_mbim_ms_uuid[16],emu_mbim_atds_uuid[16];
void emu_mbim_init(emu_mbim *s);
/* Caller serializes access with modem state. Output buffer >= CONTROL_MAX.
   Zero means malformed input or a deferred Connect response (activation.pending).
   No credential is ever returned by a query/response. */
size_t emu_mbim_request(emu_mbim *s,emu_modem *m,const uint8_t *request,size_t length,
                        uint8_t *response,size_t capacity,uint32_t now_ms);
size_t emu_mbim_poll(emu_mbim *s,emu_modem *m,uint8_t *response,size_t capacity,uint32_t now_ms);
/* Async scan completion is generation fenced across USB reset/reopen. */
bool emu_mbim_scan_take(emu_mbim *s,uint32_t token);
void emu_mbim_scan_complete(emu_mbim *s,uint32_t token,const emu_mbim_ap *aps,size_t count,bool ok);
/* Wi-Fi measurements map to explicitly virtual LTE cell identities/metrics. */
void emu_mbim_current_ap(emu_mbim *s,const emu_mbim_ap *ap);
/* Strict bounded NTB16, IPS session 0 only; callback receives raw IPv4. */
typedef bool (*emu_mbim_ip_receiver)(const uint8_t *ip,size_t length,void *context);
bool emu_mbim_ntb_receive(const uint8_t *ntb,size_t length,emu_mbim_ip_receiver receive,void *context);
size_t emu_mbim_ntb_send(uint8_t *ntb,size_t capacity,const uint8_t *ip,size_t length,uint16_t sequence);
#endif
