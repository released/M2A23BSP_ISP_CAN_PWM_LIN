/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include "NuMicro.h"

#include "drv_pwm.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
#define DRV_PWM_DUTY_MAX                                (100U)
#define DRV_PWM_DUTY_RESOLUTION                         (100U)
#define DRV_PWM_GROUP1_CHANNEL                          (0U)
#define DRV_PWM_GROUP2_CHANNEL                          (2U)
#define DRV_PWM_GROUP3_CHANNEL                          (4U)
#define DRV_PWM_GROUP1_FREQ_HZ                          (100UL)
#define DRV_PWM_GROUP2_FREQ_HZ                          (200UL)
#define DRV_PWM_GROUP3_FREQ_HZ                          (400UL)

static uint8_t g_u8PwmDutyGroup1 = 0U;
static uint8_t g_u8PwmDutyGroup2 = 0U;
static uint8_t g_u8PwmDutyGroup3 = 0U;

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

/* Example:
 * DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_1, 60U);
 * DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_2, 30U);
 * DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_3, 30U);
 */
void DRV_PWM_Init(void)
{
    uint32_t u32Mask;

    g_u8PwmDutyGroup1 = 0U;
    g_u8PwmDutyGroup2 = 0U;
    g_u8PwmDutyGroup3 = 0U;

    PWM_ConfigOutputChannel(PWM0, DRV_PWM_GROUP1_CHANNEL, DRV_PWM_GROUP1_FREQ_HZ, 0U);
    PWM_ConfigOutputChannel(PWM0, DRV_PWM_GROUP2_CHANNEL, DRV_PWM_GROUP2_FREQ_HZ, 0U);
    PWM_ConfigOutputChannel(PWM0, DRV_PWM_GROUP3_CHANNEL, DRV_PWM_GROUP3_FREQ_HZ, 0U);

    u32Mask = BIT0 | BIT2 | BIT4;

    PWM_EnableOutput(PWM0, u32Mask);
    PWM_Start(PWM0, u32Mask);

    DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_1, 60U);
    DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_2, 30U);
    DRV_PWM_SetOutputDutyCycle(eDRV_PWM_GROUP_3, 30U);

    DRV_PWM_PrintStatus();
}
