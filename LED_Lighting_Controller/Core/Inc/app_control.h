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

typedef enum { APP_CONTROL_OK, APP_CONTROL_STATE, APP_CONTROL_RANGE } AppControl_Result;

void AppControl_Init(TIM_HandleTypeDef *pwmTimer);
void AppControl_Process(uint32_t nowMs);
void AppControl_HandleButton(AppButton_Event event);
void AppControl_GetStatus(AppControl_Status *out);
AppControl_Result AppControl_SetMode(AppControl_Mode mode, uint32_t nowMs);
AppControl_Result AppControl_SetDuty(uint32_t duty);
AppControl_Result AppControl_SetFrequency(uint32_t frequency);
AppControl_Result AppControl_Pause(uint32_t nowMs);
AppControl_Result AppControl_Resume(uint32_t nowMs);

#endif
