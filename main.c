#include "utils/kernel.h"
#include "framework.h"
#include "utils/getline.h"
#include "utils/non_volatile_mem.h"
#include "utils/debug.h"
#include "utils/random.h"
#include "lib/acme_5_outlines_font.h"
#include "lib/BMSPA_font.h"
#include "iot_tcpclient.h"
#include "lib/fontd.h"

#pragma region Icons

#include "icons/wifi.h"
ssd1306_status_icon_array wifiIcon = {
    wifi_bmps_data, // data
    wifi_bmps_size, // size
    118,            // x_offset
    0,              // y_offset
    10,             // width
    10,             // height
    true            // value
};

#include "icons/tcp.h"
ssd1306_status_icon_array tcpIcon = {
    tcp_bmps_data, // data
    tcp_bmps_size, // size
    103,           // x_offset
    0,             // y_offset
    10,            // width
    10,            // height
    true           // value
};

#include "icons/connection.h"
ssd1306_status_icon connectionIcon = {
    connection_bmp_data, // data
    connection_bmp_size, // size
    88,                  // x_offset
    0,                   // y_offset
    10,                  // width
    10,                  // height
    true                 // value
};

#include "icons/loading.h"
ssd1306_status_icon_array loadingIcon = {
    loading_bmps_data, // data
    loading_bmps_size, // size
    39,                // x_offset
    12,                // y_offset
    50,                // width
    50,                // height
    true               // value
};

#include "icons/panic_code.h"
#include "icons/motion_detected.h"
#pragma endregion

#define ACTION_BTN 15
#define ACTION_BTN_DOWN !gpio_get(ACTION_BTN)

#define PANIC_BTN 16
#define PANIC_BTN_DOWN !gpio_get(PANIC_BTN)

#define MOTION_SENSOR 11

typedef struct _settingsData
{
    uint8_t controlByteA;
    uint8_t controlByteC;
    bool wifiSetup;
    char wifiSSID[33];
    char wifiPassword[65];
    uint8_t controlByteB;
    uint8_t controlByteD;
} SettingsData;

rotencoder_t actionRot;

SemaphoreHandle_t dispMut; // I2C is not thread safe
ssd1306_t disp;
uint32_t logCounter = 0;
#define LOG_LINE_COUNT 7
const char *logLines[LOG_LINE_COUNT];
#define GET_ROLLING_BUFFER(buf, size, ind, off) ((buf)[((ind) + (off)) % (size)])

iot_tcp_client_t client;
bool clientInitialized = false;

bool rtcClockSet = false;

// screen pages
#define SCREEN_PAGE_TIME 0
#define SCREEN_PAGE_TEMP 1
#define SCREEN_PAGE_MOTION 2
#define SCREEN_PAGE_ABOUT 3

#define SCREEN_PAGE_STR(p) (                                          \
    p == SCREEN_PAGE_TIME ? "Time" : p == SCREEN_PAGE_TEMP ? "Temp"   \
                                 : p == SCREEN_PAGE_MOTION ? "Motion" \
                                 : p == SCREEN_PAGE_ABOUT  ? "About"  \
                                                           : "Uknown")

void vUpdateDisplayLog()
{
    F_START("vUpdateDisplayLog");
    if (logCounter >= LOG_LINE_COUNT)
        logCounter = 0;
    ssd1306_draw_square(&disp, 0, 10, disp.width, disp.height - 10, false);
    for (size_t i = 0; i < LOG_LINE_COUNT; i++)
    {
        ssd1306_draw_string(&disp, 0, (LOG_LINE_COUNT - i - 1) * 8 + 10, 1, GET_ROLLING_BUFFER(logLines, LOG_LINE_COUNT, i, logCounter), true);
    }
    F_END("vUpdateDisplayLog");
}

void debugLog(const char *frm, const char *shortFrm, ...)
{
    F_START("debugLog");
    va_list args;
    va_start(args, shortFrm);
    if (frm)
    {
        vprintf(frm, args);
        printf("\n");
    }

    if (shortFrm)
    {
        char *dbgBuffer = malloc(19 * sizeof(char));
        vsnprintf(dbgBuffer, 19, shortFrm, args);
        logLines[logCounter++] = dbgBuffer;
        vUpdateDisplayLog();
        ssd1306_show(&disp);
    }
    va_end(args);
    F_END("debugLog");
}

void setRTCTime(uint32_t sec)
{
    const time_t t = (const time_t)sec;
    struct tm *ptm = gmtime(&t);
    datetime_t time = {
        .year = 1900 + ptm->tm_year,
        .month = ptm->tm_mon + 1,
        .day = ptm->tm_mday,
        .dotw = ptm->tm_wday,
        .hour = ptm->tm_hour,
        .min = ptm->tm_min,
        .sec = ptm->tm_sec};
    rtc_set_datetime(&time);
    rtcClockSet = true;
}

