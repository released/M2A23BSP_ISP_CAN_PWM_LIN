/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include "NuMicro.h"

#include "drv_gpio_io.h"

/*_____ D E C L A R A T I O N S ____________________________________________*/
typedef struct
{
    GPIO_T *psPort;
    uint8_t u8PortIndex;
    uint8_t u8Pin;
} S_DRV_GPIO_PIN;

typedef struct
{
    uint32_t u32LastRiseTick;
    uint32_t u32LastEdgeTick;
    uint32_t u32HighTicks;
    uint16_t u16FreqHz;
    uint8_t u8Duty;
    uint8_t u8RiseSeen;
    uint8_t u8HighValid;
    uint8_t u8DataReady;
    uint8_t u8SignalPresent;
} S_DRV_GPIO_INPUT_MEASURE_DATA;

/*_____ D E F I N I T I O N S ______________________________________________*/
#define DRV_GPIO_BUTTON_PA_MASK                         (BIT12 | BIT13 | BIT14 | BIT15)
#define DRV_GPIO_BUTTON_PC_MASK                         (BIT0 | BIT1)
#define DRV_GPIO_INPUT_MASK                             (BIT4 | BIT5)
#define DRV_GPIO_INPUT_MEASURE_TICK_HZ                  (1000000UL)
#define DRV_GPIO_INPUT_MEASURE_FREQ_MIN_HZ              (80UL)
#define DRV_GPIO_INPUT_MEASURE_FREQ_MAX_HZ              (550UL)
#define DRV_GPIO_INPUT_MEASURE_TIMEOUT_TICKS            (500000UL)
#define DRV_GPIO_INPUT_MEASURE_COUNTER_MASK             (0x00FFFFFFUL)

static const S_DRV_GPIO_PIN g_asLedPin[eDRV_GPIO_LED_MAX] =
{
    {PA, 0U, 0U},
    {PA, 0U, 1U},
    {PA, 0U, 2U},
    {PA, 0U, 3U},
    {PA, 0U, 4U},
    {PA, 0U, 5U}
};

static volatile S_DRV_GPIO_INPUT_MEASURE_DATA g_asInputMeasureData[eDRV_GPIO_INPUT_MEASURE_MAX];

/*_____ F U N C T I O N S __________________________________________________*/
static uint32_t DRV_GPIO_IO_ReadPin(const S_DRV_GPIO_PIN *psPin)
{
    return (GPIO_PIN_DATA(psPin->u8PortIndex, psPin->u8Pin) != 0U) ? 1UL : 0UL;
}

static uint32_t DRV_GPIO_IO_GetMeasureTick(void)
{
    return (TIMER_GetCounter(TIMER2) & DRV_GPIO_INPUT_MEASURE_COUNTER_MASK);
}

static uint32_t DRV_GPIO_IO_GetMeasureElapsed(uint32_t u32NowTick, uint32_t u32PrevTick)
{
    return ((u32NowTick - u32PrevTick) & DRV_GPIO_INPUT_MEASURE_COUNTER_MASK);
}

