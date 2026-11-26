/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include "NuMicro.h"

#include "drv_pwm.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
static uint16_t g_u16PwmDutyGroup1 = 0U;
static uint16_t g_u16PwmDutyGroup2 = 0U;
static uint16_t g_u16PwmDutyGroup3 = 0U;
static uint16_t g_u16BpwmDuty = 0U;

/*_____ F U N C T I O N S __________________________________________________*/
static uint32_t DRV_PWM_EnterCritical(void)
{
    uint32_t u32Primask;

    u32Primask = __get_PRIMASK();
    __disable_irq();
    return u32Primask;
}

static void DRV_PWM_ExitCritical(uint32_t u32Primask)
{
    __set_PRIMASK(u32Primask);
}

static uint16_t DRV_PWM_ClampDuty(uint16_t u16Duty)
{
    if (u16Duty > DRV_PWM_DUTY_MAX)
    {
        return DRV_PWM_DUTY_MAX;
    }

    return u16Duty;
}

static uint32_t DRV_PWM_CalcNewDutyCmr(PWM_T *pwm, uint32_t u32ChannelNum, uint32_t u32DutyCycle, uint32_t u32CycleResolution)
{
    if (u32DutyCycle >= u32CycleResolution)
    {
        return PWM_GET_CNR(pwm, u32ChannelNum);
    }

    return ((u32DutyCycle * (PWM_GET_CNR(pwm, u32ChannelNum) + 1UL) +
             u32CycleResolution / 2UL) / u32CycleResolution);
}

static void DRV_PWM_ChannelMask(PWM_T *pwm, uint32_t u32ChannelNum, uint32_t u32Level)
{
    uint32_t u32Bit;
    uint32_t u32Msk;
    uint32_t u32MaskEnable;
    uint32_t u32Primask;

    u32Bit = (1UL << u32ChannelNum);
    u32Msk = pwm->MSK;
    u32MaskEnable = pwm->MSKEN;

    if (u32Level != 0U)
    {
        u32Msk |= u32Bit;
    }
    else
    {
        u32Msk &= ~u32Bit;
    }

    u32MaskEnable |= u32Bit;

    u32Primask = DRV_PWM_EnterCritical();
    pwm->MSK = u32Msk;
    pwm->MSKEN = u32MaskEnable;
    DRV_PWM_ExitCritical(u32Primask);
}

static void DRV_PWM_ChannelUnmask(PWM_T *pwm, uint32_t u32ChannelNum)
{
    uint32_t u32Bit;
    uint32_t u32Primask;

    u32Bit = (1UL << u32ChannelNum);
    u32Primask = DRV_PWM_EnterCritical();
    pwm->MSKEN &= ~u32Bit;
    DRV_PWM_ExitCritical(u32Primask);
}

static void DRV_BPWM_ChannelMask(BPWM_T *bpwm, uint32_t u32ChannelNum, uint32_t u32Level)
{
    uint32_t u32Bit;
    uint32_t u32Msk;
    uint32_t u32MaskEnable;
    uint32_t u32Primask;

    u32Bit = (1UL << u32ChannelNum);
    u32Msk = bpwm->MSK;
    u32MaskEnable = bpwm->MSKEN;

    if (u32Level != 0U)
    {
        u32Msk |= u32Bit;
    }
    else
    {
        u32Msk &= ~u32Bit;
    }

    u32MaskEnable |= u32Bit;

    u32Primask = DRV_PWM_EnterCritical();
    bpwm->MSK = u32Msk;
    bpwm->MSKEN = u32MaskEnable;
    DRV_PWM_ExitCritical(u32Primask);
}

static void DRV_BPWM_ChannelUnmask(BPWM_T *bpwm, uint32_t u32ChannelNum)
{
    uint32_t u32Bit;
    uint32_t u32Primask;

    u32Bit = (1UL << u32ChannelNum);
    u32Primask = DRV_PWM_EnterCritical();
    bpwm->MSKEN &= ~u32Bit;
    DRV_PWM_ExitCritical(u32Primask);
}

