#include "../framework.h"
#include "debug.h"

void print_buf(const uint8_t *buf, size_t len)
{
    printf("\n");
    for (size_t i = 0; i < len; ++i)
    {
        printf("%02x", buf[i]);
        if (i % 16 == 15)
            printf("\n");
        else
            printf(" ");
    }
    printf("\n");
}

static char *prvWriteNameToBuffer(char *pcBuffer,
                                  const char *pcTaskName)
{
    size_t x;

    /* Start by copying the entire string. */
    strcpy(pcBuffer, pcTaskName);

    /* Pad the end of the string with spaces to ensure columns line up when
     * printed out. */
    for (x = strlen(pcBuffer); x < (size_t)(configMAX_TASK_NAME_LEN - 1); x++)
    {
        pcBuffer[x] = ' ';
    }

    /* Terminate. */
    pcBuffer[x] = (char)0x00;

    /* Return the new end of string. */
    return &(pcBuffer[x]);
}

static void prvWriteNameToBufferDynamic(char *pcBuffer,
                                        const char *pcName, size_t len)
{
    size_t x;

    /* Start by copying the entire string. */
    strcpy(pcBuffer, pcName);

    /* Pad the end of the string with spaces to ensure columns line up when
     * printed out. */
    for (x = strlen(pcBuffer); x < len - 1; x++)
    {
        pcBuffer[x] = ' ';
    }

    /* Terminate. */
    pcBuffer[x] = (char)0x00;
}

void vTaskGetRunTimeStats_fixed(char *pcWriteBuffer)
{
    TaskStatus_t *pxTaskStatusArray;
    UBaseType_t uxArraySize, x;
    uint32_t ulTotalTime, ulStatsAsPercentage;

    /* Make sure the write buffer does not contain a string. */
    *pcWriteBuffer = (char)0x00;

    /* Take a snapshot of the number of tasks in case it changes while this
     * function is executing. */
    uxArraySize = uxTaskGetNumberOfTasks();

    /* Allocate an array index for each task.  NOTE!  If
     * configSUPPORT_DYNAMIC_ALLOCATION is set to 0 then pvPortMalloc() will
     * equate to NULL. */
    pxTaskStatusArray = pvPortMalloc(uxArraySize * sizeof(TaskStatus_t)); /*lint !e9079 All values returned by pvPortMalloc() have at least the alignment required by the MCU's stack and this allocation allocates a struct that has the alignment requirements of a pointer. */

    if (pxTaskStatusArray != NULL)
    {
        /* Generate the (binary) data. */
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalTime);

        /* Avoid divide by zero errors. */
        if (ulTotalTime > 0UL)
        {
            /* Create a human readable table from the binary data. */
            for (x = 0; x < uxArraySize; x++)
            {
                /* What percentage of the total run time has the task used?
                 * This will always be rounded down to the nearest integer.
                 */
                ulStatsAsPercentage = (pxTaskStatusArray[x].ulRunTimeCounter * 100) /* For percentage calculations. */
                                      / ulTotalTime;

                /* Write the task name to the string, padding with
                 * spaces so it can be printed in tabular form more
                 * easily. */
                pcWriteBuffer = prvWriteNameToBuffer(pcWriteBuffer, pxTaskStatusArray[x].pcTaskName);

                if (ulStatsAsPercentage > 0UL)
                {
#ifdef portLU_PRINTF_SPECIFIER_REQUIRED
                    {
                        sprintf(pcWriteBuffer, "\t%lu\t\t%lu%%\r\n", pxTaskStatusArray[x].ulRunTimeCounter, ulStatsAsPercentage);
                    }
#else
                    {
                        /* sizeof( int ) == sizeof( long ) so a smaller
                         * printf() library can be used. */
                        sprintf(pcWriteBuffer, "\t%u\t\t%u%%\r\n", (unsigned int)pxTaskStatusArray[x].ulRunTimeCounter, (unsigned int)ulStatsAsPercentage); /*lint !e586 sprintf() allowed as this is compiled with many compilers and this is a utility function only - not part of the core kernel implementation. */
                    }
#endif
                }
                else
                {
/* If the percentage is zero here then the task has
 * consumed less than 1% of the total run time. */
#ifdef portLU_PRINTF_SPECIFIER_REQUIRED
                    {
                        sprintf(pcWriteBuffer, "\t%lu\t\t<1%%\r\n", pxTaskStatusArray[x].ulRunTimeCounter);
                    }
#else
                    {
                        /* sizeof( int ) == sizeof( long ) so a smaller
                         * printf() library can be used. */
                        sprintf(pcWriteBuffer, "\t%u\t\t<1%%\r\n", (unsigned int)pxTaskStatusArray[x].ulRunTimeCounter); /*lint !e586 sprintf() allowed as this is compiled with many compilers and this is a utility function only - not part of the core kernel implementation. */
                    }
#endif
                }

                pcWriteBuffer += strlen(pcWriteBuffer); /*lint !e9016 Pointer arithmetic ok on char pointers especially as in this case where it best denotes the intent of the code. */
            }
        }
        else
        {
            mtCOVERAGE_TEST_MARKER();
        }

        /* Free the array again.  NOTE!  If configSUPPORT_DYNAMIC_ALLOCATION
         * is 0 then vPortFree() will be #defined to nothing. */
        vPortFree(pxTaskStatusArray);
    }
    else
    {
        mtCOVERAGE_TEST_MARKER();
    }
}

