#ifndef _FRAMEWORK_H
#define _FRAMEWORK_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <math.h>

#include <pico/stdlib.h>
#include <pico/util/datetime.h>
#include <pico/cyw43_arch.h>

#include <hardware/vreg.h>
#include <hardware/clocks.h>
#include <hardware/watchdog.h>
#include <hardware/rtc.h>
#include <hardware/adc.h>

#include <lwip/netif.h>
#include <lwip/ip4_addr.h>
#include <lwip/apps/lwiperf.h>
#include <lwip/sockets.h>
#include <lwip/apps/sntp.h>

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include "config.h"

#include "lib/ssd1306.h"
#include "lib/hashmap.h"

#include "lib/json/JParser.h"
#include "lib/json/JDecoder.h"
#include "lib/json/JEncoder.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

#endif