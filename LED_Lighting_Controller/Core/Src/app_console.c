#include "app_console.h"
#include "app_uart.h"
#include "app_control.h"
#include <stdio.h>
#include <string.h>

#define LINE_SIZE 64U
#define RESPONSE_SIZE 512U
#define RX_BUDGET 32U
#define STREAM_PERIOD_MS 1000U

static char s_line[LINE_SIZE];
static char s_response[RESPONSE_SIZE];
static uint8_t s_length, s_discardLine, s_invalidLine, s_skipLf, s_stream;
static uint32_t s_lastStreamMs, s_errors, s_lineOverflow;

static void Send(const char *text)
{
    /* Process reserves RESPONSE_SIZE before consuming any order. */
    (void)AppUart_Write(text, strlen(text));
}
static void Error(const char *kind)
{
    ++s_errors;
    Send(kind);
}
static const char *ModeName(AppControl_Mode mode)
{
    return mode == APP_MODE_MANUAL ? "MANUAL" : mode == APP_MODE_AUTO ? "AUTO" : "PAUSA";
}
static void SendStatus(uint32_t nowMs, const char *prefix)
{
    AppControl_Status control;
    AppUart_Stats uart;
    AppControl_GetStatus(&control);
    AppUart_GetStats(&uart);
    int length = snprintf(s_response, sizeof s_response,
        "%s MODE=%s FREQ=%u DUTY=%u MANUAL=%u UPTIME=%lu SHORT=%lu LONG=%lu "
        "ERRORS=%lu RX_OVF=%lu LINE_OVF=%lu UART_ERR=%lu TX_OVF=%lu STREAM=%s\r\n",
        prefix, ModeName(control.mode), (unsigned)control.frequencyHz,
        (unsigned)control.appliedDuty, (unsigned)control.manualDuty,
        (unsigned long)nowMs, (unsigned long)control.shortPressCount,
        (unsigned long)control.longPressCount, (unsigned long)s_errors,
        (unsigned long)uart.rxOverflow, (unsigned long)s_lineOverflow,
        (unsigned long)uart.uartErrors, (unsigned long)uart.txOverflow,
        s_stream != 0U ? "ON" : "OFF");
    if (length > 0 && (size_t)length < sizeof s_response)
        (void)AppUart_Write(s_response, (size_t)length);
}

static unsigned Split(char *tokens[3])
{
    unsigned count = 0U;
    char *cursor = s_line;
    while (*cursor != '\0')
    {
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (*cursor == '\0') break;
        if (count == 3U) return 4U;
        tokens[count++] = cursor;
        while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') ++cursor;
        if (*cursor != '\0') *cursor++ = '\0';
    }
    return count;
}

typedef enum { NUMBER_OK, NUMBER_ARG, NUMBER_RANGE } NumberResult;
static NumberResult Number(const char *text, uint32_t *value)
{
    if (*text == '\0') return NUMBER_ARG;
    /* Validate every character before distinguishing numeric overflow. */
    for (const char *p = text; *p != '\0'; ++p)
        if (*p < '0' || *p > '9') return NUMBER_ARG;
    uint32_t number = 0U;
    for (const char *p = text; *p != '\0'; ++p)
    {
        uint32_t digit = (uint32_t)(*p - '0');
        if (number > (UINT32_MAX - digit) / 10U) return NUMBER_RANGE;
        number = number * 10U + digit;
    }
    *value = number;
    return NUMBER_OK;
}

static void ControlResponse(AppControl_Result result)
{
    if (result == APP_CONTROL_OK) Send("OK\r\n");
    else Error(result == APP_CONTROL_STATE ? "ERR STATE\r\n" : "ERR RANGE\r\n");
}