void rtos_panic_oled(const char *fmt, ...)
{
    puts("\n*** PANIC ***\n");
    if (fmt)
    {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        puts("\n");
    }

    printf("[PANIC] Printing system state dump:\n");
    vTaskPrintRunTimeStats();

    printf("[PANIC] Debug trace:\n");
    debug_trace_print_entries();

    if (dispMut != NULL)
    {
        bool safe = false;

        if (!xSemaphoreTake(dispMut, 100))
            printf("[PANIC] WARNING! Unsafe display access\n");
        else
            safe = true;

        ssd1306_fill(&disp);
        ssd1306_draw_string(&disp, 10, 3, 2, "**PANIC**", false);

        if (fmt)
        {
            char buf[32];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, 32, fmt, args);
            va_end(args);
            ssd1306_draw_string(&disp, 2, 18, 1, buf, false);
        }

        short_task_desc_t *tasks = vTaskGetRunTimeStatsShort(3);
        size_t maxLen = 0;

        for (int i = 0; i < 3; i++)
        {
            size_t l = strlen(tasks[i].name);
            if (l > maxLen)
                maxLen = l;
            ssd1306_draw_string(&disp, 2, 28 + i * 9, 1, tasks[i].name, false);
        }

        char buf[32];
        for (int i = 0; i < 3; i++)
        {
            sprintf(buf, tasks[i].cpuUsed == 0 ? "<1%%" : "%u%%", tasks[i].cpuUsed);
            ssd1306_draw_string(&disp, 2 + maxLen * 6 + 2, 28 + i * 9, 1, buf, false);
        }
        vPortFree(tasks);

        ssd1306_draw_string(&disp, 2, disp.height - 8, 1, "Exit code: 1   DEBUG", false);

        ssd1306_bmp_show_image_with_offset(&disp, panic_code_bmp_data, panic_code_bmp_size, disp.width - 37 - 2, 18, ROTATE_NONE, false);

        ssd1306_show(&disp);
        sleep_ms(200);
        ssd1306_show(&disp); // fix visual glitch

        if (safe)
            xSemaphoreGive(dispMut);
    }

    exit(1);
}

static const char *status_name(int status)
{
    F_START("status_name");
    switch (status)
    {
    case CYW43_LINK_DOWN:
        F_RETURNV("status_name", "link down");
    case CYW43_LINK_JOIN:
        F_RETURNV("status_name", "joining");
    case CYW43_LINK_NOIP:
        F_RETURNV("status_name", "no ip");
    case CYW43_LINK_UP:
        F_RETURNV("status_name", "link up");
    case CYW43_LINK_FAIL:
        F_RETURNV("status_name", "link fail");
    case CYW43_LINK_NONET:
        F_RETURNV("status_name", "network fail");
    case CYW43_LINK_BADAUTH:
        F_RETURNV("status_name", "bad auth");
    }
    F_RETURNV("status_name", "unknown");
}

static int scan_result(void *env, const cyw43_ev_scan_result_t *result)
{
    F_START("scan_result");
    if (result)
    {
        printf("ssid: %-32s rssi: %4d chan: %3d mac: %02x:%02x:%02x:%02x:%02x:%02x sec: %u\n",
               result->ssid, result->rssi, result->channel,
               result->bssid[0], result->bssid[1], result->bssid[2], result->bssid[3], result->bssid[4], result->bssid[5],
               result->auth_mode);
    }
    F_RETURNV("scan_result", 0);
}

void setupWIFI()
{
    C_START("setupWIFI");
    printf("\n"
           "*=====================================================================================================*\n"
           "*                                                                                                     *\n"
           "*   ██╗ ██████╗ ████████╗    ██╗    ██╗██╗███████╗██╗    ███████╗███████╗████████╗██╗   ██╗██████╗    *\n*   ██║██╔═══██╗╚══██╔══╝    ██║    ██║██║██╔════╝██║    ██╔════╝██╔════╝╚══██╔══╝██║   ██║██╔══██╗   *\n*   ██║██║   ██║   ██║       ██║ █╗ ██║██║█████╗  ██║    ███████╗█████╗     ██║   ██║   ██║██████╔╝   *\n*   ██║██║   ██║   ██║       ██║███╗██║██║██╔══╝  ██║    ╚════██║██╔══╝     ██║   ██║   ██║██╔═══╝    *\n*   ██║╚██████╔╝   ██║       ╚███╔███╔╝██║██║     ██║    ███████║███████╗   ██║   ╚██████╔╝██║        *\n*   ╚═╝ ╚═════╝    ╚═╝        ╚══╝╚══╝ ╚═╝╚═╝     ╚═╝    ╚══════╝╚══════╝   ╚═╝    ╚═════╝ ╚═╝        *\n"
           "*                                                                                                     *\n"
           "*=====================================================================================================*\n"
           "\n");
    absolute_time_t scan_test = nil_time;
    bool scan_in_progress = false;
    bool run_scan = true;

    while (run_scan)
    {
        if (absolute_time_diff_us(get_absolute_time(), scan_test) < 0)
        {
            if (!scan_in_progress)
            {
                debugLog("[WIFI] Starting scan", "Start wifi scan");
                printf("WiFi Scan Results:\n\n");
                cyw43_wifi_scan_options_t scan_options = {0};
                int err = cyw43_wifi_scan(&cyw43_state, &scan_options, NULL, scan_result);
                if (err == 0)
                {
                    scan_in_progress = true;
                }
                else
                {
                    debugLog("[WIFI] Failed to start scan: %d", "Failed(%d)! Retry", err);
                    scan_test = make_timeout_time_ms(10000); // wait 10s and scan again
                }
            }
            else if (!cyw43_wifi_scan_active(&cyw43_state))
            {
                debugLog("[WIFI] Finished scan", "Wifi scan done");
                run_scan = false;
                scan_in_progress = false;
            }
        }
    }
    C_END("setupWIFI");
}

