#ifndef APP_CONTROL_H
#define APP_CONTROL_H

#include "main.h"
#include "app_button.h"

typedef enum
{
    APP_MODE_MANUAL = 0,
    APP_MODE_AUTO,
    APP_MODE_PAUSA
} AppControl_Mode;

typedef struct
{
    AppControl_Mode mode;
    uint8_t appliedDuty;
    uint8_t manualDuty;
    uint16_t frequencyHz;
    uint32_t shortPressCount;
    uint32_t longPressCount;
} AppControl_Status;

void AppControl_Init(TIM_HandleTypeDef *pwmTimer);
void AppControl_Process(uint32_t nowMs);
void AppControl_HandleButton(AppButton_Event event);
void AppControl_GetStatus(AppControl_Status *out);

#endif
