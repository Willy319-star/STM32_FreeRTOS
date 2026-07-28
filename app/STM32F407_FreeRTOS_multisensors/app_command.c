#include "app_command.h"

#include "FreeRTOS.h"
#include "app_comm.h"
#include "app_control.h"
#include "app_health.h"
#include "app_observe.h"
#include "app_snapshot.h"
#include "board.h"
#include "bsp_debug_uart.h"
#include "queue.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

static QueueHandle_t s_cmd_rx_queue;

#define CMD_LINE_MAX_LEN  48U
#define CMD_IDLE_TIMEOUT  pdMS_TO_TICKS(50)

static void print_help(void)
{
    AppObserve_WriteLine("\r\nCMD help:\r\n");
    AppObserve_WriteLine("  ? / H / HELP       : show help\r\n");
    AppObserve_WriteLine("  S / STATUS         : print system status\r\n");
    AppObserve_WriteLine("  SELFTEST / TEST    : check snapshot and modules\r\n");
    AppObserve_WriteLine("  A / ALERT          : print alert bits\r\n");
    AppObserve_WriteLine("  CONFIG             : print alert thresholds\r\n");
    AppObserve_WriteLine("  SAVE CONFIG        : save thresholds to Flash\r\n");
    AppObserve_WriteLine("  LOAD CONFIG        : reload thresholds from Flash\r\n");
    AppObserve_WriteLine("  DEFAULT CONFIG     : restore RAM defaults\r\n");
    AppObserve_WriteLine("  SET NEAR <MM>      : set near-distance alert\r\n");
    AppObserve_WriteLine("  SET HOT/DRY/POT/MOTION <N>\r\n");
    AppObserve_WriteLine("  B / BIN            : toggle binary protocol frames\r\n");
    AppObserve_WriteLine("  BIN ON / BIN OFF   : set binary frames explicitly\r\n");
    AppObserve_WriteLine("  FAULT <TASK>       : inject health fault, e.g. FAULT MPU\r\n");
    AppObserve_WriteLine("  FAULT CLEAR        : clear injected health faults before reset\r\n");
    AppObserve_WriteLine("  HEALTH STATUS      : print watchdog health state\r\n");
    AppObserve_WriteLine("  LOG ERROR/WARN/INFO/DEBUG\r\n");
    AppObserve_WriteLine("XCOM tip: keep binary off for readable text logs\r\n");
}

static char to_upper_ascii(char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - 'a' + 'A');
    }

    return ch;
}

static void trim_and_uppercase(char *cmd)
{
    uint8_t start = 0U;
    uint8_t end;
    uint8_t out = 0U;

    while (cmd[start] == ' ') {
        start++;
    }

    end = (uint8_t)strlen(&cmd[start]);
    while (end > 0U && cmd[(uint8_t)(start + end - 1U)] == ' ') {
        end--;
    }

    for (uint8_t i = 0U; i < end; i++) {
        cmd[out++] = to_upper_ascii(cmd[(uint8_t)(start + i)]);
    }
    cmd[out] = '\0';
}

static void print_status(void)
{
    AppSystemSnapshot snapshot;
    char line[96];

    snprintf(line, sizeof(line),
             "CMD STATUS tick=%lu heap=%lu min_heap=%lu binary=%s\r\n",
             (unsigned long)xTaskGetTickCount(),
             (unsigned long)xPortGetFreeHeapSize(),
             (unsigned long)xPortGetMinimumEverFreeHeapSize(),
             AppComm_IsBinaryEnabled() ? "ON" : "OFF");
    AppObserve_WriteLine(line);
    snprintf(line, sizeof(line),
             "CMD STATUS log=%s\r\n",
             AppObserve_LogLevelName(AppObserve_GetLogLevel()));
    AppObserve_WriteLine(line);

    if (AppSnapshot_Get(&snapshot)) {
        snprintf(line, sizeof(line),
                 "CMD SNAP DHT=%u temp=%uC hum=%u%% stale=%u\r\n",
                 (unsigned int)snapshot.dht_valid,
                 (unsigned int)snapshot.temperature,
                 (unsigned int)snapshot.humidity,
                 (unsigned int)snapshot.dht_stale);
        AppObserve_WriteLine(line);

        snprintf(line, sizeof(line),
                 "CMD SNAP VL53=%u range=%u dist=%umm POT=%u%% raw=%u\r\n",
                 (unsigned int)snapshot.vl53_valid,
                 (unsigned int)snapshot.vl53_range_valid,
                 (unsigned int)snapshot.distance_mm,
                 (unsigned int)snapshot.pot_percent,
                 (unsigned int)snapshot.pot_raw);
        AppObserve_WriteLine(line);

        snprintf(line, sizeof(line),
                 "CMD SNAP MPU=%u acc=%d,%d,%d motion=%u posture=%u alert=0x%04lX\r\n",
                 (unsigned int)snapshot.mpu_valid,
                 (int)snapshot.ax_mg,
                 (int)snapshot.ay_mg,
                 (int)snapshot.az_mg,
                 (unsigned int)snapshot.motion,
                 (unsigned int)snapshot.posture_changed,
                 (unsigned long)snapshot.alert_bits);
        AppObserve_WriteLine(line);
    }
}

