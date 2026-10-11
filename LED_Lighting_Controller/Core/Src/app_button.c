#include "app_button.h"
#include "main.h"

#define DEBOUNCE_MS     30U
#define LONG_PRESS_MS 1500U
#define SHORT_MIN_MS   30U


#define BUTTON_PORT   B1_GPIO_Port
#define BUTTON_PIN    B1_Pin
#define BUTTON_ACTIVE GPIO_PIN_RESET

static volatile uint8_t s_edgePending;
static uint8_t s_debounceActive;
static uint8_t s_candidatePressed;
static uint8_t s_stablePressed;
static uint8_t s_longReported;
static uint32_t s_candidateSinceMs;
static uint32_t s_pressSinceMs;

static uint8_t Button_IsPressed(void)
{
    return (HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN) == BUTTON_ACTIVE) ? 1U : 0U;
}

static uint8_t Button_TakeEdge(void)
{
    uint32_t irqState = __get_PRIMASK();
    __disable_irq();
    uint8_t pending = s_edgePending;
    s_edgePending = 0U;
    __set_PRIMASK(irqState);
    return pending;
}

void AppButton_Init(uint32_t nowMs)
{
    s_edgePending = 0U;
    s_debounceActive = 0U;
    s_stablePressed = Button_IsPressed();
    s_candidatePressed = s_stablePressed;
    s_longReported = 0U;
    s_candidateSinceMs = nowMs;
    s_pressSinceMs = nowMs;
}

void AppButton_NotifyEdge(void)
{
    s_edgePending = 1U;
}

AppButton_Event AppButton_Process(uint32_t nowMs)
{
    AppButton_Event result = APP_BUTTON_NONE;
    uint8_t edgePending = Button_TakeEdge();
    uint8_t rawPressed = Button_IsPressed();


    if (edgePending != 0U)
    {
        /* Also restart if bounce returned to the same sampled level. */
        s_candidateSinceMs = nowMs;
        s_debounceActive = 1U;
    }

    if (rawPressed != s_candidatePressed)
    {
        s_candidatePressed = rawPressed;
        s_candidateSinceMs = nowMs;
        s_debounceActive = 1U;
    }

    if ((s_debounceActive != 0U) &&
        ((uint32_t)(nowMs - s_candidateSinceMs) >= DEBOUNCE_MS))
    {
        s_debounceActive = 0U;

        if (s_candidatePressed != s_stablePressed)
        {
            s_stablePressed = s_candidatePressed;

            if (s_stablePressed != 0U)
            {
                s_pressSinceMs = nowMs;
                s_longReported = 0U;
            }
            else
            {
                uint32_t pressDuration = (uint32_t)(nowMs - s_pressSinceMs);
                if ((s_longReported == 0U) &&
                    (pressDuration >= SHORT_MIN_MS) &&
                    (pressDuration < LONG_PRESS_MS))
                {
                    result = APP_BUTTON_SHORT;
                }
                else if ((s_longReported == 0U) &&
                         (pressDuration >= LONG_PRESS_MS))
                {
                    /* Release may validate on the exact long-press tick. */
                    s_longReported = 1U;
                    result = APP_BUTTON_LONG;
                }
            }
        }
    }

    if ((s_stablePressed != 0U) && (s_longReported == 0U) &&
        ((uint32_t)(nowMs - s_pressSinceMs) >= LONG_PRESS_MS))
    {
        s_longReported = 1U;
        result = APP_BUTTON_LONG;
    }

    return result;
}