void setupWIFI_end(SettingsData *settings, SettingsData *oldSettings)
{
    C_START("setupWIFI_end");
    size_t length = 0;

    printf("\n[CFG] Enter the wifi SSID:\n");
    char *ssid = waitForLine(true, '\r', &length);
    length = min(length, 32);
    strncpy(settings->wifiSSID, ssid, length);
    settings->wifiSSID[length] = '\0';
    free(ssid);

    debugLog(NULL, "Copied wifi SSID.");

    printf("\n[CFG] Enter the wifi Password:\n");
    char *passwrd = waitForLine(true, '\r', &length);
    length = min(length, 64);
    strncpy(settings->wifiPassword, passwrd, length);
    settings->wifiPassword[length] = '\0';
    free(passwrd);

    debugLog(NULL, "Copied wifi password.");

    settings->wifiSetup = true;
    settings->controlByteA = oldSettings->controlByteA + 1;
    settings->controlByteB = settings->controlByteA + 3;
    settings->controlByteC = settings->controlByteB + 2;
    settings->controlByteD = settings->controlByteC + 1;

    debugLog("[CFG] Generated error correction bytes", "Gen EC bytes.");
    C_END("setupWIFI_end");
}

int wifi_connect_until(const char *ssid, const char *pw, uint32_t auth, absolute_time_t until)
{
    C_START("wifi_connect_until");
    int err = cyw43_arch_wifi_connect_async(ssid, pw, auth);
    if (err)
        C_RETURNV("wifi_connect_until", err);

    int status = CYW43_LINK_UP + 1;
    int joinStep = 0;
    absolute_time_t nextAnimStep = make_timeout_time_ms(400);
    while (status >= 0 && status != CYW43_LINK_UP)
    {
        int new_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (new_status != status)
        {
            status = new_status;
            if (status == CYW43_LINK_NOIP)
            {
                ssd1306_draw_status_icon_array(&disp, wifiIcon, ICONS_WIFI_NOIP);
                ssd1306_show(&disp);
            }
            debugLog("[WIFI] Connect status: %s", NULL, status_name(status));
        }

        if (status == CYW43_LINK_JOIN)
        {
            if (time_reached(nextAnimStep))
            {
                nextAnimStep = make_timeout_time_ms(400);
                ssd1306_draw_status_icon_array(&disp, wifiIcon, ICONS_WIFI_JOIN_0 + joinStep);
                ssd1306_show(&disp);
                if (++joinStep >= 4)
                    joinStep = 0;
            }
        }

        if (time_reached(until))
        {
            C_RETURNV("wifi_connect_until", PICO_ERROR_TIMEOUT);
        }
    }
    C_RETURNV("wifi_connect_until",
              status == CYW43_LINK_UP ? 0 : status);
}

static void tcp_status_update(bool data)
{
    F_START("tcp_status_update");
    if (dispMut != NULL)
    {
        if (xSemaphoreTake(dispMut, 10))
        {
            ssd1306_clear_status_icon_area(&disp, connectionIcon, 0, 0, 0, 0);
            if (data)
                ssd1306_draw_status_icon_overlay(&disp, connectionIcon);
            ssd1306_show(&disp);

            xSemaphoreGive(dispMut);
        }
    }
    F_END("tcp_status_update");
}

