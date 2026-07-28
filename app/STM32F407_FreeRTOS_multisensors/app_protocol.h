#pragma once

#include <stdint.h>

#define APP_PROTOCOL_SOF0          0xAAU
#define APP_PROTOCOL_SOF1          0x55U
#define APP_PROTOCOL_VERSION       0x01U
#define APP_PROTOCOL_HEADER_SIZE   12U
#define APP_PROTOCOL_CRC_SIZE      2U
#define APP_PROTOCOL_MAX_PAYLOAD   64U
#define APP_PROTOCOL_MAX_FRAME     (APP_PROTOCOL_HEADER_SIZE + APP_PROTOCOL_MAX_PAYLOAD + APP_PROTOCOL_CRC_SIZE)

typedef enum {
    APP_PROTO_MSG_DHT11 = 0x01,
    APP_PROTO_MSG_MPU6050 = 0x02,
    APP_PROTO_MSG_VL53L0X = 0x03,
    APP_PROTO_MSG_POT = 0x04,
    APP_PROTO_MSG_HEARTBEAT = 0x10,
} AppProtoMsgType;

uint16_t AppProtocol_Crc16(const uint8_t *data, uint16_t len);
uint16_t AppProtocol_BuildFrame(uint8_t msg_type,
                                uint16_t seq,
                                uint32_t timestamp,
                                const uint8_t *payload,
                                uint16_t payload_len,
                                uint8_t *out,
                                uint16_t out_size);
