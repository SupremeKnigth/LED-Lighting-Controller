#ifndef APP_UART_H
#define APP_UART_H
#include "main.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum { APP_UART_EMPTY, APP_UART_BYTE, APP_UART_LOSS } AppUart_ReadResult;
typedef struct
{
    uint32_t rxOverflow;
    uint32_t uartErrors;
    uint32_t txOverflow;
} AppUart_Stats;

HAL_StatusTypeDef AppUart_Init(UART_HandleTypeDef *uart);
void AppUart_Process(void);
AppUart_ReadResult AppUart_Read(uint8_t *byte);
bool AppUart_Write(const char *data, size_t length);
size_t AppUart_TxFree(void);
void AppUart_GetStats(AppUart_Stats *out);
/* Dispatch only from the HAL callbacks for the configured USART. */
void AppUart_RxComplete(UART_HandleTypeDef *uart);
void AppUart_TxComplete(UART_HandleTypeDef *uart);
void AppUart_Error(UART_HandleTypeDef *uart);
#endif
