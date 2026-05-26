/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include "NuMicro.h"

#include "drv_adc.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
#define DRV_ADC_EXTEND_SAMPLING                         (10U)
#define DRV_ADC_AVDD_EXTEND_SAMPLING                    (71U)
#define DRV_ADC_AVDD_SAMPLE_COUNT                       (4U)
#define DRV_ADC_AVG_TARGET                              (8U)
#define DRV_ADC_AVG_POW                                 (3U)
#define DRV_ADC_WAIT_READY_TIMEOUT_COUNT                (0xFFFFU)
#define DRV_ADC_MAX_CODE                                (4095UL)
#define DRV_ADC_VBG_TYP_MV                              (1210UL)
#define DRV_ADC_BANDGAP_CHANNEL_MASK                    (BIT29)
#define DRV_ADC_BANDGAP_CHANNEL_INDEX                   (29U)
#define DRV_ADC_BANDGAP_CODE_MIN                        (800UL)
#define DRV_ADC_BANDGAP_CODE_MAX                        (2200UL)

typedef struct
{
    uint32_t u32ChannelMask;
    uint32_t u32ChannelIndex;
    uint16_t u16Sample;
} S_DRV_ADC_CHANNEL_CFG;

static S_DRV_ADC_CHANNEL_CFG g_asDrvAdcChannel[eDRV_ADC_CHANNEL_MAX] =
{
    {BIT0, 0U, 0U},
    {BIT2, 2U, 0U}
};

static uint8_t g_u8AdcAverageReady = 0U;
static uint32_t g_u32AdcTimeoutTotal = 0U;
static uint32_t g_u32AdcTimeoutInit = 0U;
static uint32_t g_u32AdcTimeoutMoving = 0U;
static uint32_t g_u32AdcAvddMv = 0U;

/*_____ F U N C T I O N S __________________________________________________*/
static uint8_t DRV_ADC_ConvertChannel(S_DRV_ADC_CHANNEL_CFG *psChannelCfg, uint16_t *pu16Sample)
{
    uint32_t u32TimeOutCnt;

    if ((psChannelCfg == (S_DRV_ADC_CHANNEL_CFG *)0) || (pu16Sample == (uint16_t *)0))
    {
        return 0U;
    }

    ADC_SET_INPUT_CHANNEL(ADC0, psChannelCfg->u32ChannelMask);
    ADC_CLR_INT_FLAG(ADC0, ADC_ADF_INT);
    ADC_START_CONV(ADC0);

    u32TimeOutCnt = DRV_ADC_WAIT_READY_TIMEOUT_COUNT;
    while (ADC_GET_INT_FLAG(ADC0, ADC_ADF_INT) == 0U)
    {
        if (u32TimeOutCnt == 0U)
        {
            return 0U;
        }
        u32TimeOutCnt--;
    }

    *pu16Sample = (uint16_t)ADC_GET_CONVERSION_DATA(ADC0, psChannelCfg->u32ChannelIndex);
    ADC_CLR_INT_FLAG(ADC0, ADC_ADF_INT);

    return 1U;
}

static uint32_t DRV_ADC_ConvertDataToMilliVolt(uint16_t u16Sample, uint32_t u32AvddMv)
{
    if (u32AvddMv == 0U)
    {
        return 0U;
    }

    return (((uint32_t)u16Sample * u32AvddMv) + (DRV_ADC_MAX_CODE / 2UL)) / DRV_ADC_MAX_CODE;
}

static void DRV_ADC_SeedAverage(void)
{
    uint8_t i;
    uint8_t j;
    uint8_t u8Timeout;
    uint32_t u32Sum;
    uint16_t u16Sample;

    for (i = 0U; i < (uint8_t)eDRV_ADC_CHANNEL_MAX; i++)
    {
        u32Sum = 0U;
        u8Timeout = 0U;

        for (j = 0U; j < DRV_ADC_AVG_TARGET; j++)
        {
            if (DRV_ADC_ConvertChannel(&g_asDrvAdcChannel[i], &u16Sample) == 0U)
            {
                g_u32AdcTimeoutTotal++;
                g_u32AdcTimeoutInit++;
                u8Timeout = 1U;
                break;
            }

            u32Sum += (uint32_t)u16Sample;
        }

        if (u8Timeout == 0U)
        {
            g_asDrvAdcChannel[i].u16Sample = (uint16_t)(u32Sum >> DRV_ADC_AVG_POW);
        }
    }

    g_u8AdcAverageReady = 1U;
}

void DRV_ADC_Init(void)
{
    SYS_ResetModule(ADC0_RST);

    GPIO_DISABLE_DIGITAL_PATH(PB, BIT0 | BIT2);

    ADC_POWER_ON(ADC0);
    ADC_Open(ADC0,
             ADC_ADCR_DIFFEN_SINGLE_END,
             ADC_ADCR_ADMD_SINGLE,
             g_asDrvAdcChannel[eDRV_ADC_B0].u32ChannelMask);
    ADC_SetExtendSampleTime(ADC0, 0U, DRV_ADC_EXTEND_SAMPLING);
    DRV_ADC_UpdateAvdd();
}