static void tcp_task(__unused void *params)
{
    C_START("tcp_task");
    int client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    struct sockaddr_in listen_addr = {};
    listen_addr.sin_len = sizeof(struct sockaddr_in);
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(5001);
    listen_addr.sin_addr.s_addr = 0;

    struct sockaddr_in connect_addr = {};
    connect_addr.sin_len = sizeof(struct sockaddr_in);
    connect_addr.sin_family = AF_INET;
    connect_addr.sin_port = htons(23);
    connect_addr.sin_addr.s_addr = PP_HTONL(LWIP_MAKEU32(192, 168, 1, 153));

    if (client_sock < 0)
    {
        debugLog("[TCP] Unable to create socket: error %d", NULL, errno);

        ssd1306_draw_status_icon_array(&disp, tcpIcon, ICONS_TCP_FAIL);
        ssd1306_show(&disp);
        vTaskDelete(NULL);
        C_RETURN("tcp_task");
    }

    if (bind(client_sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0)
    {
        debugLog("[TCP] Unable to bind socket: error %d", NULL, errno);

        ssd1306_draw_status_icon_array(&disp, tcpIcon, ICONS_TCP_FAIL);
        ssd1306_show(&disp);
        vTaskDelete(NULL);
        C_RETURN("tcp_task");
    }

    if (connect(client_sock, (struct sockaddr *)&connect_addr, sizeof(connect_addr)) < 0)
    {
        debugLog("[TCP] Unable to connect to server: error %d", NULL, errno);

        ssd1306_draw_status_icon_array(&disp, tcpIcon, ICONS_TCP_FAIL);
        ssd1306_show(&disp);
        vTaskDelete(NULL);
        C_RETURN("tcp_task");
    }

    debugLog("[TCP] Connected client from %s with port %u", NULL, ip4addr_ntoa(netif_ip4_addr(netif_list)), ntohs(listen_addr.sin_port));

    ssd1306_draw_status_icon_array(&disp, tcpIcon, ICONS_TCP_UP);
    ssd1306_show(&disp);

    IOT_constructor(&client, &client_sock, tcp_status_update);

    clientInitialized = true;
    IOT_startMessageLoop(&client);
    clientInitialized = false;

    ssd1306_draw_status_icon_array(&disp, tcpIcon, ICONS_TCP_FAIL);
    ssd1306_show(&disp);

    vTaskDelete(NULL);
    C_END("tcp_task");
}

typedef enum
{
    TempC = 0,
    TempF = 1
} TemperatureUnit;

#define TEMP_UNIT(u) ((u) == 'C' ? TempC : (u) == 'F' ? TempF \
                                                      : -1)
#define STR_TEMP_UNIT(u) ((u) == TempC ? "C" : (u) == TempF ? "F" \
                                                            : "-")

float read_onboard_temperature(const char unit)
{
    /* 12-bit conversion, assume max value == ADC_VREF == 3.3 V */
    const float conversionFactor = 3.3f / (1 << 12);

    float adc = (float)adc_read() * conversionFactor;
    float tempC = 27.0f - (adc - 0.706f) / 0.001721f;

    if (unit == 'C')
    {
        return tempC;
    }
    else if (unit == 'F')
    {
        return tempC * 9 / 5 + 32;
    }

    return -1.0f;
}

int64_t dim_alarm_callback(alarm_id_t id, void *user_data)
{
    ssd1306_contrast(&disp, 1);
    return 0;
}

int64_t turnoff_alarm_callback(alarm_id_t id, void *user_data)
{
    ssd1306_poweroff(&disp);
    return 0;
}

int displayOffsetX = 0;
int displayOffsetY = 0;

bool shift_timer_callback(repeating_timer_t *rt)
{
    displayOffsetX = get_rand_int(-2, 3);
    displayOffsetY = get_rand_int(-2, 3);
    return true; // keep repeating
}