static uint32_t DRV_BPWM_CalcNewDutyCmr(BPWM_T *bpwm, uint32_t u32ChannelNum, uint32_t u32DutyCycle, uint32_t u32CycleResolution)
{
    if (u32DutyCycle >= u32CycleResolution)
    {
        return BPWM_GET_CNR(bpwm, u32ChannelNum);
    }

    return ((u32DutyCycle * (BPWM_GET_CNR(bpwm, u32ChannelNum) + 1UL) +
             u32CycleResolution / 2UL) / u32CycleResolution);
}

static void DRV_PWM_PrintDuty(uint16_t u16Duty)
{
#if (DRV_PWM_DUTY_RESOLUTION == 10000U)
    printf("%u.%02u %%\r\n", (unsigned int)(u16Duty / 100U), (unsigned int)(u16Duty % 100U));
#elif (DRV_PWM_DUTY_RESOLUTION == 1000U)
    printf("%u.%u %%\r\n", (unsigned int)(u16Duty / 10U), (unsigned int)(u16Duty % 10U));
#else
    printf("%u %%\r\n", (unsigned int)u16Duty);
#endif
}

static void DRV_PWM_PrintStatus(void)
{
    printf("PWM1(ch%u): %lu Hz, ",
           DRV_PWM_GROUP1_CHANNEL,
           (unsigned long)DRV_PWM_GROUP1_FREQ_HZ);
    DRV_PWM_PrintDuty(g_u16PwmDutyGroup1);
    printf("PWM2(ch%u): %lu Hz, ",
           DRV_PWM_GROUP2_CHANNEL,
           (unsigned long)DRV_PWM_GROUP2_FREQ_HZ);
    DRV_PWM_PrintDuty(g_u16PwmDutyGroup2);
    printf("PWM3(ch%u): %lu Hz, ",
           DRV_PWM_GROUP3_CHANNEL,
           (unsigned long)DRV_PWM_GROUP3_FREQ_HZ);
    DRV_PWM_PrintDuty(g_u16PwmDutyGroup3);
    printf("BPWM4(PA3/ch%u): %lu Hz, ",
           DRV_BPWM_CHANNEL,
           (unsigned long)DRV_BPWM_FREQ_HZ);
    DRV_PWM_PrintDuty(g_u16BpwmDuty);
}

void DRV_PWM_SetOutputDutyCycle(E_DRV_PWM_GROUP eGroup, uint16_t u16Duty)
{
    uint32_t u32NewCmr;
    uint8_t u8Channel;
    uint16_t *pu16Duty;

    switch (eGroup)
    {
        case eDRV_PWM_GROUP_1:
            u8Channel = DRV_PWM_GROUP1_CHANNEL;
            pu16Duty = &g_u16PwmDutyGroup1;
            break;

        case eDRV_PWM_GROUP_2:
            u8Channel = DRV_PWM_GROUP2_CHANNEL;
            pu16Duty = &g_u16PwmDutyGroup2;
            break;

        case eDRV_PWM_GROUP_3:
            u8Channel = DRV_PWM_GROUP3_CHANNEL;
            pu16Duty = &g_u16PwmDutyGroup3;
            break;

        default:
            return;
    }

    u16Duty = DRV_PWM_ClampDuty(u16Duty);
    *pu16Duty = u16Duty;

    if (u16Duty == 0U)
    {
        DRV_PWM_ChannelMask(PWM0, (uint32_t)u8Channel, 0U);
        PWM_SET_CMR(PWM0, (uint32_t)u8Channel, 0U);
        return;
    }

    u32NewCmr = DRV_PWM_CalcNewDutyCmr(PWM0,
                                       (uint32_t)u8Channel,
                                       (uint32_t)u16Duty,
                                       DRV_PWM_DUTY_RESOLUTION);
    PWM_SET_CMR(PWM0, (uint32_t)u8Channel, u32NewCmr);
    DRV_PWM_ChannelUnmask(PWM0, (uint32_t)u8Channel);
}

