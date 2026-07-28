#include "app_comm.h"

#include "FreeRTOS.h"
#include "app_observe.h"
#include "app_protocol.h"
#include "board.h"
#include "queue.h"
#include "task.h"

#include <string.h>

typedef struct {
    uint8_t msg_type;
    uint32_t tick;
    uint16_t payload_len;
    uint8_t payload[APP_PROTOCOL_MAX_PAYLOAD];
} CommMessage;

static QueueHandle_t s_comm_queue;
static volatile uint8_t s_binary_enabled;

static void put_u16_le(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)value;
    buf[1] = (uint8_t)(value >> 8);
}

static void put_i16_le(uint8_t *buf, int16_t value)
{
    put_u16_le(buf, (uint16_t)value);
}

static uint8_t build_payload_from_sensor(const AppMessage *msg, CommMessage *out)
{
    if (msg == 0 || out == 0) {
        return 0U;
    }

    memset(out, 0, sizeof(*out));
    out->tick = msg->tick;

    if (msg->type == APP_MSG_DHT11) {
        out->msg_type = APP_PROTO_MSG_DHT11;
        out->payload[0] = msg->valid;
        out->payload[1] = msg->data.dht11.temperature;
        out->payload[2] = msg->data.dht11.humidity;
        out->payload_len = 3U;
        return 1U;
    }

    if (msg->type == APP_MSG_MPU6050) {
        out->msg_type = APP_PROTO_MSG_MPU6050;
        out->payload[0] = msg->valid;
        out->payload[1] = msg->data.mpu6050.addr;
        out->payload[2] = msg->data.mpu6050.whoami;
        put_i16_le(&out->payload[3], msg->data.mpu6050.ax_mg);
        put_i16_le(&out->payload[5], msg->data.mpu6050.ay_mg);
        put_i16_le(&out->payload[7], msg->data.mpu6050.az_mg);
        put_i16_le(&out->payload[9], msg->data.mpu6050.gx_dps);
        put_i16_le(&out->payload[11], msg->data.mpu6050.gy_dps);
        put_i16_le(&out->payload[13], msg->data.mpu6050.gz_dps);
        out->payload_len = 15U;
        return 1U;
    }

    if (msg->type == APP_MSG_VL53L0X) {
        out->msg_type = APP_PROTO_MSG_VL53L0X;
        out->payload[0] = msg->valid;
        out->payload[1] = msg->data.vl53l0x.addr;
        out->payload[2] = msg->data.vl53l0x.model_id;
        out->payload[3] = msg->data.vl53l0x.range_valid;
        put_u16_le(&out->payload[4], msg->data.vl53l0x.distance_mm);
        out->payload_len = 6U;
        return 1U;
    }

    if (msg->type == APP_MSG_POT) {
        out->msg_type = APP_PROTO_MSG_POT;
        out->payload[0] = msg->valid;
        put_u16_le(&out->payload[1], msg->data.pot.raw);
        out->payload[3] = msg->data.pot.percent;
        out->payload_len = 4U;
        return 1U;
    }

    return 0U;
}

static void TaskCommTx(void *argument)
{
    (void)argument;
    CommMessage msg;
    uint8_t frame[APP_PROTOCOL_MAX_FRAME];
    uint16_t seq = 0U;

    AppObserve_WriteLine("CommTxTask start: binary frames disabled by default, send 'b' to toggle\r\n");

    for (;;) {
        uint16_t frame_len;

        if (xQueueReceive(s_comm_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        frame_len = AppProtocol_BuildFrame(msg.msg_type,
                                           seq++,
                                           msg.tick,
                                           msg.payload,
                                           msg.payload_len,
                                           frame,
                                           sizeof(frame));
        if (frame_len == 0U) {
            AppObserve_WriteLine("COMM build frame failed\r\n");
            continue;
        }

        AppObserve_WriteBytes(frame, frame_len);
    }
}

void AppComm_Start(void)
{
    TaskHandle_t comm_handle = NULL;

    s_comm_queue = xQueueCreate(16U, sizeof(CommMessage));
    if (s_comm_queue == NULL) {
        Board_Error_Handler();
    }

    if (xTaskCreate(TaskCommTx, "COMMTX", 512U, NULL,
                    tskIDLE_PRIORITY + 1U, &comm_handle) != pdPASS) {
        Board_Error_Handler();
    }

    AppObserve_RegisterTask("COMMTX", comm_handle);
}

void AppComm_SubmitSensorMessage(const AppMessage *msg)
{
    CommMessage comm;

    if (s_comm_queue == NULL) {
        return;
    }
    if (!s_binary_enabled) {
        return;
    }
    if (!build_payload_from_sensor(msg, &comm)) {
        return;
    }

    if (xQueueSend(s_comm_queue, &comm, 0U) != pdTRUE) {
        AppObserve_WriteLine("COMM queue full\r\n");
    }
}

void AppComm_SetBinaryEnabled(uint8_t enabled)
{
    s_binary_enabled = enabled ? 1U : 0U;
}

uint8_t AppComm_IsBinaryEnabled(void)
{
    return s_binary_enabled ? 1U : 0U;
}

uint8_t AppComm_ToggleBinaryEnabled(void)
{
    s_binary_enabled = s_binary_enabled ? 0U : 1U;
    return AppComm_IsBinaryEnabled();
}
