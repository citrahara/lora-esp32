/**
 * xoodyak_wrapper.h
 * ─────────────────────────────────────────────────────────────────────
 * Wrapper tipis di atas library Xoodyak kalian yang sudah ada.
 * Semua 4 sketch security testing #include file ini.
 *
 * ASUMSI: Library Xoodyak kalian sudah ada dan namanya persis seperti
 * yang di-include di bawah. Sesuaikan nama #include jika berbeda.
 * ─────────────────────────────────────────────────────────────────────
 */

#ifndef XOODYAK_WRAPPER_H
#define XOODYAK_WRAPPER_H

// ── Sesuaikan baris ini dengan nama library Xoodyak kalian ──────────
#include <Xoodyak.h>   // ganti jika nama file library berbeda
// ────────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <string.h>

// ── Kunci & ukuran tetap ─────────────────────────────────────────────
// PENTING: kunci ini harus SAMA persis di TX, RX, dan semua sketch.
//          128 bit = 16 byte
static const uint8_t XOODYAK_KEY[16] = {
  0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
  0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
};

#define KEY_LEN     16   // bytes
#define NONCE_LEN   16   // bytes
#define TAG_LEN     16   // bytes (authentication tag)
#define MAX_PT_LEN  64   // max plaintext/ciphertext payload bytes

// ── Struktur paket LoRa ──────────────────────────────────────────────
// Layout bytes yang dikirim lewat LoRa:
//   [ nonce (16B) | ciphertext (var) | tag (16B) ]
//
// Total overhead per paket: NONCE_LEN + TAG_LEN = 32 byte
// ─────────────────────────────────────────────────────────────────────

/**
 * xoodyak_encrypt()
 * Enkripsi plaintext → ciphertext + hasilkan authentication tag.
 *
 * @param key        Kunci 16 byte
 * @param nonce      Nonce 16 byte (harus unik tiap paket)
 * @param plaintext  Data asli
 * @param pt_len     Panjang plaintext (byte)
 * @param ciphertext Buffer output ciphertext (minimal pt_len byte)
 * @param tag        Buffer output tag (minimal TAG_LEN byte)
 */
inline void xoodyak_encrypt(
    const uint8_t* key,
    const uint8_t* nonce,
    const uint8_t* plaintext,  size_t pt_len,
    uint8_t*       ciphertext,
    uint8_t*       tag)
{
    Xoodyak xobj;
    xobj.initialize(key, KEY_LEN, nullptr, 0);
    xobj.absorb(nonce, NONCE_LEN);
    xobj.encrypt(ciphertext, plaintext, pt_len);
    xobj.squeeze(tag, TAG_LEN);
}

/**
 * xoodyak_decrypt()
 * Dekripsi ciphertext → plaintext + verifikasi tag.
 *
 * @return true  jika tag valid (data autentik & tidak diubah)
 *         false jika tag TIDAK cocok (data dimanipulasi / palsu)
 */
inline bool xoodyak_decrypt(
    const uint8_t* key,
    const uint8_t* nonce,
    const uint8_t* ciphertext, size_t ct_len,
    const uint8_t* tag_received,
    uint8_t*       plaintext)
{
    Xoodyak xobj;
    xobj.initialize(key, KEY_LEN, nullptr, 0);
    xobj.absorb(nonce, NONCE_LEN);
    xobj.decrypt(plaintext, ciphertext, ct_len);

    uint8_t tag_computed[TAG_LEN];
    xobj.squeeze(tag_computed, TAG_LEN);

    // Bandingkan tag byte per byte (constant-time compare)
    uint8_t diff = 0;
    for (int i = 0; i < TAG_LEN; i++) {
        diff |= (tag_received[i] ^ tag_computed[i]);
    }
    return (diff == 0);  // true = AMAN, false = TAMPERED/INVALID
}

/**
 * print_hex()
 * Cetak array byte sebagai hex ke Serial — berguna untuk screenshot.
 */
inline void print_hex(const char* label, const uint8_t* buf, size_t len) {
    Serial.print(label);
    Serial.print(": ");
    for (size_t i = 0; i < len; i++) {
        if (buf[i] < 0x10) Serial.print("0");
        Serial.print(buf[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
}

/**
 * build_lora_packet()
 * Gabungkan nonce + ciphertext + tag jadi 1 buffer siap kirim LoRa.
 *
 * @param out_packet  Buffer output (minimal NONCE_LEN + ct_len + TAG_LEN)
 * @return panjang total paket (byte)
 */
inline size_t build_lora_packet(
    uint8_t*       out_packet,
    const uint8_t* nonce,
    const uint8_t* ciphertext, size_t ct_len,
    const uint8_t* tag)
{
    size_t pos = 0;
    memcpy(out_packet + pos, nonce,      NONCE_LEN); pos += NONCE_LEN;
    memcpy(out_packet + pos, ciphertext, ct_len);     pos += ct_len;
    memcpy(out_packet + pos, tag,        TAG_LEN);    pos += TAG_LEN;
    return pos;
}

/**
 * parse_lora_packet()
 * Pecah paket LoRa → nonce, ciphertext, tag.
 *
 * @param ct_len  [out] panjang ciphertext yang diekstrak
 * @return true jika ukuran paket valid
 */
inline bool parse_lora_packet(
    const uint8_t* packet,   size_t pkt_len,
    uint8_t*       nonce,
    uint8_t*       ciphertext, size_t* ct_len,
    uint8_t*       tag)
{
    if (pkt_len <= NONCE_LEN + TAG_LEN) return false;
    *ct_len = pkt_len - NONCE_LEN - TAG_LEN;

    size_t pos = 0;
    memcpy(nonce,      packet + pos, NONCE_LEN); pos += NONCE_LEN;
    memcpy(ciphertext, packet + pos, *ct_len);   pos += *ct_len;
    memcpy(tag,        packet + pos, TAG_LEN);
    return true;
}

#endif // XOODYAK_WRAPPER_H
