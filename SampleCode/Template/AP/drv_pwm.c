/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include "NuMicro.h"

#include "drv_pwm.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
static uint8_t g_u8PwmDutyGroup1 = 0U;
static uint8_t g_u8PwmDutyGroup2 = 0U;
static uint8_t g_u8PwmDutyGroup3 = 0U;
static uint8_t g_u8BpwmDuty = 0U;

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

static uint8_t DRV_PWM_ClampDuty(uint8_t u8Duty)
{
    if (u8Duty > DRV_PWM_DUTY_MAX)
    {
        return DRV_PWM_DUTY_MAX;
    }

    return u8Duty;
}

static uint32_t DRV_PWM_CalcNewDutyCmr(PWM_T *pwm, uint32_t u32ChannelNum, uint32_t u32DutyCycle, uint32_t u32CycleResolution)
{
    if (u32DutyCycle >= u32CycleResolution)
    {
        return PWM_GET_CNR(pwm, u32ChannelNum);
    }

    return (u32DutyCycle * (PWM_GET_CNR(pwm, u32ChannelNum) + 1UL) / u32CycleResolution);
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

    return (u32DutyCycle * (BPWM_GET_CNR(bpwm, u32ChannelNum) + 1UL) / u32CycleResolution);
}

static void DRV_PWM_PrintStatus(void)
{
    printf("PWM1(ch%u): %lu Hz, %u %%\r\n",
           DRV_PWM_GROUP1_CHANNEL,
           (unsigned long)DRV_PWM_GROUP1_FREQ_HZ,
           g_u8PwmDutyGroup1);
    printf("PWM2(ch%u): %lu Hz, %u %%\r\n",
           DRV_PWM_GROUP2_CHANNEL,
           (unsigned long)DRV_PWM_GROUP2_FREQ_HZ,
           g_u8PwmDutyGroup2);
    printf("PWM3(ch%u): %lu Hz, %u %%\r\n",
           DRV_PWM_GROUP3_CHANNEL,
           (unsigned long)DRV_PWM_GROUP3_FREQ_HZ,
           g_u8PwmDutyGroup3);
    printf("BPWM4(PA3/ch%u): %lu Hz, %u %%\r\n",
           DRV_BPWM_CHANNEL,
           (unsigned long)DRV_BPWM_FREQ_HZ,
           g_u8BpwmDuty);
}

void DRV_PWM_SetOutputDutyCycle(E_DRV_PWM_GROUP eGroup, uint8_t u8Duty)
{
    uint32_t u32NewCmr;
    uint8_t u8Channel;
    uint8_t *pu8Duty;

    switch (eGroup)
    {
        case eDRV_PWM_GROUP_1:
            u8Channel = DRV_PWM_GROUP1_CHANNEL;
            pu8Duty = &g_u8PwmDutyGroup1;
            break;

        case eDRV_PWM_GROUP_2:
            u8Channel = DRV_PWM_GROUP2_CHANNEL;
            pu8Duty = &g_u8PwmDutyGroup2;
            break;

        case eDRV_PWM_GROUP_3:
            u8Channel = DRV_PWM_GROUP3_CHANNEL;
            pu8Duty = &g_u8PwmDutyGroup3;
            break;

        default:
            return;
    }

    u8Duty = DRV_PWM_ClampDuty(u8Duty);
    *pu8Duty = u8Duty;

    if (u8Duty == 0U)
    {
        DRV_PWM_ChannelMask(PWM0, (uint32_t)u8Channel, 0U);
        PWM_SET_CMR(PWM0, (uint32_t)u8Channel, 0U);
        return;
    }

    u32NewCmr = DRV_PWM_CalcNewDutyCmr(PWM0,
                                       (uint32_t)u8Channel,
                                       (uint32_t)u8Duty,
                                       DRV_PWM_DUTY_RESOLUTION);
    PWM_SET_CMR(PWM0, (uint32_t)u8Channel, u32NewCmr);
    DRV_PWM_ChannelUnmask(PWM0, (uint32_t)u8Channel);
}

void DRV_BPWM_SetOutputDutyCycle(uint8_t u8Duty)
{
    uint32_t u32NewCmr;

    u8Duty = DRV_PWM_ClampDuty(u8Duty);
    g_u8BpwmDuty = u8Duty;

    if (u8Duty == 0U)
    {
        DRV_BPWM_ChannelMask(BPWM0, DRV_BPWM_CHANNEL, 0U);
        BPWM_SET_CMR(BPWM0, DRV_BPWM_CHANNEL, 0U);
        return;
    }

    u32NewCmr = DRV_BPWM_CalcNewDutyCmr(BPWM0,
                                        DRV_BPWM_CHANNEL,
                                        (uint32_t)u8Duty,
                                        DRV_PWM_DUTY_RESOLUTION);
    BPWM_SET_CMR(BPWM0, DRV_BPWM_CHANNEL, u32NewCmr);
    DRV_BPWM_ChannelUnmask(BPWM0, DRV_BPWM_CHANNEL);
}

/* PWM0 output:
 * DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_1, 60U);
 * DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_2, 30U);
 * DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_3, 30U);
 *
 * BPWM0 output on PA3:
 * DRV_BPWM_SetOutputDutyCycle(25U);
 * DRV_BPWM_SetOutputDutyCycle(50U);
 * DRV_BPWM_SetOutputDutyCycle(0U);
 *
 * Note:
 * PA3 is used by BPWM0_CH3 when DRV_GPIO_LED_SET4_GPIO_ENABLE is 0U.
 * 0% duty forces PA3 output to logic low.
 */
void DRV_PWM_Init(void)
{
    uint32_t u32Mask;
    uint32_t u32BpwmMask;

    g_u8PwmDutyGroup1 = 0U;
    g_u8PwmDutyGroup2 = 0U;
    g_u8PwmDutyGroup3 = 0U;
    g_u8BpwmDuty = 0U;

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

    DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_1, 60U);
    DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_2, 30U);
    DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_3, 30U);
    DRV_BPWM_SetOutputDutyCycle(0U);

    DRV_PWM_PrintStatus();
}