void DRV_BPWM_SetOutputDutyCycle(uint16_t u16Duty)
{
    uint32_t u32NewCmr;

    u16Duty = DRV_PWM_ClampDuty(u16Duty);
    g_u16BpwmDuty = u16Duty;

    if (u16Duty == 0U)
    {
        DRV_BPWM_ChannelMask(BPWM0, DRV_BPWM_CHANNEL, 0U);
        BPWM_SET_CMR(BPWM0, DRV_BPWM_CHANNEL, 0U);
        return;
    }

    u32NewCmr = DRV_BPWM_CalcNewDutyCmr(BPWM0,
                                        DRV_BPWM_CHANNEL,
                                        (uint32_t)u16Duty,
                                        DRV_PWM_DUTY_RESOLUTION);
    BPWM_SET_CMR(BPWM0, DRV_BPWM_CHANNEL, u32NewCmr);
    DRV_BPWM_ChannelUnmask(BPWM0, DRV_BPWM_CHANNEL);
}

/* PWM0 and BPWM0 duty examples (select matching DRV_PWM_DUTY_RESOLUTION):
 * 10000U:
 * DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_1, 8230U);  82.30%
 * DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_2, 8233U);  82.33%
 * DRV_BPWM_SetOutputDutyCycle(4750U);                  47.50%
 *
 * 1000U:
 * DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_1, 823U);   82.3%
 * DRV_BPWM_SetOutputDutyCycle(475U);                   47.5%
 *
 * 100U:
 * DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_1, 82U);    82%
 * DRV_BPWM_SetOutputDutyCycle(47U);                    47%
 *
 * Note:
 * Startup duty remains 60%, 30%, 30% and BPWM 0% for every resolution.
 * PA3 is used by BPWM0_CH3 when DRV_GPIO_LED_SET4_GPIO_ENABLE is 0U.
 * 0U duty forces the selected output to logic low.
 * DRV_PWM_DUTY_MAX represents 100% for both output APIs.
 */
void DRV_PWM_Init(void)
{
    uint32_t u32Mask;
    uint32_t u32BpwmMask;

    g_u16PwmDutyGroup1 = 0U;
    g_u16PwmDutyGroup2 = 0U;
    g_u16PwmDutyGroup3 = 0U;
    g_u16BpwmDuty = 0U;

    PWM_ConfigOutputChannel(PWM0, DRV_PWM_GROUP1_CHANNEL, DRV_PWM_GROUP1_FREQ_HZ, 0U);
    PWM_ConfigOutputChannel(PWM0, DRV_PWM_GROUP2_CHANNEL, DRV_PWM_GROUP2_FREQ_HZ, 0U);
    PWM_ConfigOutputChannel(PWM0, DRV_PWM_GROUP3_CHANNEL, DRV_PWM_GROUP3_FREQ_HZ, 0U);

    u32Mask = BIT0 | BIT2 | BIT4;

    PWM_EnableOutput(PWM0, u32Mask);
    PWM_Start(PWM0, u32Mask);

    BPWM_ConfigOutputChannel(BPWM0, DRV_BPWM_CHANNEL, DRV_BPWM_FREQ_HZ, 0U);
    u32BpwmMask = (1UL << DRV_BPWM_CHANNEL);
    BPWM_EnableOutput(BPWM0, u32BpwmMask);
    BPWM_Start(BPWM0, u32BpwmMask);

    DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_1, (uint16_t)(60UL * DRV_PWM_DUTY_RESOLUTION / 100UL));
    DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_2, (uint16_t)(30UL * DRV_PWM_DUTY_RESOLUTION / 100UL));
    DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_3, (uint16_t)(30UL * DRV_PWM_DUTY_RESOLUTION / 100UL));
    DRV_BPWM_SetOutputDutyCycle(0U);

    DRV_PWM_PrintStatus();
}
