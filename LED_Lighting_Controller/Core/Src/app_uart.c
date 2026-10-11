#include "app_uart.h"

#define RX_SIZE 256U
#define TX_SIZE 2048U

static UART_HandleTypeDef *s_uart;
static uint8_t s_rxByte;
static uint8_t s_rx[RX_SIZE];
static uint8_t s_tx[TX_SIZE];
static volatile uint16_t s_rxHead, s_rxTail, s_txHead, s_txTail;
static volatile uint16_t s_txLength;
static volatile uint8_t s_txBusy, s_rxRetry, s_dropRx, s_dropSkipLf;
static volatile uint32_t s_lossPending, s_rxOverflow, s_uartErrors, s_txOverflow;

static uint32_t Lock(void)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    return saved;
}
static uint16_t NextRx(uint16_t index) { return (uint16_t)((index + 1U) % RX_SIZE); }
static uint16_t NextTx(uint16_t index) { return (uint16_t)((index + 1U) % TX_SIZE); }
static size_t TxFreeLocked(void)
{
    return (s_txTail + TX_SIZE - s_txHead - 1U) % TX_SIZE;
}

static HAL_StatusTypeDef ArmRx(void)
{
    HAL_StatusTypeDef rc = HAL_UART_Receive_IT(s_uart, &s_rxByte, 1U);
    /* For non-blocking HAL errors an existing RX can still be active. */
    s_rxRetry = (rc != HAL_OK && s_uart->RxState != HAL_UART_STATE_BUSY_RX);
    return rc;
}

static void LoseRx(void)
{
    /* Called in ISR; main's ring operations mask interrupts briefly. */
    s_rxHead = s_rxTail;
    s_dropRx = 1U;
    s_dropSkipLf = 0U;
    ++s_lossPending;
}

HAL_StatusTypeDef AppUart_Init(UART_HandleTypeDef *uart)
{
    s_uart = uart;
    s_rxHead = s_rxTail = s_txHead = s_txTail = s_txLength = 0U;
    s_txBusy = s_rxRetry = s_dropRx = s_dropSkipLf = 0U;
    s_lossPending = s_rxOverflow = s_uartErrors = s_txOverflow = 0U;
    return ArmRx();
}

void AppUart_RxComplete(UART_HandleTypeDef *uart)
{
    if (uart != s_uart) return;
    /* Preserve HAL's error until its IRQ dispatches ErrorCallback. Rearming
     * here would clear ErrorCode and could hide an overrun from that handler. */
    if (uart->ErrorCode != HAL_UART_ERROR_NONE) return;
    uint8_t byte = s_rxByte;
    if (s_dropSkipLf != 0U)
    {
        s_dropSkipLf = 0U;
        if (byte == '\n') { (void)ArmRx(); return; }
    }
    if (s_dropRx != 0U)
    {
        if (byte == '\r' || byte == '\n')
        {
            s_dropRx = 0U;
            s_dropSkipLf = (byte == '\r');
        }
    }
    else
    {
        uint16_t next = NextRx(s_rxHead);
        if (next == s_rxTail)
        {
            ++s_rxOverflow;
            LoseRx();
            /* Overflow may itself occur on the terminator. */
            if (byte == '\r' || byte == '\n')
            {
                s_dropRx = 0U;
                s_dropSkipLf = (byte == '\r');
            }
        }
        else
        {
            s_rx[s_rxHead] = byte;
            s_rxHead = next;
        }
    }
    (void)ArmRx();
}

void AppUart_Error(UART_HandleTypeDef *uart)
{
    if (uart != s_uart) return;
    ++s_uartErrors;
    LoseRx();
    (void)ArmRx();
}

void AppUart_TxComplete(UART_HandleTypeDef *uart)
{
    if (uart != s_uart || s_txBusy == 0U) return;
    s_txTail = (uint16_t)((s_txTail + s_txLength) % TX_SIZE);
    s_txBusy = 0U;
}

void AppUart_Process(void)
{
    uint32_t irq = Lock();
    if (s_rxRetry != 0U) (void)ArmRx();
    if (s_txBusy == 0U && s_txTail != s_txHead)
    {
        uint16_t length = (s_txHead > s_txTail)
                        ? (uint16_t)(s_txHead - s_txTail)
                        : (uint16_t)(TX_SIZE - s_txTail);
        s_txLength = length;
        s_txBusy = 1U;
        HAL_StatusTypeDef rc = HAL_UART_Transmit_IT(s_uart, &s_tx[s_txTail], length);
        if (rc != HAL_OK) s_txBusy = 0U;
    }
    __set_PRIMASK(irq);
}

AppUart_ReadResult AppUart_Read(uint8_t *byte)
{
    uint32_t irq = Lock();
    AppUart_ReadResult rc = APP_UART_EMPTY;
    if (s_lossPending != 0U)
    {
        --s_lossPending;
        rc = APP_UART_LOSS;
    }
    else if (s_rxHead != s_rxTail)
    {
        *byte = s_rx[s_rxTail];
        s_rxTail = NextRx(s_rxTail);
        rc = APP_UART_BYTE;
    }
    __set_PRIMASK(irq);
    return rc;
}

bool AppUart_Write(const char *data, size_t length)
{
    uint32_t irq = Lock();
    if (length > TxFreeLocked())
    {
        ++s_txOverflow;
        __set_PRIMASK(irq);
        return false;
    }
    uint16_t head = s_txHead;
    __set_PRIMASK(irq);
    /* Only main produces TX. ISR consumes already-published bytes only. */
    for (size_t i = 0U; i < length; ++i)
    {
        s_tx[head] = (uint8_t)data[i];
        head = NextTx(head);
    }
    irq = Lock();
    s_txHead = head;
    __set_PRIMASK(irq);
    return true;
}

size_t AppUart_TxFree(void)
{
    uint32_t irq = Lock();
    size_t freeBytes = TxFreeLocked();
    __set_PRIMASK(irq);
    return freeBytes;
}

void AppUart_GetStats(AppUart_Stats *out)
{
    uint32_t irq = Lock();
    out->rxOverflow = s_rxOverflow;
    out->uartErrors = s_uartErrors;
    out->txOverflow = s_txOverflow;
    __set_PRIMASK(irq);
}
