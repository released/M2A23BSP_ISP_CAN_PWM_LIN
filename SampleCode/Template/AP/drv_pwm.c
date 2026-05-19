/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include "NuMicro.h"

#include "drv_pwm.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
#define DRV_PWM_DUTY_MAX                                (100U)
#define DRV_PWM_DUTY_RESOLUTION                         (100U)

static const uint8_t g_au8PwmChannel[DRV_PWM_GROUP_NUM] = {0U, 2U, 4U};
static const uint32_t g_au32PwmFreq[DRV_PWM_GROUP_NUM] = {10000UL, 1000UL, 100UL};
static uint8_t g_au8PwmDuty[DRV_PWM_GROUP_NUM] = {50U, 50U, 50U};

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
    uint32_t u32Msken;
    uint32_t u32Primask;

    u32Bit = (1UL << u32ChannelNum);
    u32Msk = pwm->MSK;
    u32Msken = pwm->MSKEN;

    if (u32Level != 0U)
    {
        u32Msk |= u32Bit;
    }
    else
    {
        u32Msk &= ~u32Bit;
    }

    u32Msken |= u32Bit;

    u32Primask = DRV_PWM_EnterCritical();
    pwm->MSK = u32Msk;
    pwm->MSKEN = u32Msken;
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

static void DRV_PWM_PrintStatus(void)
{
    uint8_t i;

    for (i = 0U; i < DRV_PWM_GROUP_NUM; i++)
    {
        printf("PWM%u(ch%u): %lu Hz, %u %%\r\n",
               i + 1U,
               g_au8PwmChannel[i],
               (unsigned long)g_au32PwmFreq[i],
               g_au8PwmDuty[i]);
    }
}

void DRV_PWM_SetOutputDutyCycle(E_DRV_PWM_GROUP eGroup, uint8_t u8Duty)
{
    uint32_t u32NewCmr;
    uint8_t u8Group;
    uint8_t u8Channel;

    u8Group = (uint8_t)eGroup;
    if (u8Group >= DRV_PWM_GROUP_NUM)
    {
        return;
    }

    u8Duty = DRV_PWM_ClampDuty(u8Duty);
    u8Channel = g_au8PwmChannel[u8Group];
    g_au8PwmDuty[u8Group] = u8Duty;

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

/* Example:
 * DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_1, 60U);
 * DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_2, 0U);
 */
void DRV_PWM_Init(void)
{
    uint8_t i;
    uint32_t u32Mask;

    u32Mask = 0U;

    for (i = 0U; i < DRV_PWM_GROUP_NUM; i++)
    {
        PWM_ConfigOutputChannel(PWM0,
                                (uint32_t)g_au8PwmChannel[i],
                                g_au32PwmFreq[i],
                                g_au8PwmDuty[i]);
        u32Mask |= (1UL << g_au8PwmChannel[i]);
    }

    PWM_EnableOutput(PWM0, u32Mask);
    PWM_Start(PWM0, u32Mask);

    for (i = 0U; i < DRV_PWM_GROUP_NUM; i++)
    {
        DRV_PWM_SetOutputDutyCycle((E_DRV_PWM_GROUP)i, g_au8PwmDuty[i]);
    }

    DRV_PWM_PrintStatus();
}
