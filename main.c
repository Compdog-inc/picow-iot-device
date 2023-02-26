#include "utils/kernel.h"
#include "framework.h"
#include "utils/getline.h"
#include "utils/non_volatile_mem.h"
#include "utils/debug.h"
#include "lib/acme_5_outlines_font.h"
#include "lib/BMSPA_font.h"
#include "iot_tcpclient.h"

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

#include "icons/panic_code.h"
#pragma endregion

#define ACTION_BTN 15
#define ACTION_BTN_DOWN !gpio_get(ACTION_BTN)
#define PANIC_BTN 16
#define PANIC_BTN_DOWN !gpio_get(PANIC_BTN)

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

SemaphoreHandle_t dispMut; // I2C is not thread safe
ssd1306_t disp;
uint32_t logCounter = 0;
#define LOG_LINE_COUNT 7
const char *logLines[LOG_LINE_COUNT];
#define GET_ROLLING_BUFFER(buf, size, ind, off) ((buf)[((ind) + (off)) % (size)])

iot_tcp_client_t client;
bool clientInitialized = false;

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

        ssd1306_bmp_show_image_with_offset(&disp, panic_code_bmp_data, panic_code_bmp_size, disp.width - 37 - 2, 18, false);

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

static void ui_task(__unused void *params)
{
    C_START("ui_task");

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

    int page = -1;
    bool prevDown = false;

    while (client.running)
    {
        bool down = ACTION_BTN_DOWN;
        if (page == -1 || (down && !prevDown))
        {
            if (++page > SCREEN_PAGE_ABOUT)
                page = SCREEN_PAGE_TIME;

            printf("[UI] Switching to page: %s\n", SCREEN_PAGE_STR(page));

            if (dispMut != NULL)
            {
                if (!xSemaphoreTake(dispMut, 100))
                    printf("[UI] WARNING! Unsafe display access\n");
                else
                {
                    ssd1306_draw_square(&disp, 0, 10, disp.width, disp.height - 10, false); // clear client area

                    switch (page)
                    {
                    case SCREEN_PAGE_TIME:
                    {
                        // total width: 24+3+51+3+3 = 84
                        // total height: 24
                        // center X: 22
                        // center Y: 20
                        ssd1306_draw_string_with_font(&disp, 22, 20, 3, BMSPA_font, "2", true);
                        ssd1306_draw_string_with_font(&disp, 49, 20, 3, BMSPA_font, ":", true);
                        ssd1306_draw_string_with_font(&disp, 55, 20, 3, BMSPA_font, "09", true);
                        break;
                    }
                    }

                    ssd1306_show(&disp);
                    xSemaphoreGive(dispMut);
                }
            }
        }
        prevDown = down;
        vTaskDelay(10);
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

    TaskHandle_t tcpTask;
    debugLog("[MAIN] Starting TCP client task", "Starting client.");
    xTaskCreate(tcp_task, "TCPThread", configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 2UL), &tcpTask);

    while (!clientInitialized) // wait for iot client
    {
        vTaskDelay(10);
    }

    TaskHandle_t uiTask;
    debugLog("[MAIN] Starting UI task", "Starting UI.");
    xTaskCreate(ui_task, "UIThread", configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 2UL), &uiTask);

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

    vInitScreen();
    initTaskTimer(&timer);
    initTraceTimer(&traceTimer);

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

    cancel_repeating_timer(&timer);
    cancel_repeating_timer(&traceTimer);
    debug_trace_deinit();

    return 0;
}
