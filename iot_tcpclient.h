#ifndef _IOT_TCPCLIENT_H
#define _IOT_TCPCLIENT_H

#define TCP_MAX_STRING_LEN (256)

#define TCP_INFINITE_TMO (~((U32)0))

#define TCP_MAX_MEMBER_NAME_LEN (10 + 1)
#define TCP_IN_OUT_BUF_SIZE 256
#define TCP_MAX_PACKET_COUNT 5

/** Status callback function.
    \param data, show/hide data icon
 */
typedef void (*IOTTcpClient_Status)(bool data);

typedef struct iot_command_packet
{
    bool led;
} iot_command_packet_t;

typedef struct
{
    AllocatorIntf super;
    U8 buf[TCP_MAX_STRING_LEN];
} IOT_JParserAllocator;

typedef struct iot_tcp_client
{
    JParserIntf super;
    BufPrint out;
    JErr err;
    JEncoder encoder;
    JDecoder decoder;
    IOT_JParserAllocator pAlloc;
    JParser parser;
    int *sock;
    U8 inBuf[TCP_IN_OUT_BUF_SIZE];
    iot_command_packet_t packet;
    char outBuf[TCP_IN_OUT_BUF_SIZE];
    char memberName[TCP_MAX_MEMBER_NAME_LEN];
    IOTTcpClient_Status statusCallback;
    bool running;
} iot_tcp_client_t;

void IOT_constructor(iot_tcp_client_t *o, int *sock, IOTTcpClient_Status statusCallback);
int IOT_Send(iot_tcp_client_t *o, const char *fmt, ...);
int IOT_startMessageLoop(iot_tcp_client_t *o);
void IOT_stopMessageLoop(iot_tcp_client_t *o);

#endif