static void ui_task(void *params)
{
    C_START("ui_task");

    SettingsData *settings = (SettingsData *)params;

    if (dispMut != NULL)
    {
        if (!xSemaphoreTake(dispMut, 100))
            printf("[UI] WARNING! Unsafe display access\n");
        else
        {
            ssd1306_draw_square(&disp, 0, 10, disp.width, disp.height - 10, false); // clear client area
            ssd1306_show(&disp);
            xSemaphoreGive(dispMut);
        }
    }

    size_t loadingAnim = 0;
    ssd1306_bmp_rotation_t loadingAnimRot = ROTATE_NONE;

    while (!rtcClockSet)
    {
        vTaskDelay(5);

        if (dispMut != NULL)
        {
            if (!xSemaphoreTake(dispMut, 100))
                printf("[UI] WARNING! Unsafe display access\n");
            else
            {
                ssd1306_draw_square(&disp, 0, 10, disp.width, disp.height - 10, false); // clear client area
                ssd1306_draw_status_icon_array_overlay(&disp, loadingIcon, 45 - loadingAnim, loadingAnimRot);
                ssd1306_show(&disp);
                xSemaphoreGive(dispMut);
            }
        }

        loadingAnim += 3;
        if (loadingAnim > 45)
        {
            loadingAnim = 3;
            if (loadingAnimRot == ROTATE_NONE)
                loadingAnimRot = ROTATE_90;
            else if (loadingAnimRot == ROTATE_90)
                loadingAnimRot = ROTATE_180;
            else if (loadingAnimRot == ROTATE_180)
                loadingAnimRot = ROTATE_270;
            else if (loadingAnimRot == ROTATE_270)
                loadingAnimRot = ROTATE_NONE;
        }
    }

    vTaskDelay(1);

    int page = -1;
    datetime_t timeS;
    char strBuf[128];
    char strBufS[32];
    float onboardTemp = 0;
    absolute_time_t screenUpdateTime = get_absolute_time();
    absolute_time_t tempUpdateTime = get_absolute_time();
    absolute_time_t page_auto_switch = make_timeout_time_ms(30 * 1000);
    alarm_id_t dimAlarm = add_alarm_in_ms(2 * 60 * 1000, dim_alarm_callback, NULL, true);         // dim screen after 2 mins
    alarm_id_t turnOffAlarm = add_alarm_in_ms(5 * 60 * 1000, turnoff_alarm_callback, NULL, true); // turn off screen after 5 mins
    absolute_time_t lastMotionTime = get_absolute_time();

    repeating_timer_t shiftTimer;
    if (!add_repeating_timer_ms(-1000 * 60 * 10, shift_timer_callback, NULL, &shiftTimer))
    {
        debugLog("[INIT] Failed to create shift timer", "SHTimer: FAIL");
    }
    else
    {
        debugLog("[INIT] Created shift timer [%i] @%" PRId64, "SHTimer: OK", shiftTimer.alarm_id, shiftTimer.delay_us);
    }

    while (client.running)
    {
        if (gpio_get(MOTION_SENSOR))
        {
            lastMotionTime = get_absolute_time();
        }

        if (ACTION_BTN_DOWN || actionRot.rel_val != 0 || gpio_get(MOTION_SENSOR))
        {
            // turn on display after interaction
            if (!cancel_alarm(turnOffAlarm))
            {
                if (dispMut != NULL)
                {
                    if (!xSemaphoreTake(dispMut, 100))
                        printf("[UI_INTR] WARNING! Unsafe display access\n");
                    else
                    {
                        ssd1306_poweron(&disp);
                        xSemaphoreGive(dispMut);
                    }
                }
            }

            if (!cancel_alarm(dimAlarm))
            {
                if (dispMut != NULL)
                {
                    if (!xSemaphoreTake(dispMut, 100))
                        printf("[UI_INTR] WARNING! Unsafe display access\n");
                    else
                    {
                        ssd1306_contrast(&disp, 0xff);
                        xSemaphoreGive(dispMut);
                    }
                }
            }
            dimAlarm = add_alarm_in_ms(2 * 60 * 1000, dim_alarm_callback, NULL, true);
            turnOffAlarm = add_alarm_in_ms(5 * 60 * 1000, turnoff_alarm_callback, NULL, true);
        }

        if (page == -1 || (actionRot.rel_val != 0) || time_reached(page_auto_switch))
        {
            if (page == -1)
                page = SCREEN_PAGE_TIME;
            else if (actionRot.rel_val != 0)
            {
                page = wrap(page + actionRot.rel_val, SCREEN_PAGE_TIME, SCREEN_PAGE_ABOUT + 1);
            }
            else if (time_reached(page_auto_switch))
            {
                if (++page > SCREEN_PAGE_MOTION)
                    page = SCREEN_PAGE_TIME;
            }

            actionRot.rel_val = 0;
            printf("[UI] Switching to page: %s\n", SCREEN_PAGE_STR(page));
            screenUpdateTime = get_absolute_time();
            page_auto_switch = make_timeout_time_ms(30 * 1000);
        }

        if (time_reached(screenUpdateTime) && dispMut != NULL)
        {
            if (!xSemaphoreTake(dispMut, 100))
                printf("[UI] WARNING! Unsafe display access\n");
            else
            {
                disp.offsetX = 0;
                disp.offsetY = 0;
                screenUpdateTime = make_timeout_time_ms(200);
                ssd1306_draw_square(&disp, 0, 10, disp.width, disp.height - 10, false); // clear client area
                ssd1306_draw_square(&disp, disp.width / 2 - 8, 0, 16, 10, false);       // clear top part of overlay img

                disp.offsetX = displayOffsetX;
                disp.offsetY = displayOffsetY;

                switch (page)
                {
                case SCREEN_PAGE_TIME:
                {
                    if (!rtc_get_datetime(&timeS))
                    {
                        ssd1306_draw_string(&disp, 10, 10, 1, "RTC Error", true);
                        break;
                    }

                    struct tm timeT;
                    timeT.tm_mday = timeS.day;
                    timeT.tm_isdst = 0;
                    timeT.tm_year = timeS.year - 1900;
                    timeT.tm_mon = timeS.month - 1;
                    timeT.tm_wday = timeS.dotw;
                    timeT.tm_hour = timeS.hour;
                    timeT.tm_min = timeS.min;
                    timeT.tm_sec = timeS.sec;
                    time_t utc = mktime(&timeT);
                    time_t local = utc + (CLOCK_TIMEZONE) + (CLOCK_DAYLIGHT_SAVINGS ? 3600 : 0);
                    struct tm *localT = localtime(&local);

                    datetime_t timeL = {
                        .year = 1900 + localT->tm_year,
                        .month = localT->tm_mon + 1,
                        .day = localT->tm_mday,
                        .dotw = localT->tm_wday,
                        .hour = localT->tm_hour,
                        .min = localT->tm_min,
                        .sec = localT->tm_sec};

                    timeL.hour %= 12;
                    if (timeL.hour == 0)
                        timeL.hour = 12;

                    sprintf(strBuf, "%d:%02d", timeL.hour, timeL.min);

                    ssd1306_string_measure timeSize = ssd1306_measure_string(BMSPA_font, strBuf, 3);
                    uint32_t offX = disp.width / 2 - timeSize.width / 2;
                    uint32_t offY = disp.height / 2 - timeSize.height / 2;

                    ssd1306_draw_string_with_font(&disp, offX, offY, 3, BMSPA_font, strBuf, true);
                    break;
                }
                case SCREEN_PAGE_TEMP:
                {
                    if (time_reached(tempUpdateTime))
                    {
                        tempUpdateTime = make_timeout_time_ms(1000);
                        adc_select_input(4); // select temp sensor
                        onboardTemp = read_onboard_temperature(TEMPERATURE_UNITS);
                    }

                    sprintf(strBuf, "%d", (int)roundf(onboardTemp));

                    ssd1306_string_measure strSize = ssd1306_measure_string(BMSPA_font, strBuf, 3);
                    ssd1306_string_measure degSize = ssd1306_measure_string(BMSPA_font, "o", 1);
                    ssd1306_string_measure unitSize = ssd1306_measure_string(BMSPA_font, STR_TEMP_UNIT(TEMP_UNIT(TEMPERATURE_UNITS)), 2);
                    uint32_t totalWidth = strSize.width + 2 * 3 + degSize.width + 2 * 1 + unitSize.width;
                    uint32_t offX = disp.width / 2 - totalWidth / 2;
                    uint32_t offY = disp.height / 2 - strSize.height / 2;
                    uint32_t degX = offX + strSize.width + 2 * 3;
                    uint32_t unitX = degX + degSize.width + 2 * 1;

                    ssd1306_draw_string_with_font(&disp, offX, offY, 3, BMSPA_font, strBuf, true);
                    ssd1306_draw_string_with_font(&disp, degX, offY, 1, BMSPA_font, "o", true);
                    ssd1306_draw_string_with_font(&disp, unitX, offY, 2, BMSPA_font, STR_TEMP_UNIT(TEMP_UNIT(TEMPERATURE_UNITS)), true);
                    break;
                }
                case SCREEN_PAGE_MOTION:
                {
                    bool motion = gpio_get(MOTION_SENSOR);
                    if (motion)
                    {
                        ssd1306_bmp_show_image_with_offset(&disp, motion_detected_bmp_data, motion_detected_bmp_size, 0, 0, ROTATE_NONE, true);
                    }
                    else
                    {
                        int64_t timeSinceLastMotion = absolute_time_diff_us(lastMotionTime, get_absolute_time());
                        int64_t milliseconds = timeSinceLastMotion / 1000;

                        int64_t seconds = milliseconds / 1000;
                        milliseconds %= 1000;

                        int64_t minutes = seconds / 60;
                        seconds %= 60;

                        int64_t hours = minutes / 60;
                        minutes %= 60;

                        ssd1306_draw_string_with_font(&disp, 0, 10, 1, fontd_8x5, "No motion for:", true);

                        if (hours == 0)
                        {
                            if (minutes == 0)
                            {
                                // draw big seconds
                                sprintf(strBuf, "%lld", seconds);

                                ssd1306_string_measure numSize = ssd1306_measure_string(fontd_8x5, strBuf, 3);
                                ssd1306_string_measure unitSize = ssd1306_measure_string(fontd_8x5, "sec.", 2);

                                uint32_t offX = disp.width / 2 - (numSize.width + 3 + unitSize.width) / 2;
                                uint32_t offY = disp.height / 2 - numSize.height / 2;
                                uint32_t unitOffX = offX + numSize.width + 3;

                                ssd1306_draw_string_with_font(&disp, offX, offY, 3, fontd_8x5, strBuf, true);
                                ssd1306_draw_string_with_font(&disp, unitOffX, offY + (numSize.height - unitSize.height), 2, fontd_8x5, "sec.", true);
                            }
                            else
                            {
                                // draw big minutes + small seconds
                                sprintf(strBuf, "%lld", minutes);
                                sprintf(strBufS, ":%02lld", seconds);

                                ssd1306_string_measure numSize = ssd1306_measure_string(fontd_8x5, strBuf, 3);
                                ssd1306_string_measure subSize = ssd1306_measure_string(fontd_8x5, strBufS, 2);

                                uint32_t offX = disp.width / 2 - (numSize.width + 3 + subSize.width) / 2;
                                uint32_t offY = disp.height / 2 - numSize.height / 2;
                                uint32_t subOffX = offX + numSize.width + 3;

                                ssd1306_draw_string_with_font(&disp, offX, offY, 3, fontd_8x5, strBuf, true);
                                ssd1306_draw_string_with_font(&disp, subOffX, offY + (numSize.height - subSize.height), 2, fontd_8x5, strBufS, true);
                            }
                        }
                        else
                        {
                            // draw big hours + small minutes:seconds
                            sprintf(strBuf, "%lld", hours);
                            sprintf(strBufS, ":%02lld:%02lld", minutes, seconds);

                            ssd1306_string_measure numSize = ssd1306_measure_string(fontd_8x5, strBuf, 3);
                            ssd1306_string_measure subSize = ssd1306_measure_string(fontd_8x5, strBufS, 2);

                            uint32_t offX = disp.width / 2 - (numSize.width + 3 + subSize.width) / 2;
                            uint32_t offY = disp.height / 2 - numSize.height / 2;
                            uint32_t subOffX = offX + numSize.width + 3;

                            ssd1306_draw_string_with_font(&disp, offX, offY, 3, fontd_8x5, strBuf, true);
                            ssd1306_draw_string_with_font(&disp, subOffX, offY + (numSize.height - subSize.height), 2, fontd_8x5, strBufS, true);
                        }
                    }
                    break;
                }
                case SCREEN_PAGE_ABOUT:
                {
                    sprintf(strBuf, "picow-iot-device\nCompdog Inc.(c) 2023\nv0.4.1 %s\n%s\n%s", PICO_CMAKE_BUILD_TYPE, WIFI_HOSTNAME, settings->wifiSSID);
                    ssd1306_draw_string(&disp, 0, 10, 1, strBuf, true);
                    break;
                }
                }

                ssd1306_show(&disp);
                disp.offsetX = 0;
                disp.offsetY = 0;
                xSemaphoreGive(dispMut);
            }
        }
        vTaskDelay(20);
    }

    vTaskDelete(NULL);
    C_END("ui_task");
}