void __short_task_swap(short_task_desc_t *a, short_task_desc_t *b)
{
    short_task_desc_t temp = *a;
    *a = *b;
    *b = temp;
}

void __short_task_sort(short_task_desc_t *arr, int n)
{
    int i, j, min_idx;

    for (i = 0; i < n - 1; i++)
    {
        min_idx = i;
        for (j = i + 1; j < n; j++)
            if (arr[j].cpuUsed > arr[min_idx].cpuUsed)
                min_idx = j;

        __short_task_swap(&arr[min_idx], &arr[i]);
    }
}

short_task_desc_t *vTaskGetRunTimeStatsShort(size_t taskCount)
{
    short_task_desc_t *tasks = pvPortMalloc(taskCount * sizeof(short_task_desc_t));
    TaskStatus_t *pxTaskStatusArray;
    UBaseType_t uxArraySize, x;
    uint32_t ulTotalTime, ulStatsAsPercentage;

    uxArraySize = uxTaskGetNumberOfTasks();

    pxTaskStatusArray = pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));

    if (pxTaskStatusArray != NULL)
    {
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalTime);

        if (ulTotalTime > 0UL)
        {
            for (x = 0; x < min(uxArraySize, taskCount); x++)
            {
                /* What percentage of the total run time has the task used?
                 * This will always be rounded down to the nearest integer.
                 */
                ulStatsAsPercentage = (pxTaskStatusArray[x].ulRunTimeCounter * 100) /* For percentage calculations. */
                                      / ulTotalTime;

                /* Write the task name to the string, padding with
                 * spaces so it can be printed in tabular form more
                 * easily. */
                short_task_desc_t desc = {};
                desc.name = pxTaskStatusArray[x].pcTaskName;
                desc.cpuUsed = ulStatsAsPercentage;
                tasks[x] = desc;
            }
        }
        else
        {
            mtCOVERAGE_TEST_MARKER();
        }

        /* Free the array again.  NOTE!  If configSUPPORT_DYNAMIC_ALLOCATION
         * is 0 then vPortFree() will be #defined to nothing. */
        vPortFree(pxTaskStatusArray);
    }
    else
    {
        mtCOVERAGE_TEST_MARKER();
    }

    __short_task_sort(tasks, taskCount);
    return tasks;
}

void vTaskPrintRunTimeStats()
{
    puts("~~~~~~~~~~~~~  DUMP  ~~~~~~~~~~~~~~~\n");
    char buffer[512];
    vTaskGetRunTimeStats_fixed(buffer);
    puts(buffer);
    puts("~~~~~~~~~~~~~  STOP  ~~~~~~~~~~~~~~~\n");
}

int debug_trace_entry_compare(const void *a, const void *b, void *udata)
{
    const debug_trace_entry_t *ua = a;
    const debug_trace_entry_t *ub = b;
    return strcmp(ua->name, ub->name);
}

uint64_t debug_trace_entry_hash(const void *item, uint64_t seed0, uint64_t seed1)
{
    const debug_trace_entry_t *entry = item;
    return hashmap_sip(entry->name, strlen(entry->name), seed0, seed1);
}

struct hashmap *debug_trace_entry_map;

void debug_trace_init()
{
    debug_trace_entry_map = hashmap_new(sizeof(debug_trace_entry_t), 30, 0, 0,
                                        debug_trace_entry_hash, debug_trace_entry_compare, NULL, NULL);
}

void debug_trace_deinit()
{
    hashmap_free(debug_trace_entry_map);
}

void debug_trace_update_entries()
{
    unsigned long currentTime = portGET_RUN_TIME_COUNTER_VALUE();
    size_t iter = 0;
    void *item;
    while (hashmap_iter(debug_trace_entry_map, &iter, &item))
    {
        debug_trace_entry_t *entry = item;
        unsigned long length = currentTime - entry->startTime;
        if (!entry->inside && length > DEBUG_RECENT_LENGTH) // irrelevant trace points
        {
            hashmap_delete(debug_trace_entry_map, item);
            iter = 0;
        }
    }
}

void debug_trace_start(const char *name)
{
    hashmap_set(debug_trace_entry_map, &(debug_trace_entry_t){.name = name, .startTime = portGET_RUN_TIME_COUNTER_VALUE(), .inside = true});
}

void debug_trace_end(const char *name)
{
    debug_trace_entry_t *entry = hashmap_get(debug_trace_entry_map, &(debug_trace_entry_t){.name = name});
    hashmap_set(debug_trace_entry_map, &(debug_trace_entry_t){.name = name, .startTime = entry->startTime, .inside = false});
}

void debug_trace_print_entries()
{
    size_t size = hashmap_count(debug_trace_entry_map);
    printf("Total hashmap size: %u\n", size);
    unsigned long currentTime = portGET_RUN_TIME_COUNTER_VALUE();
    size_t iter = 0;
    void *item;
    char nameBuffer[64];
    printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    while (hashmap_iter(debug_trace_entry_map, &iter, &item))
    {
        debug_trace_entry_t *entry = item;
        unsigned long length = currentTime - entry->startTime;
        if (entry->inside || length <= DEBUG_RECENT_LENGTH) // possibly important trace points
        {
            prvWriteNameToBufferDynamic(nameBuffer, entry->name, 64);
            printf("%s\t%u\t\t%s\r\n", nameBuffer, length, entry->inside ? "INSIDE" : "RECENT");
        }
    }
    printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
}