static void DRV_GPIO_IO_HandleInputMeasureEdge(E_DRV_GPIO_INPUT_MEASURE eInput, uint32_t u32Level)
{
    volatile S_DRV_GPIO_INPUT_MEASURE_DATA *psMeasureData;
    uint32_t u32NowTick;
    uint32_t u32PeriodTicks;
    uint32_t u32FreqHz;
    uint32_t u32Duty;

    if ((uint32_t)eInput >= (uint32_t)eDRV_GPIO_INPUT_MEASURE_MAX)
    {
        return;
    }

    psMeasureData = &g_asInputMeasureData[(uint32_t)eInput];
    u32NowTick = DRV_GPIO_IO_GetMeasureTick();
    psMeasureData->u32LastEdgeTick = u32NowTick;

    if (u32Level != 0U)
    {
        if ((psMeasureData->u8RiseSeen != 0U) && (psMeasureData->u8HighValid != 0U))
        {
            u32PeriodTicks = DRV_GPIO_IO_GetMeasureElapsed(u32NowTick, psMeasureData->u32LastRiseTick);
            if ((u32PeriodTicks != 0U) && (psMeasureData->u32HighTicks <= u32PeriodTicks))
            {
                u32FreqHz = DRV_GPIO_INPUT_MEASURE_TICK_HZ / u32PeriodTicks;
                if ((u32FreqHz >= DRV_GPIO_INPUT_MEASURE_FREQ_MIN_HZ) && (u32FreqHz <= DRV_GPIO_INPUT_MEASURE_FREQ_MAX_HZ))
                {
                    u32Duty = ((psMeasureData->u32HighTicks * 100UL) + (u32PeriodTicks / 2UL)) / u32PeriodTicks;
                    if (u32Duty > 100UL)
                    {
                        u32Duty = 100UL;
                    }

                    psMeasureData->u16FreqHz = (uint16_t)u32FreqHz;
                    psMeasureData->u8Duty = (uint8_t)u32Duty;
                    psMeasureData->u8DataReady = 1U;
                    psMeasureData->u8SignalPresent = 1U;
                }
            }
        }

        psMeasureData->u32LastRiseTick = u32NowTick;
        psMeasureData->u8RiseSeen = 1U;
        psMeasureData->u8HighValid = 0U;
    }
    else
    {
        if (psMeasureData->u8RiseSeen != 0U)
        {
            psMeasureData->u32HighTicks = DRV_GPIO_IO_GetMeasureElapsed(u32NowTick, psMeasureData->u32LastRiseTick);
            psMeasureData->u8HighValid = 1U;
        }
    }
}

void DRV_GPIO_IO_Init(void)
{
    uint32_t i;

    SYS_UnlockReg();
    SET_INT0_PB5();
    SET_INT1_PB4();
    SET_GPIO_PA0();
    SET_GPIO_PA1();
    SET_GPIO_PA2();
    SET_GPIO_PA3();
    SET_GPIO_PA4();
    SET_GPIO_PA5();
    SET_GPIO_PA12();
    SET_GPIO_PA13();
    SET_GPIO_PA14();
    SET_GPIO_PA15();
    SET_GPIO_PC0();
    SET_GPIO_PC1();
    SYS_LockReg();

    GPIO_SetMode(PA, BIT0 | BIT1 | BIT2 | BIT3 | BIT4 | BIT5, GPIO_MODE_OUTPUT);
    GPIO_SetMode(PA, DRV_GPIO_BUTTON_PA_MASK, GPIO_MODE_INPUT);
    GPIO_SetMode(PC, DRV_GPIO_BUTTON_PC_MASK, GPIO_MODE_INPUT);
    GPIO_SetMode(PB, DRV_GPIO_INPUT_MASK, GPIO_MODE_INPUT);
    GPIO_SetPullCtl(PB, DRV_GPIO_INPUT_MASK, GPIO_PUSEL_PULL_UP);

    LED_SET1 = 0U;
    LED_SET2 = 0U;
    LED_SET3 = 0U;
    LED_SET4 = 0U;
    LED_SET5 = 0U;
    LED_SET6 = 0U;

    GPIO_CLR_INT_FLAG(PB, DRV_GPIO_INPUT_MASK);
    GPIO_EnableInt(PB, 5U, GPIO_INT_BOTH_EDGE);
    GPIO_EnableInt(PB, 4U, GPIO_INT_BOTH_EDGE);
    NVIC_ClearPendingIRQ(EINT0_IRQn);
    NVIC_ClearPendingIRQ(EINT1_IRQn);
    NVIC_EnableIRQ(EINT0_IRQn);
    NVIC_EnableIRQ(EINT1_IRQn);

    for (i = 0U; i < (uint32_t)eDRV_GPIO_INPUT_MEASURE_MAX; i++)
    {
        g_asInputMeasureData[i].u32LastRiseTick = 0U;
        g_asInputMeasureData[i].u32LastEdgeTick = 0U;
        g_asInputMeasureData[i].u32HighTicks = 0U;
        g_asInputMeasureData[i].u16FreqHz = 0U;
        g_asInputMeasureData[i].u8Duty = 0U;
        g_asInputMeasureData[i].u8RiseSeen = 0U;
        g_asInputMeasureData[i].u8HighValid = 0U;
        g_asInputMeasureData[i].u8DataReady = 0U;
        g_asInputMeasureData[i].u8SignalPresent = 0U;
    }
}