static void print_alert(void)
{
    char line[64];

    snprintf(line, sizeof(line),
             "CMD ALERT bits=0x%04lX\r\n",
             (unsigned long)AppControl_GetAlertBits());
    AppObserve_WriteLine(line);
}

static void print_selftest_result(const char *name, uint8_t pass, const char *detail)
{
    char line[112];

    snprintf(line, sizeof(line),
             "SELFTEST %-5s %s %s\r\n",
             name,
             pass ? "PASS" : "FAIL",
             detail);
    AppObserve_WriteLine(line);
}

static void execute_selftest(void)
{
    AppSystemSnapshot snapshot;
    char detail[64];
    uint8_t pass_all = 1U;
    uint8_t pass;

    if (!AppSnapshot_Get(&snapshot)) {
        AppObserve_WriteLine("SELFTEST FAIL snapshot unavailable\r\n");
        return;
    }

    AppObserve_WriteLine("SELFTEST begin: using AppSystemSnapshot\r\n");

    snprintf(detail, sizeof(detail), "temp=%uC hum=%u%% stale=%u",
             (unsigned int)snapshot.temperature,
             (unsigned int)snapshot.humidity,
             (unsigned int)snapshot.dht_stale);
    pass = snapshot.dht_valid;
    pass_all = (uint8_t)(pass_all && pass);
    print_selftest_result("DHT", pass, detail);

    snprintf(detail, sizeof(detail), "acc=%d,%d,%d motion=%u",
             (int)snapshot.ax_mg,
             (int)snapshot.ay_mg,
             (int)snapshot.az_mg,
             (unsigned int)snapshot.motion);
    pass = snapshot.mpu_valid;
    pass_all = (uint8_t)(pass_all && pass);
    print_selftest_result("MPU", pass, detail);

    snprintf(detail, sizeof(detail), "range=%u dist=%umm",
             (unsigned int)snapshot.vl53_range_valid,
             (unsigned int)snapshot.distance_mm);
    pass = (uint8_t)(snapshot.vl53_valid && snapshot.vl53_range_valid);
    pass_all = (uint8_t)(pass_all && pass);
    print_selftest_result("VL53", pass, detail);

    snprintf(detail, sizeof(detail), "raw=%u percent=%u%%",
             (unsigned int)snapshot.pot_raw,
             (unsigned int)snapshot.pot_percent);
    pass = snapshot.pot_valid;
    pass_all = (uint8_t)(pass_all && pass);
    print_selftest_result("POT", pass, detail);

    snprintf(detail, sizeof(detail), "heap=%lu min=%lu",
             (unsigned long)snapshot.heap_free,
             (unsigned long)snapshot.heap_min);
    pass = (snapshot.heap_free >= 4096UL && snapshot.heap_min >= 4096UL) ? 1U : 0U;
    pass_all = (uint8_t)(pass_all && pass);
    print_selftest_result("HEAP", pass, detail);

    snprintf(detail, sizeof(detail), "bits=0x%04lX",
             (unsigned long)snapshot.alert_bits);
    print_selftest_result("ALERT", 1U, detail);

    AppObserve_WriteLine(pass_all ? "SELFTEST RESULT PASS\r\n" : "SELFTEST RESULT FAIL\r\n");
}

static void print_config(void)
{
    AppControlConfig config;
    char line[128];

    AppControl_GetConfig(&config);
    snprintf(line, sizeof(line),
             "CMD CONFIG hot=%uC dry=%u%% near=%umm pot=%u%% motion=%ddps\r\n",
             (unsigned int)config.temp_hot_c,
             (unsigned int)config.humidity_dry_pct,
             (unsigned int)config.distance_near_mm,
             (unsigned int)config.pot_high_pct,
             (int)config.gyro_motion_dps);
    AppObserve_WriteLine(line);
}

static void print_binary_mode(uint8_t enabled)
{
    char line[96];

    snprintf(line, sizeof(line),
             "CMD BINARY %s: %s\r\n",
             enabled ? "ON" : "OFF",
             enabled ? "use PC parser, XCOM may show garbled bytes" : "XCOM text mode");
    AppObserve_WriteLine(line);
}

static void print_log_mode(void)
{
    char line[48];

    snprintf(line, sizeof(line),
             "CMD LOG %s\r\n",
             AppObserve_LogLevelName(AppObserve_GetLogLevel()));
    AppObserve_WriteLine(line);
}