static void Execute(uint32_t nowMs)
{
    char *tokens[3];
    unsigned count = Split(tokens);
    if (count == 0U) { Error("ERR COMMAND\r\n"); return; }
    const char *cmd = tokens[0];
    if (strcmp(cmd, "HELP") == 0 || strcmp(cmd, "STATUS") == 0 ||
        strcmp(cmd, "PAUSE") == 0 || strcmp(cmd, "RESUME") == 0)
    {
        if (count != 1U) { Error("ERR ARG\r\n"); return; }
        if (strcmp(cmd, "HELP") == 0)
            Send("OK HELP\r\nHELP | STATUS | MODE MANUAL | MODE AUTO\r\n"
                 "DUTY 0..100 | FREQ 500/1000/2000\r\n"
                 "PAUSE | RESUME | STREAM ON | STREAM OFF\r\n");
        else if (strcmp(cmd, "STATUS") == 0) SendStatus(nowMs, "OK");
        else if (strcmp(cmd, "PAUSE") == 0) ControlResponse(AppControl_Pause(nowMs));
        else ControlResponse(AppControl_Resume(nowMs));
        return;
    }
    if (strcmp(cmd, "MODE") == 0 || strcmp(cmd, "DUTY") == 0 ||
        strcmp(cmd, "FREQ") == 0 || strcmp(cmd, "STREAM") == 0)
    {
        if (count != 2U) { Error("ERR ARG\r\n"); return; }
        if (strcmp(cmd, "STREAM") == 0)
        {
            if (strcmp(tokens[1], "ON") == 0) s_stream = 1U;
            else if (strcmp(tokens[1], "OFF") == 0) s_stream = 0U;
            else { Error("ERR ARG\r\n"); return; }
            s_lastStreamMs = nowMs;
            Send("OK\r\n");
            return;
        }
        AppControl_Status current;
        AppControl_GetStatus(&current);
        if (current.mode == APP_MODE_PAUSA ||
            (strcmp(cmd, "DUTY") == 0 && current.mode != APP_MODE_MANUAL))
        { Error("ERR STATE\r\n"); return; }
        if (strcmp(cmd, "MODE") == 0)
        {
            if (strcmp(tokens[1], "MANUAL") == 0)
                ControlResponse(AppControl_SetMode(APP_MODE_MANUAL, nowMs));
            else if (strcmp(tokens[1], "AUTO") == 0)
                ControlResponse(AppControl_SetMode(APP_MODE_AUTO, nowMs));
            else Error("ERR ARG\r\n");
            return;
        }
        uint32_t number;
        NumberResult parsed = Number(tokens[1], &number);
        if (parsed != NUMBER_OK)
        { Error(parsed == NUMBER_ARG ? "ERR ARG\r\n" : "ERR RANGE\r\n"); return; }
        ControlResponse(strcmp(cmd, "DUTY") == 0
            ? AppControl_SetDuty(number) : AppControl_SetFrequency(number));
        return;
    }
    Error("ERR COMMAND\r\n");
}

void AppConsole_Init(uint32_t nowMs)
{
    s_length = s_discardLine = s_invalidLine = s_skipLf = s_stream = 0U;
    s_lastStreamMs = nowMs;
    s_errors = s_lineOverflow = 0U;
    Send("OK READY MANUAL FREQ=1000 DUTY=25 STREAM=OFF\r\n");
}

void AppConsole_Process(uint32_t nowMs)
{
    /* Backpressure reserves a complete response before executing any command. */
    for (unsigned budget = 0U; budget < RX_BUDGET; ++budget)
    {
        if (AppUart_TxFree() < RESPONSE_SIZE) break;
        uint8_t byte;
        AppUart_ReadResult result = AppUart_Read(&byte);
        if (result == APP_UART_EMPTY) break;
        if (result == APP_UART_LOSS)
        {
            s_length = s_discardLine = s_invalidLine = s_skipLf = 0U;
            Error("ERR OVERFLOW\r\n");
            continue;
        }
        if (s_skipLf != 0U)
        {
            s_skipLf = 0U;
            if (byte == '\n') continue;
        }
        if (byte == '\r' || byte == '\n')
        {
            s_skipLf = (byte == '\r');
            if (s_discardLine != 0U)
            {
                ++s_lineOverflow;
                Error("ERR OVERFLOW\r\n");
            }
            else if (s_invalidLine != 0U) Error("ERR COMMAND\r\n");
            else
            {
                s_line[s_length] = '\0';
                Execute(nowMs);
            }
            s_length = s_discardLine = s_invalidLine = 0U;
            break; /* At most one complete line per call. */
        }
        if (s_discardLine != 0U) continue;
        if (s_length == LINE_SIZE - 1U) { s_discardLine = 1U; continue; }
        if ((byte < 32U && byte != '\t') || byte > 126U) s_invalidLine = 1U;
        s_line[s_length++] = (char)byte;
    }
    if (s_stream != 0U && (uint32_t)(nowMs - s_lastStreamMs) >= STREAM_PERIOD_MS)
    {
        if (AppUart_TxFree() >= RESPONSE_SIZE)
        {
            s_lastStreamMs = nowMs;
            SendStatus(nowMs, "DATA");
        }
    }
}