static void main_task(__unused void *params)
{
    C_START("main_task");

    debugLog("[MAIN] Initializing cyw43_arch", "Init cyw43");
    if (cyw43_arch_init())
    {
        debugLog("[MAIN] Failed to initialize cyw43_arch", "ERROR: failed cyw43");
        ssd1306_draw_status_icon_array(&disp, wifiIcon, ICONS_WIFI_FAIL);
        ssd1306_show(&disp);
        vTaskDelete(NULL);
        C_RETURN("main_task");
    }

    cyw43_arch_enable_sta_mode();
    netif_set_hostname(netif_default, WIFI_HOSTNAME);

    debugLog("[WIFI] Set netif hostname '%s'", "Set netif hostname", WIFI_HOSTNAME);

    gpio_init(ACTION_BTN);
    gpio_set_dir(ACTION_BTN, GPIO_IN);
    gpio_pull_up(ACTION_BTN);

    gpio_init(PANIC_BTN);
    gpio_set_dir(PANIC_BTN, GPIO_IN);
    gpio_pull_up(PANIC_BTN);

    gpio_init(MOTION_SENSOR);
    gpio_set_dir(MOTION_SENSOR, GPIO_IN);

    SettingsData *settings = (SettingsData *)nvmem_read(0);
    debugLog("[CFG] Read settings from nvmem (nvmem_read)", "Settings loaded");
    bool invalid = false;

    if (
        settings->controlByteD != settings->controlByteC + 1 ||
        settings->controlByteC != settings->controlByteB + 2 ||
        settings->controlByteB != settings->controlByteA + 3)
    {
        debugLog(
            "[CFG] Detected corruption in settings(EC) '%u/%u/%u/%u'",
            "Corruption detect:\n%u/%u/%u/%u",
            settings->controlByteA, settings->controlByteB, settings->controlByteC, settings->controlByteD);
        invalid = true;
    }

    if (ACTION_BTN_DOWN)
    {
        debugLog("[CFG] Manually resetting flash memory...", "!Flash reset!");
        invalid = true;
    }

    if (invalid)
    {
        ssd1306_draw_status_icon_array(&disp, wifiIcon, ICONS_WIFI_SETUP);
        ssd1306_show(&disp);

        setupWIFI();
        debugLog("[CFG] Force kernel exit", "Kernel exit");

        // force reboot with watchdog to write flash in kernel mode
        watchdog_enable(1, false);
        while (1)
            ;
    }

    size_t passLen = strlen(settings->wifiPassword);

    bool isOpenWifi = passLen <= 2;

    char censoredPassword[passLen + 1]; // + NULL char
    memset(censoredPassword, '*', passLen);
    censoredPassword[passLen] = 0x00;

    debugLog("\n[WIFI] SSID: %s, PASS: %s", NULL, settings->wifiSSID, isOpenWifi ? "Open WiFi" : censoredPassword);

    debugLog("[WIFI] Connecting... (30sec)", "WIFI connect start");
    int connectResult;
    if (isOpenWifi)
    {
        connectResult = wifi_connect_until(settings->wifiSSID, NULL, CYW43_AUTH_OPEN, make_timeout_time_ms(30000));
    }
    else
    {
        connectResult = wifi_connect_until(settings->wifiSSID, settings->wifiPassword, CYW43_AUTH_WPA2_AES_PSK, make_timeout_time_ms(30000));
    }

    if (connectResult != 0)
    {
        ssd1306_draw_status_icon_array(&disp, wifiIcon, ICONS_WIFI_FAIL);
        ssd1306_show(&disp);
        debugLog("[WIFI] Failed to connect: %i", NULL, connectResult);
        exit(1);
    }
    else
    {
        logCounter = 0;
        memset(logLines, 0, LOG_LINE_COUNT * sizeof(char *));
        ssd1306_clear(&disp);
        ssd1306_draw_status_icon_array(&disp, wifiIcon, ICONS_WIFI_UP);
        ssd1306_show(&disp);
    }

    sntp_init();

    TaskHandle_t tcpTask;
    debugLog("[MAIN] Starting TCP client task", "Starting client.");
    xTaskCreate(tcp_task, "TCPThread", configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 2UL), &tcpTask);

    while (!clientInitialized) // wait for iot client
    {
        vTaskDelay(10);
    }

    TaskHandle_t uiTask;
    debugLog("[MAIN] Starting UI task", "Starting UI.");
    xTaskCreate(ui_task, "UIThread", configMINIMAL_STACK_SIZE, settings, (tskIDLE_PRIORITY + 2UL), &uiTask);

    absolute_time_t nextSendTime = make_timeout_time_ms(1000);
    while (client.running)
    {
        if (PANIC_BTN_DOWN)
        {
            panic("User check");
        }

        if (time_reached(nextSendTime))
        {
            nextSendTime = make_timeout_time_ms(1000);
            if (IOT_Send(&client, "{b}", JE_MEMBER(&((&client)->packet), led)) < 0)
                IOT_stopMessageLoop(&client);
        }
    }

    cyw43_arch_deinit();

    vTaskDelete(NULL);
    C_END("main_task");
}

