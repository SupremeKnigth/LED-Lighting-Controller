#include "app_control.h"

#define PWM_CHANNEL          TIM_CHANNEL_1
#define AUTO_STEP_MS         20U
#define AUTO_CYCLE_STEPS    160U
#define BLINK_AUTO_MS       500U
#define BLINK_PAUSE_MS      125U


static TIM_HandleTypeDef *s_pwmTimer;
static AppControl_Mode s_mode;
static AppControl_Mode s_resumeMode;
static uint8_t s_manualDuty;
static uint8_t s_appliedDuty;
static uint16_t s_frequencyHz;
static uint32_t s_timerClockHz;
static uint32_t s_autoPhase;
static uint32_t s_autoSubMs;
static uint32_t s_ledSubMs;
static uint32_t s_lastMs;
static uint32_t s_shortPressCount;
static uint32_t s_longPressCount;

static void Control_ApplyDuty(uint8_t duty)
{

    uint32_t periodTicks = __HAL_TIM_GET_AUTORELOAD(s_pwmTimer) + 1U;
    uint32_t pulseTicks = ((uint32_t)duty * periodTicks) / 100U;
    __HAL_TIM_SET_COMPARE(s_pwmTimer, PWM_CHANNEL, pulseTicks);
    s_appliedDuty = duty;
}

static void Control_ResetStatusLed(void)
{
    s_ledSubMs = 0U;
    HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_SET);
}

static uint8_t Control_AutoDuty(void)
{
    return (uint8_t)((s_autoPhase <= 80U)
            ? (10U + s_autoPhase)
            : (170U - s_autoPhase));
}

void AppControl_Init(TIM_HandleTypeDef *pwmTimer)
{
    s_pwmTimer = pwmTimer;
    s_mode = APP_MODE_MANUAL;
    s_resumeMode = APP_MODE_MANUAL;
    s_manualDuty = 25U;
    s_appliedDuty = 0U;
    s_frequencyHz = 1000U;
    s_timerClockHz = HAL_RCC_GetPCLK1Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0U) s_timerClockHz *= 2U;
    /* Buffer period changes together with the preloaded compare register. */
    SET_BIT(s_pwmTimer->Instance->CR1, TIM_CR1_ARPE);
    s_autoPhase = 0U;
    s_autoSubMs = 0U;
    s_ledSubMs = 0U;
    s_lastMs = 0U;
    s_shortPressCount = 0U;
    s_longPressCount = 0U;

    Control_ApplyDuty(25U);
    Control_ResetStatusLed();
}

void AppControl_Process(uint32_t nowMs)
{
    uint32_t elapsed = (uint32_t)(nowMs - s_lastMs);
    s_lastMs = nowMs;

    if (s_mode == APP_MODE_AUTO)
    {
        uint32_t totalMs = s_autoSubMs + elapsed;
        uint32_t steps = totalMs / AUTO_STEP_MS;
        s_autoSubMs = totalMs % AUTO_STEP_MS;

        if (steps != 0U)
        {
            s_autoPhase = (s_autoPhase + (steps % AUTO_CYCLE_STEPS)) % AUTO_CYCLE_STEPS;
            Control_ApplyDuty(Control_AutoDuty());
        }
    }

    if (s_mode != APP_MODE_MANUAL)
    {
        uint32_t blinkPeriod = (s_mode == APP_MODE_AUTO)
                             ? BLINK_AUTO_MS : BLINK_PAUSE_MS;
        uint32_t totalMs = s_ledSubMs + elapsed;
        uint32_t toggles = totalMs / blinkPeriod;
        s_ledSubMs = totalMs % blinkPeriod;
        if ((toggles & 1U) != 0U)
        {
            HAL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
        }
    }
}

