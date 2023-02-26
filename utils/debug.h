#ifndef _DEBUG_H
#define _DEBUG_H

#define DEBUG_LEVEL_NONE 0
#define DEBUG_LEVEL_CRITICAL 1
#define DEBUG_LEVEL_INFO 2
#define DEBUG_LEVEL_FULL 3

#define DEBUG_RECENT_LENGTH 50

typedef struct short_task_desc
{
    const char *name;
    uint32_t cpuUsed;
} short_task_desc_t;

void print_buf(const uint8_t *buf, size_t len);

void vTaskPrintRunTimeStats();
short_task_desc_t *vTaskGetRunTimeStatsShort(size_t taskCount);

typedef struct debug_trace_entry
{
    const char *name;
    unsigned long startTime;
    bool inside;
} debug_trace_entry_t;

void debug_trace_init();
void debug_trace_deinit();
void debug_trace_update_entries();
void debug_trace_start(const char *name);
void debug_trace_end(const char *name);
void debug_trace_print_entries();

#if DEBUG_LEVEL >= DEBUG_LEVEL_FULL
#define F_START(n) debug_trace_start(n)
#define F_END(n) debug_trace_end(n)
#define F_RETURNV(n, v) \
    {                   \
        F_END(n);       \
        return v;       \
    }

#define F_RETURN(n) \
    {               \
        F_END(n);   \
        return;     \
    }
#else
#define F_START(n) \
    {              \
    }
#define F_END(n) \
    {            \
    }
#define F_RETURNV(n, v) \
    {                   \
        return v;       \
    }

#define F_RETURN(n) \
    {               \
        return;     \
    }
#endif

#if DEBUG_LEVEL >= DEBUG_LEVEL_INFO
#define I_START(n) debug_trace_start(n)
#define I_END(n) debug_trace_end(n)
#define I_RETURNV(n, v) \
    {                   \
        I_END(n);       \
        return v;       \
    }

#define I_RETURN(n) \
    {               \
        I_END(n);   \
        return;     \
    }
#else
#define I_START(n) \
    {              \
    }
#define I_END(n) \
    {            \
    }
#define I_RETURNV(n, v) \
    {                   \
        return v;       \
    }

#define I_RETURN(n) \
    {               \
        return;     \
    }
#endif

#if DEBUG_LEVEL >= DEBUG_LEVEL_CRITICAL
#define C_START(n) debug_trace_start(n)
#define C_END(n) debug_trace_end(n)
#define C_RETURNV(n, v) \
    {                   \
        C_END(n);       \
        return v;       \
    }

#define C_RETURN(n) \
    {               \
        C_END(n);   \
        return;     \
    }
#else
#define C_START(n) \
    {              \
    }
#define C_END(n) \
    {            \
    }
#define C_RETURNV(n, v) \
    {                   \
        return v;       \
    }

#define C_RETURN(n) \
    {               \
        return;     \
    }
#endif

#endif