#ifndef APP_BUTTON_H
#define APP_BUTTON_H

#include <stdint.h>

typedef enum
{
    APP_BUTTON_NONE = 0,
    APP_BUTTON_SHORT,
    APP_BUTTON_LONG
} AppButton_Event;

void AppButton_Init(uint32_t nowMs);
void AppButton_NotifyEdge(void);
AppButton_Event AppButton_Process(uint32_t nowMs);

#endif