void GPIO_IN_OUT_proccess(void)
{
    LED_SET1 = (SW_1 != 0U) ? 1U : 0U;
    LED_SET2 = (SW_2 != 0U) ? 1U : 0U;
    LED_SET3 = (SW_3 != 0U) ? 1U : 0U;
    LED_SET4 = (SW_4 != 0U) ? 1U : 0U;
    LED_SET5 = (SW_5 != 0U) ? 1U : 0U;
    LED_SET6 = (SW_6 != 0U) ? 1U : 0U;
}

void DRV_GPIO_IO_SetLed(E_DRV_GPIO_LED eLed, uint8_t u8On)
{
    const S_DRV_GPIO_PIN *psPin;

    if ((uint32_t)eLed >= (uint32_t)eDRV_GPIO_LED_MAX)
    {
        return;
    }

    psPin = &g_asLedPin[(uint32_t)eLed];
    GPIO_PIN_DATA(psPin->u8PortIndex, psPin->u8Pin) = (u8On != 0U) ? 1U : 0U;
}

void DRV_GPIO_IO_ToggleLed(E_DRV_GPIO_LED eLed)
{
    const S_DRV_GPIO_PIN *psPin;
    uint32_t u32Current;

    if ((uint32_t)eLed >= (uint32_t)eDRV_GPIO_LED_MAX)
    {
        return;
    }

    psPin = &g_asLedPin[(uint32_t)eLed];
    u32Current = DRV_GPIO_IO_ReadPin(psPin);
    GPIO_PIN_DATA(psPin->u8PortIndex, psPin->u8Pin) = (u32Current == 0U) ? 1U : 0U;
}

uint8_t DRV_GPIO_IO_GetAndClearInputMeasure(E_DRV_GPIO_INPUT_MEASURE eInput, uint16_t *pu16FreqHz, uint8_t *pu8Duty)
{
    volatile S_DRV_GPIO_INPUT_MEASURE_DATA *psMeasureData;
    uint32_t u32NowTick;
    uint8_t u8Ready;

    if (((uint32_t)eInput >= (uint32_t)eDRV_GPIO_INPUT_MEASURE_MAX) ||
        (pu16FreqHz == (uint16_t *)0) ||
        (pu8Duty == (uint8_t *)0))
    {
        return 0U;
    }

    psMeasureData = &g_asInputMeasureData[(uint32_t)eInput];

    __disable_irq();
    u32NowTick = DRV_GPIO_IO_GetMeasureTick();
    if ((psMeasureData->u8SignalPresent != 0U) &&
        (DRV_GPIO_IO_GetMeasureElapsed(u32NowTick, psMeasureData->u32LastEdgeTick) >= DRV_GPIO_INPUT_MEASURE_TIMEOUT_TICKS))
    {
        psMeasureData->u32HighTicks = 0U;
        psMeasureData->u16FreqHz = 0U;
        psMeasureData->u8Duty = 0U;
        psMeasureData->u8RiseSeen = 0U;
        psMeasureData->u8HighValid = 0U;
        psMeasureData->u8DataReady = 1U;
        psMeasureData->u8SignalPresent = 0U;
    }

    u8Ready = psMeasureData->u8DataReady;
    *pu16FreqHz = psMeasureData->u16FreqHz;
    *pu8Duty = psMeasureData->u8Duty;
    psMeasureData->u8DataReady = 0U;
    __enable_irq();

    return u8Ready;
}

void EINT0_IRQHandler(void)
{
    if (GPIO_GET_INT_FLAG(PB, BIT5) != 0U)
    {
        GPIO_CLR_INT_FLAG(PB, BIT5);
        DRV_GPIO_IO_HandleInputMeasureEdge(eDRV_GPIO_INPUT_MEASURE1, (PB5 != 0U) ? 1UL : 0UL);
    }
}

void EINT1_IRQHandler(void)
{
    if (GPIO_GET_INT_FLAG(PB, BIT4) != 0U)
    {
        GPIO_CLR_INT_FLAG(PB, BIT4);
        DRV_GPIO_IO_HandleInputMeasureEdge(eDRV_GPIO_INPUT_MEASURE2, (PB4 != 0U) ? 1UL : 0UL);
    }
}