void AppControl_HandleButton(AppButton_Event event)
{
    if (event == APP_BUTTON_SHORT)
    {
        if (s_mode == APP_MODE_PAUSA)
        {
            return;
        }

        ++s_shortPressCount;
        if (s_mode == APP_MODE_MANUAL)
        {
            (void)AppControl_SetMode(APP_MODE_AUTO, s_lastMs);
        }
        else
        {
            (void)AppControl_SetMode(APP_MODE_MANUAL, s_lastMs);
        }
        Control_ResetStatusLed();
    }
    else if (event == APP_BUTTON_LONG)
    {
        ++s_longPressCount;
        if (s_mode == APP_MODE_PAUSA)
        {
            (void)AppControl_Resume(s_lastMs);
        }
        else
        {
            (void)AppControl_Pause(s_lastMs);
        }
        Control_ResetStatusLed();
    }
}

void AppControl_GetStatus(AppControl_Status *out)
{
    if (out == NULL)
    {
        return;
    }

    out->mode = s_mode;
    out->appliedDuty = s_appliedDuty;
    out->manualDuty = s_manualDuty;
    out->frequencyHz = s_frequencyHz;
    out->shortPressCount = s_shortPressCount;
    out->longPressCount = s_longPressCount;
}

AppControl_Result AppControl_SetMode(AppControl_Mode mode, uint32_t nowMs)
{
    if (s_mode == APP_MODE_PAUSA) return APP_CONTROL_STATE;
    if (mode != APP_MODE_MANUAL && mode != APP_MODE_AUTO) return APP_CONTROL_RANGE;
    AppControl_Process(nowMs);
    s_mode = mode;
    s_lastMs = nowMs;
    if (mode == APP_MODE_AUTO)
    {
        s_autoPhase = s_autoSubMs = 0U;
        Control_ApplyDuty(10U);
    }
    else Control_ApplyDuty(s_manualDuty);
    Control_ResetStatusLed();
    return APP_CONTROL_OK;
}

AppControl_Result AppControl_SetDuty(uint32_t duty)
{
    if (s_mode != APP_MODE_MANUAL) return APP_CONTROL_STATE;
    if (duty > 100U) return APP_CONTROL_RANGE;
    s_manualDuty = (uint8_t)duty;
    Control_ApplyDuty(s_manualDuty);
    return APP_CONTROL_OK;
}

AppControl_Result AppControl_SetFrequency(uint32_t frequency)
{
    if (s_mode == APP_MODE_PAUSA) return APP_CONTROL_STATE;
    if (frequency != 500U && frequency != 1000U && frequency != 2000U)
        return APP_CONTROL_RANGE;
    uint32_t counterHz = s_timerClockHz / (s_pwmTimer->Instance->PSC + 1U);
    uint32_t periodTicks = counterHz / frequency;
    if (periodTicks == 0U || counterHz % frequency != 0U)
        return APP_CONTROL_RANGE;

    /* PWM is TIM2 only. TIM6 and the monotonic millisecond counter are untouched. */
    SET_BIT(s_pwmTimer->Instance->CR1, TIM_CR1_UDIS);
    __HAL_TIM_SET_AUTORELOAD(s_pwmTimer, periodTicks - 1U);
    Control_ApplyDuty(s_appliedDuty);
    CLEAR_BIT(s_pwmTimer->Instance->CR1, TIM_CR1_UDIS);
    s_pwmTimer->Instance->EGR = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(s_pwmTimer, (uint32_t)TIM_FLAG_UPDATE);
    s_frequencyHz = (uint16_t)frequency;
    return APP_CONTROL_OK;
}

AppControl_Result AppControl_Pause(uint32_t nowMs)
{
    if (s_mode == APP_MODE_PAUSA) return APP_CONTROL_OK;
    AppControl_Process(nowMs);
    s_resumeMode = s_mode;
    s_mode = APP_MODE_PAUSA;
    Control_ApplyDuty(0U);
    Control_ResetStatusLed();
    return APP_CONTROL_OK;
}

AppControl_Result AppControl_Resume(uint32_t nowMs)
{
    if (s_mode != APP_MODE_PAUSA) return APP_CONTROL_STATE;
    s_lastMs = nowMs;
    s_mode = s_resumeMode;
    Control_ApplyDuty((s_mode == APP_MODE_AUTO) ? Control_AutoDuty() : s_manualDuty);
    Control_ResetStatusLed();
    return APP_CONTROL_OK;
}