static void print_fault_status(void)
{
    char line[96];

    snprintf(line, sizeof(line),
             "CMD FAULT mask=0x%04lX\r\n",
             (unsigned long)AppHealth_GetFaultMask());
    AppObserve_WriteLine(line);
}

static uint8_t parse_uint_arg(const char *text, uint16_t *value)
{
    uint32_t result = 0U;
    uint8_t digits = 0U;

    while (*text == ' ') {
        text++;
    }

    while (*text >= '0' && *text <= '9') {
        result = (result * 10U) + (uint32_t)(*text - '0');
        if (result > 65535U) {
            return 0U;
        }
        text++;
        digits = 1U;
    }

    while (*text == ' ') {
        text++;
    }

    if (digits == 0U || *text != '\0') {
        return 0U;
    }

    *value = (uint16_t)result;
    return 1U;
}

static uint8_t starts_with(const char *text, const char *prefix)
{
    return (strncmp(text, prefix, strlen(prefix)) == 0) ? 1U : 0U;
}

static void execute_set_command(const char *cmd)
{
    uint16_t value = 0U;
    uint8_t ok = 0U;
    const char *name = NULL;
    char line[96];

    if (starts_with(cmd, "SET HOT ")) {
        name = "HOT";
        ok = parse_uint_arg(&cmd[8], &value) && AppControl_SetTempHot((uint8_t)value);
    } else if (starts_with(cmd, "SET DRY ")) {
        name = "DRY";
        ok = parse_uint_arg(&cmd[8], &value) && AppControl_SetHumidityDry((uint8_t)value);
    } else if (starts_with(cmd, "SET NEAR ")) {
        name = "NEAR";
        ok = parse_uint_arg(&cmd[9], &value) && AppControl_SetDistanceNear(value);
    } else if (starts_with(cmd, "SET POT ")) {
        name = "POT";
        ok = parse_uint_arg(&cmd[8], &value) && AppControl_SetPotHigh((uint8_t)value);
    } else if (starts_with(cmd, "SET MOTION ")) {
        name = "MOTION";
        ok = parse_uint_arg(&cmd[11], &value) && AppControl_SetGyroMotion((int16_t)value);
    }

    if (ok && name != NULL) {
        snprintf(line, sizeof(line), "CMD SET %s OK value=%u\r\n", name, (unsigned int)value);
        AppObserve_WriteLine(line);
        print_config();
    } else {
        AppObserve_WriteLine("CMD SET failed. Use: SET NEAR 120 / SET HOT 32 / SET DRY 30 / SET POT 80 / SET MOTION 100\r\n");
    }
}

static void execute_fault_command(const char *cmd)
{
    AppHealthTaskId id;
    const char *name = &cmd[6];
    char line[112];

    while (*name == ' ') {
        name++;
    }

    if (strcmp(name, "CLEAR") == 0) {
        AppHealth_ClearFaults();
        AppObserve_WriteLine("CMD FAULT CLEAR OK\r\n");
        print_fault_status();
        return;
    }

    if (strcmp(name, "STATUS") == 0) {
        print_fault_status();
        return;
    }

    if (AppHealth_FindTaskId(name, &id)) {
        AppHealth_InjectFault(id);
        snprintf(line, sizeof(line),
                 "CMD FAULT injected task=%s. IWDG will reset if not cleared in about 6s\r\n",
                 AppHealth_TaskName(id));
        AppObserve_WriteLine(line);
        print_fault_status();
        AppHealth_PrintStatus();
    } else {
        AppObserve_WriteLine("CMD FAULT failed. Use: FAULT MPU / FAULT VL53 / FAULT DHT11 / FAULT CLEAR\r\n");
    }
}

