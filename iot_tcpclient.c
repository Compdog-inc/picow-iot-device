#include "framework.h"
#include "utils/debug.h"
#include "iot_tcpclient.h"

int send_buffer(iot_tcp_client_t *o, char *buf, int size)
{
    I_START("send_buffer");
    o->statusCallback(true);

    int done = 0;
    while (done < size)
    {
        int done_now = send(*(o->sock), buf + done, size - done, 0);
        if (done_now <= 0)
        {
            o->statusCallback(false);
            I_RETURNV("send_buffer", done_now);
        }
        done += done_now;
    }

    o->statusCallback(false);
    I_RETURNV("send_buffer", 0);
}

int readtmo(int sock, U32 tmo)
{
    F_START("readtmo");
    fd_set recSet;
    struct timeval tv;
    tv.tv_sec = tmo / 1000;
    tv.tv_usec = (tmo % 1000) * 1000;
    FD_ZERO(&recSet);
    FD_SET(sock, &recSet);
    F_RETURNV("readtmo", select(sock + 1, &recSet, 0, 0, &tv) > 0 ? 0 : -1);
}

int receive_buffer(int sock, void *buf, U32 len, U32 timeout)
{
    F_START("receive_buffer");
    int recLen;
    if (timeout != TCP_INFINITE_TMO)
    {
        if (readtmo(sock, timeout))
            F_RETURNV("receive_buffer", 0);
    }

    recLen = recv(sock, buf, len, 0);
    if (recLen <= 0)
    {
        F_RETURNV("receive_buffer", -1);
    }
    F_RETURNV("receive_buffer", recLen);
}

#define IOT_JParserAllocator_constructor(o)                 \
    AllocatorIntf_constructor((AllocatorIntf *)o,           \
                              IOT_JParserAllocator_malloc,  \
                              IOT_JParserAllocator_realloc, \
                              IOT_JParserAllocator_free)

void *
IOT_JParserAllocator_malloc(AllocatorIntf *super, size_t *size)
{
    IOT_JParserAllocator *o = (IOT_JParserAllocator *)super;
    return *size <= TCP_MAX_STRING_LEN ? o->buf : 0;
}

void *
IOT_JParserAllocator_realloc(AllocatorIntf *super, void *memblock, size_t *size)
{
    IOT_JParserAllocator *o = (IOT_JParserAllocator *)super;
    baAssert(memblock == o->buf);
    return *size <= TCP_MAX_STRING_LEN ? o->buf : 0;
}

void IOT_JParserAllocator_free(AllocatorIntf *super, void *memblock)
{
    /* Do nothing */
    (void)super;
    (void)memblock;
}

int BufPrint_sockWrite(BufPrint *o, int sizeRequired)
{
    I_START("BufPrint_sockWrite");
    int status;
    (void)sizeRequired; /* Not used */
    /* Send JSON data to server */
    status = send_buffer((iot_tcp_client_t *)(o->userData), o->buf, o->cursor);
    o->cursor = 0; /* Data flushed */
    if (status < 0)
    {
        printf("Socket closed on write\n");
        F_RETURNV("BufPrint_sockWrite", status);
    }
    F_RETURNV("BufPrint_sockWrite", 0);
}

int TCP_parserCallback(JParserIntf *super, JParserVal *v, int nLevel)
{
    F_START("TCP_parserCallback");
    iot_tcp_client_t *o = (iot_tcp_client_t *)super;
    F_RETURNV("TCP_parserCallback", JParserIntf_serviceCB((JParserIntf *)&o->decoder, v, nLevel));
}

int TCP_manage(iot_tcp_client_t *o, U8 *data, U32 dsize)
{
    F_START("TCP_manage");
    int status;
    do
    {
        if (JDecoder_get(&o->decoder, "{b}",
                         JD_MNUM(&(o->packet), led)))
        {
            F_RETURNV("TCP_manage", 1);
        }
        status = JParser_parse(&o->parser, data, dsize);
        if (status)
        {
            if (status > 0)
            {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, o->packet.led);
                status = 0;
            }
            else
            {
                printf("JParser or parser callback error: %d\n", JParser_getStatus(&o->parser));
            }
        }
    } while (status == 0 && JParser_getStatus(&o->parser) == JParsStat_Done);
    F_RETURNV("TCP_manage", status);
}

void IOT_constructor(iot_tcp_client_t *o, int *sock, IOTTcpClient_Status statusCallback)
{
    I_START("IOT_constructor");
    JParserIntf_constructor((JParserIntf *)o, TCP_parserCallback);
    BufPrint_constructor2(&o->out, o->outBuf, TCP_IN_OUT_BUF_SIZE, o, BufPrint_sockWrite);
    JErr_constructor(&o->err);
    JEncoder_constructor(&o->encoder, &o->err, &o->out);
    JDecoder_constructor(&o->decoder, o->inBuf, TCP_IN_OUT_BUF_SIZE, 0);
    IOT_JParserAllocator_constructor(&o->pAlloc);
    JParser_constructor(&o->parser, (JParserIntf *)o, o->memberName,
                        TCP_MAX_MEMBER_NAME_LEN, (AllocatorIntf *)&o->pAlloc, 0);
    o->sock = sock;
    o->statusCallback = statusCallback;
    I_END("IOT_constructor");
}

int IOT_Send(iot_tcp_client_t *o, const char *fmt, ...)
{
    I_START("IOT_Send");
    if (JParser_getStatus(&o->parser) != JParsStat_NeedMoreData)
    {
        int retVal;
        va_list varg;
        va_start(varg, fmt);
        retVal = JEncoder_vSetJV(&o->encoder, &fmt, &varg);
        if (retVal) /* Can only set error once. Just in case not set */
            JErr_setError((&o->encoder)->err, JErrT_FmtValErr, "?");
        va_end(varg);
        I_RETURNV("IOT_Send", JErr_isError(&o->err) || JEncoder_commit(&o->encoder) ? -1 : 0);
    }
    I_RETURNV("IOT_Send", 0);
}

int sendError(iot_tcp_client_t *o, int error)
{
    I_START("sendError");
    if (JParser_getStatus(&o->parser) != JParsStat_NeedMoreData)
    {
        JEncoder_beginObject(&o->encoder);

        JEncoder_setName(&o->encoder, "message");
        JEncoder_setString(&o->encoder, "Server does not follow strict API rules.");

        JEncoder_setName(&o->encoder, "error");
        JEncoder_setInt(&o->encoder, error);

        JEncoder_endObject(&o->encoder);
        I_RETURNV("sendError", JErr_isError(&o->err) || JEncoder_commit(&o->encoder) ? -1 : 0);
    }
    I_RETURNV("sendError", 0);
}

int IOT_startMessageLoop(iot_tcp_client_t *o)
{
    C_START("IOT_startMessageLoop");
    o->running = true;
    int rc, status = -1;
    U8 *buf = pvPortMalloc(TCP_IN_OUT_BUF_SIZE);
    while ((rc = receive_buffer(*o->sock, buf, TCP_IN_OUT_BUF_SIZE, 50)) >= 0 && o->running)
    {
        if (rc)
        {
            if ((status = TCP_manage(o, buf, rc)) != 0)
            {
                sendError(o, status);
                break;
            }
        }
    }

    vPortFree(buf);
    C_RETURNV("IOT_startMessageLoop", status);
}

void IOT_stopMessageLoop(iot_tcp_client_t *o)
{
    C_START("IOT_stopMessageLoop");
    o->running = false;
    C_END("IOT_stopMessageLoop");
}