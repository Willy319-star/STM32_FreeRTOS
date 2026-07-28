#include "app_protocol.h"

#include <string.h>

static void put_u16_le(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)value;
    buf[1] = (uint8_t)(value >> 8);
}

static void put_u32_le(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)value;
    buf[1] = (uint8_t)(value >> 8);
    buf[2] = (uint8_t)(value >> 16);
    buf[3] = (uint8_t)(value >> 24);
}

uint16_t AppProtocol_Crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;

    if (data == 0 && len != 0U) {
        return 0U;
    }

    for (uint16_t i = 0U; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

uint16_t AppProtocol_BuildFrame(uint8_t msg_type,
                                uint16_t seq,
                                uint32_t timestamp,
                                const uint8_t *payload,
                                uint16_t payload_len,
                                uint8_t *out,
                                uint16_t out_size)
{
    uint16_t frame_len;
    uint16_t crc;

    if (out == 0) {
        return 0U;
    }
    if (payload_len > APP_PROTOCOL_MAX_PAYLOAD) {
        return 0U;
    }
    if (payload_len != 0U && payload == 0) {
        return 0U;
    }

    frame_len = (uint16_t)(APP_PROTOCOL_HEADER_SIZE + payload_len + APP_PROTOCOL_CRC_SIZE);
    if (out_size < frame_len) {
        return 0U;
    }

    out[0] = APP_PROTOCOL_SOF0;
    out[1] = APP_PROTOCOL_SOF1;
    out[2] = APP_PROTOCOL_VERSION;
    out[3] = msg_type;
    put_u16_le(&out[4], seq);
    put_u16_le(&out[6], payload_len);
    put_u32_le(&out[8], timestamp);

    if (payload_len != 0U) {
        memcpy(&out[APP_PROTOCOL_HEADER_SIZE], payload, payload_len);
    }

    crc = AppProtocol_Crc16(out, (uint16_t)(APP_PROTOCOL_HEADER_SIZE + payload_len));
    put_u16_le(&out[APP_PROTOCOL_HEADER_SIZE + payload_len], crc);

    return frame_len;
}