static void execute_command(char *cmd)
{
    char line[96];

    trim_and_uppercase(cmd);
    if (cmd[0] == '\0') {
        return;
    }

    if (strcmp(cmd, "?") == 0 || strcmp(cmd, "H") == 0 || strcmp(cmd, "HELP") == 0) {
        print_help();
    } else if (strcmp(cmd, "S") == 0 || strcmp(cmd, "STATUS") == 0) {
        print_status();
    } else if (strcmp(cmd, "SELFTEST") == 0 || strcmp(cmd, "TEST") == 0) {
        execute_selftest();
    } else if (strcmp(cmd, "A") == 0 || strcmp(cmd, "ALERT") == 0) {
        print_alert();
    } else if (strcmp(cmd, "CONFIG") == 0 || strcmp(cmd, "GET CONFIG") == 0) {
        print_config();
    } else if (strcmp(cmd, "SAVE CONFIG") == 0) {
        AppObserve_WriteLine("CMD SAVE CONFIG: erasing Flash sector 7...\r\n");
        if (AppControl_SaveConfigToFlash()) {
            AppObserve_WriteLine("CMD SAVE CONFIG OK\r\n");
        } else {
            AppObserve_WriteLine("CMD SAVE CONFIG FAIL\r\n");
        }
    } else if (strcmp(cmd, "LOAD CONFIG") == 0) {
        if (AppControl_LoadConfigFromFlash()) {
            AppObserve_WriteLine("CMD LOAD CONFIG OK\r\n");
        } else {
            AppObserve_WriteLine("CMD LOAD CONFIG: flash invalid, defaults active\r\n");
        }
        print_config();
    } else if (strcmp(cmd, "DEFAULT CONFIG") == 0) {
        AppControl_ResetConfigDefaults();
        AppObserve_WriteLine("CMD DEFAULT CONFIG OK, use SAVE CONFIG to persist\r\n");
        print_config();
    } else if (starts_with(cmd, "SET ")) {
        execute_set_command(cmd);
    } else if (strcmp(cmd, "B") == 0 || strcmp(cmd, "BIN") == 0) {
        print_binary_mode(AppComm_ToggleBinaryEnabled());
    } else if (strcmp(cmd, "BIN ON") == 0) {
        AppComm_SetBinaryEnabled(1U);
        print_binary_mode(1U);
    } else if (strcmp(cmd, "BIN OFF") == 0) {
        AppComm_SetBinaryEnabled(0U);
        print_binary_mode(0U);
    } else if (starts_with(cmd, "FAULT ")) {
        execute_fault_command(cmd);
    } else if (strcmp(cmd, "HEALTH STATUS") == 0 || strcmp(cmd, "HEALTH") == 0) {
        AppHealth_PrintStatus();
    } else if (strcmp(cmd, "LOG ERROR") == 0) {
        AppObserve_SetLogLevel(APP_LOG_ERROR);
        print_log_mode();
    } else if (strcmp(cmd, "LOG WARN") == 0) {
        AppObserve_SetLogLevel(APP_LOG_WARN);
        print_log_mode();
    } else if (strcmp(cmd, "LOG INFO") == 0) {
        AppObserve_SetLogLevel(APP_LOG_INFO);
        print_log_mode();
    } else if (strcmp(cmd, "LOG DEBUG") == 0) {
        AppObserve_SetLogLevel(APP_LOG_DEBUG);
        print_log_mode();
    } else if (strcmp(cmd, "LOG STATUS") == 0) {
        print_log_mode();
    } else {
        snprintf(line, sizeof(line), "CMD unknown \"%s\", send HELP\r\n", cmd);
        AppObserve_WriteLine(line);
    }
}

void AppCommand_OnRxByteFromISR(uint8_t byte, BaseType_t *higher_priority_woken)
{
    if (s_cmd_rx_queue == NULL) {
        return;
    }

    (void)xQueueSendFromISR(s_cmd_rx_queue, &byte, higher_priority_woken);
}

static void TaskCommand(void *argument)
{
    (void)argument;
    uint8_t byte;
    char cmd[CMD_LINE_MAX_LEN];
    uint8_t cmd_len = 0U;

    AppObserve_WriteLine("CommandTask start: USART1 RX IRQ -> queue -> task\r\n");
    print_help();
    BSP_DebugUART_EnableRxInterrupt();

    for (;;) {
        if (xQueueReceive(s_cmd_rx_queue, &byte,
                          (cmd_len == 0U) ? portMAX_DELAY : CMD_IDLE_TIMEOUT) != pdTRUE) {
            cmd[cmd_len] = '\0';
            execute_command(cmd);
            cmd_len = 0U;
            continue;
        }

        if (byte == '\r') {
            continue;
        }
        if (byte == '\n') {
            cmd[cmd_len] = '\0';
            execute_command(cmd);
            cmd_len = 0U;
            continue;
        }

        if (byte >= 32U && byte <= 126U) {
            if (cmd_len < (CMD_LINE_MAX_LEN - 1U)) {
                cmd[cmd_len++] = (char)byte;
            } else {
                AppObserve_WriteLine("CMD line too long, dropped\r\n");
                cmd_len = 0U;
            }
        }
    }
}

void AppCommand_Start(void)
{
    TaskHandle_t command_handle = NULL;

    s_cmd_rx_queue = xQueueCreate(32U, sizeof(uint8_t));
    if (s_cmd_rx_queue == NULL) {
        Board_Error_Handler();
    }

    if (xTaskCreate(TaskCommand, "CMD", 384U, NULL,
                    tskIDLE_PRIORITY + 2U, &command_handle) != pdPASS) {
        Board_Error_Handler();
    }

    AppObserve_RegisterTask("CMD", command_handle);
}