void vInitScreen()
{
    I_START("vInitScreen");
    i2c_init(i2c0, 400000);
    gpio_set_function(4, GPIO_FUNC_I2C);
    gpio_set_function(5, GPIO_FUNC_I2C);
    gpio_pull_up(4);
    gpio_pull_up(5);

    disp.external_vcc = false;
    ssd1306_init(&disp, 128, 64, 0x3C, i2c0);
    ssd1306_clear(&disp);
    vUpdateDisplayLog();
    ssd1306_show(&disp);

    dispMut = xSemaphoreCreateMutex();
    I_END("vInitScreen");
}

volatile unsigned portLONG ulHighFrequencyTimerTicks = 0UL;
bool task_timer_callback(repeating_timer_t *rt)
{
    ulHighFrequencyTimerTicks++; // tick timer
    return true;                 // keep repeating
}

bool trace_timer_callback(repeating_timer_t *rt)
{
    debug_trace_update_entries();
    return true; // keep repeating
}

void initTaskTimer(repeating_timer_t *timer)
{
    I_START("initTaskTimer");
    if (!add_repeating_timer_us(-1000000 / 20, task_timer_callback, NULL, timer))
    {
        debugLog("[INIT] Failed to create task tick timer", "TKTimer: FAIL");
        I_RETURN("initTaskTimer");
    }

    debugLog("[INIT] Created task tick timer [%i] @%" PRId64, "TKTimer: OK", timer->alarm_id, timer->delay_us);
    I_END("initTaskTimer");
}