void DRV_ADC_SampleAll(void)
{
    uint8_t i;
    uint32_t u32Sum;
    uint16_t u16Sample;

    if (g_u8AdcAverageReady == 0U)
    {
        DRV_ADC_SeedAverage();
        return;
    }

    for (i = 0U; i < (uint8_t)eDRV_ADC_CHANNEL_MAX; i++)
    {
        if (DRV_ADC_ConvertChannel(&g_asDrvAdcChannel[i], &u16Sample) == 0U)
        {
            g_u32AdcTimeoutTotal++;
            g_u32AdcTimeoutMoving++;
            continue;
        }

        u32Sum = ((uint32_t)g_asDrvAdcChannel[i].u16Sample << DRV_ADC_AVG_POW) -
                 (uint32_t)g_asDrvAdcChannel[i].u16Sample +
                 (uint32_t)u16Sample;
        g_asDrvAdcChannel[i].u16Sample = (uint16_t)(u32Sum >> DRV_ADC_AVG_POW);
    }
}

uint16_t DRV_ADC_GetSample(E_DRV_ADC_CHANNEL eChannel)
{
    if ((uint32_t)eChannel >= (uint32_t)eDRV_ADC_CHANNEL_MAX)
    {
        return 0U;
    }

    return g_asDrvAdcChannel[(uint32_t)eChannel].u16Sample;
}

void DRV_ADC_UpdateAvdd(void)
{
    uint32_t u32ConversionData;
    uint32_t u32Sum;
    uint32_t u32TimeOutCnt;
    uint32_t u32SampleIdx;

    u32ConversionData = 0U;
    u32Sum = 0U;
    u32SampleIdx = 0U;

    ADC_POWER_ON(ADC0);
    CLK_SysTickDelay(10000);

    ADC_SET_INPUT_CHANNEL(ADC0, DRV_ADC_BANDGAP_CHANNEL_MASK);
    ADC_SetExtendSampleTime(ADC0, 0U, DRV_ADC_AVDD_EXTEND_SAMPLING);

    /* Discard the first sample after switching mux to CH29. */
    ADC_CLR_INT_FLAG(ADC0, ADC_ADF_INT);
    ADC_START_CONV(ADC0);

    u32TimeOutCnt = DRV_ADC_WAIT_READY_TIMEOUT_COUNT;
    while ((ADC_GET_INT_FLAG(ADC0, ADC_ADF_INT) == 0U) && (u32TimeOutCnt > 0U))
    {
        u32TimeOutCnt--;
    }

    if ((u32TimeOutCnt != 0U) && (ADC_IS_DATA_VALID(ADC0, DRV_ADC_BANDGAP_CHANNEL_INDEX) != 0U))
    {
        (void)ADC_GET_CONVERSION_DATA(ADC0, DRV_ADC_BANDGAP_CHANNEL_INDEX);
        ADC_CLR_INT_FLAG(ADC0, ADC_ADF_INT);

        for (u32SampleIdx = 0U; u32SampleIdx < DRV_ADC_AVDD_SAMPLE_COUNT; u32SampleIdx++)
        {
            ADC_START_CONV(ADC0);

            u32TimeOutCnt = DRV_ADC_WAIT_READY_TIMEOUT_COUNT;
            while ((ADC_GET_INT_FLAG(ADC0, ADC_ADF_INT) == 0U) && (u32TimeOutCnt > 0U))
            {
                u32TimeOutCnt--;
            }

            if ((u32TimeOutCnt == 0U) || (ADC_IS_DATA_VALID(ADC0, DRV_ADC_BANDGAP_CHANNEL_INDEX) == 0U))
            {
                u32Sum = 0U;
                break;
            }

            u32Sum += ADC_GET_CONVERSION_DATA(ADC0, DRV_ADC_BANDGAP_CHANNEL_INDEX);
            ADC_CLR_INT_FLAG(ADC0, ADC_ADF_INT);
        }
    }

    if (u32Sum != 0U)
    {
        u32ConversionData = (u32Sum + (DRV_ADC_AVDD_SAMPLE_COUNT / 2U)) / DRV_ADC_AVDD_SAMPLE_COUNT;
        if ((u32ConversionData >= DRV_ADC_BANDGAP_CODE_MIN) &&
            (u32ConversionData <= DRV_ADC_BANDGAP_CODE_MAX))
        {
            g_u32AdcAvddMv = ((DRV_ADC_VBG_TYP_MV * DRV_ADC_MAX_CODE) + (u32ConversionData / 2UL)) / u32ConversionData;
        }
    }

    ADC_SET_INPUT_CHANNEL(ADC0, g_asDrvAdcChannel[eDRV_ADC_B0].u32ChannelMask);
    ADC_SetExtendSampleTime(ADC0, 0U, DRV_ADC_EXTEND_SAMPLING);
}

uint32_t DRV_ADC_GetAvddMv(void)
{
    return g_u32AdcAvddMv;
}

uint32_t DRV_ADC_GetSampleMilliVolt(E_DRV_ADC_CHANNEL eChannel)
{
    if ((uint32_t)eChannel >= (uint32_t)eDRV_ADC_CHANNEL_MAX)
    {
        return 0U;
    }

    return DRV_ADC_ConvertDataToMilliVolt(g_asDrvAdcChannel[(uint32_t)eChannel].u16Sample, g_u32AdcAvddMv);
}