void initTraceTimer(repeating_timer_t *timer)
{
    I_START("initTraceTimer");
    if (!add_repeating_timer_us(-1000000 / 10, trace_timer_callback, NULL, timer))
    {
        debugLog("[INIT] Failed to create trace update timer", "DBGTimer: FAIL");
        I_RETURN("initTraceTimer");
    }

    debugLog("[INIT] Created trace update timer [%i] @%" PRId64, "DBGTimer: OK", timer->alarm_id, timer->delay_us);
    I_END("initTraceTimer");
}

int main()
{
    repeating_timer_t timer;
    repeating_timer_t traceTimer;

    stdio_init_all();
    debug_trace_init();

    rtc_init();
    datetime_t time = {
        .year = 2023,
        .month = 02,
        .day = 27,
        .dotw = 1,
        .hour = 12,
        .min = 00,
        .sec = 00};
    rtc_set_datetime(&time);
    sleep_us(64);

    adc_init();
    adc_set_temp_sensor_enabled(true);

    vInitScreen();
    initTaskTimer(&timer);
    initTraceTimer(&traceTimer);

    rotencoder_register_callback(); // enable internal gpio callback
    rotencoder_init(&actionRot, 12, 13);

    if (watchdog_caused_reboot())
    {
        SettingsData *settings = (SettingsData *)nvmem_read(0);
        debugLog("[CFG] Read settings from nvmem (nvmem_read)", "Settings loaded");

        SettingsData newSettings = {};
        setupWIFI_end(&newSettings, settings);

        debugLog("[CFG] Resetting dyn_flash... (nvmem_reset)", "Resetting flash...");
        nvmem_reset();

        debugLog("[CFG] Writing settings to dyn_flash... (nvmem_write)", "Writing flash...");
        nvmem_write((uint8_t *)(&newSettings), sizeof(SettingsData));

        settings = (SettingsData *)nvmem_read(0);
        debugLog("[CFG] Read settings from nvmem (nvmem_read)", "Settings loaded");
    }

    debugLog("[BOOT] Creating MainThread task", "Create MainThread");
    TaskHandle_t task;
    xTaskCreate(main_task, "MainThread", configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 2UL), &task);

    debugLog("[BOOT] Starting task scheduler", "Start tasksch");
    vTaskStartScheduler();

    rotencoder_deinit(&actionRot);
    rotencoder_deinit_callback();

    cancel_repeating_timer(&timer);
    cancel_repeating_timer(&traceTimer);
    debug_trace_deinit();

    return 0;
}
