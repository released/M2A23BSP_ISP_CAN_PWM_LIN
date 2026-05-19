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
    uint32_t u32Mask;
} S_DRV_GPIO_PIN;

/*_____ D E F I N I T I O N S ______________________________________________*/
#define DRV_GPIO_BUTTON_PA_MASK                         (BIT12 | BIT13 | BIT14 | BIT15)
#define DRV_GPIO_BUTTON_PC_MASK                         (BIT0 | BIT1)
#define DRV_GPIO_INT_MASK                               (BIT4 | BIT5)

static const S_DRV_GPIO_PIN g_asLedPin[eDRV_GPIO_LED_MAX] =
{
    {PA, 0U, 0U, BIT0},
    {PA, 0U, 1U, BIT1},
    {PA, 0U, 2U, BIT2},
    {PA, 0U, 3U, BIT3},
    {PA, 0U, 4U, BIT4},
    {PA, 0U, 5U, BIT5}
};

static const S_DRV_GPIO_PIN g_asButtonPin[6] =
{
    {PA, 0U, 15U, BIT15},
    {PA, 0U, 14U, BIT14},
    {PA, 0U, 13U, BIT13},
    {PA, 0U, 12U, BIT12},
    {PC, 2U, 0U, BIT0},
    {PC, 2U, 1U, BIT1}
};

static const S_DRV_GPIO_PIN g_asInputPin[2] =
{
    {PB, 1U, 5U, BIT5},
    {PB, 1U, 4U, BIT4}
};

static volatile uint32_t g_u32ButtonEventFlags = 0U;
static volatile uint32_t g_u32InputEventFlags = 0U;

/*_____ F U N C T I O N S __________________________________________________*/
static uint32_t DRV_GPIO_IO_ReadPin(const S_DRV_GPIO_PIN *psPin)
{
    return (GPIO_PIN_DATA(psPin->u8PortIndex, psPin->u8Pin) != 0U) ? 1UL : 0UL;
}

static void DRV_GPIO_IO_SetButtonEventByMask(GPIO_T *psPort, uint32_t u32Mask)
{
    if (psPort == PA)
    {
        if ((u32Mask & BIT15) != 0U)
        {
            g_u32ButtonEventFlags |= DRV_GPIO_BUTTON1_EVENT;
        }
        if ((u32Mask & BIT14) != 0U)
        {
            g_u32ButtonEventFlags |= DRV_GPIO_BUTTON2_EVENT;
        }
        if ((u32Mask & BIT13) != 0U)
        {
            g_u32ButtonEventFlags |= DRV_GPIO_BUTTON3_EVENT;
        }
        if ((u32Mask & BIT12) != 0U)
        {
            g_u32ButtonEventFlags |= DRV_GPIO_BUTTON4_EVENT;
        }
    }

    if (psPort == PC)
    {
        if ((u32Mask & BIT0) != 0U)
        {
            g_u32ButtonEventFlags |= DRV_GPIO_BUTTON5_EVENT;
        }
        if ((u32Mask & BIT1) != 0U)
        {
            g_u32ButtonEventFlags |= DRV_GPIO_BUTTON6_EVENT;
        }
    }
}

void DRV_GPIO_IO_Init(void)
{
    SYS_UnlockReg();
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
    GPIO_SetMode(PB, DRV_GPIO_INT_MASK, GPIO_MODE_INPUT);

    PA0 = 0U;
    PA1 = 0U;
    PA2 = 0U;
    PA3 = 0U;
    PA4 = 0U;
    PA5 = 0U;

    GPIO_SET_DEBOUNCE_TIME(PA, GPIO_DBCTL_DBCLKSRC_LIRC, GPIO_DBCTL_DBCLKSEL_128);
    GPIO_SET_DEBOUNCE_TIME(PC, GPIO_DBCTL_DBCLKSRC_LIRC, GPIO_DBCTL_DBCLKSEL_128);
    GPIO_ENABLE_DEBOUNCE(PA, DRV_GPIO_BUTTON_PA_MASK);
    GPIO_ENABLE_DEBOUNCE(PC, DRV_GPIO_BUTTON_PC_MASK);

    GPIO_CLR_INT_FLAG(PA, DRV_GPIO_BUTTON_PA_MASK);
    GPIO_CLR_INT_FLAG(PC, DRV_GPIO_BUTTON_PC_MASK);
    GPIO_CLR_INT_FLAG(PB, DRV_GPIO_INT_MASK);
    GPIO_EnableInt(PA, 15U, GPIO_INT_RISING);
    GPIO_EnableInt(PA, 14U, GPIO_INT_RISING);
    GPIO_EnableInt(PA, 13U, GPIO_INT_RISING);
    GPIO_EnableInt(PA, 12U, GPIO_INT_RISING);
    GPIO_EnableInt(PC, 0U, GPIO_INT_RISING);
    GPIO_EnableInt(PC, 1U, GPIO_INT_RISING);
    GPIO_EnableInt(PB, 5U, GPIO_INT_BOTH_EDGE);
    GPIO_EnableInt(PB, 4U, GPIO_INT_BOTH_EDGE);
    NVIC_ClearPendingIRQ(GPA_IRQn);
    NVIC_ClearPendingIRQ(GPC_IRQn);
    NVIC_ClearPendingIRQ(EINT0_IRQn);
    NVIC_ClearPendingIRQ(EINT1_IRQn);
    NVIC_EnableIRQ(GPA_IRQn);
    NVIC_EnableIRQ(GPC_IRQn);
    NVIC_EnableIRQ(EINT0_IRQn);
    NVIC_EnableIRQ(EINT1_IRQn);
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

uint32_t DRV_GPIO_IO_GetAndClearButtonEventFlags(void)
{
    uint32_t u32Flags;

    __disable_irq();
    u32Flags = g_u32ButtonEventFlags;
    g_u32ButtonEventFlags = 0U;
    __enable_irq();

    return u32Flags;
}

uint32_t DRV_GPIO_IO_GetInputMask(void)
{
    uint32_t u32Mask;

    u32Mask = 0U;

    if (DRV_GPIO_IO_ReadPin(&g_asInputPin[0]) != 0U)
    {
        u32Mask |= DRV_GPIO_INPUT1_EVENT;
    }

    if (DRV_GPIO_IO_ReadPin(&g_asInputPin[1]) != 0U)
    {
        u32Mask |= DRV_GPIO_INPUT2_EVENT;
    }

    return u32Mask;
}

uint32_t DRV_GPIO_IO_GetAndClearInputEventFlags(void)
{
    uint32_t u32Flags;

    __disable_irq();
    u32Flags = g_u32InputEventFlags;
    g_u32InputEventFlags = 0U;
    __enable_irq();

    return u32Flags;
}

void EINT0_IRQHandler(void)
{
    if (GPIO_GET_INT_FLAG(PB, BIT5) != 0U)
    {
        GPIO_CLR_INT_FLAG(PB, BIT5);
        g_u32InputEventFlags |= DRV_GPIO_INPUT1_EVENT;
    }
}

void EINT1_IRQHandler(void)
{
    if (GPIO_GET_INT_FLAG(PB, BIT4) != 0U)
    {
        GPIO_CLR_INT_FLAG(PB, BIT4);
        g_u32InputEventFlags |= DRV_GPIO_INPUT2_EVENT;
    }
}

void GPA_IRQHandler(void)
{
    uint32_t u32Flags;

    u32Flags = GPIO_GET_INT_FLAG(PA, DRV_GPIO_BUTTON_PA_MASK);
    if (u32Flags != 0U)
    {
        GPIO_CLR_INT_FLAG(PA, u32Flags);
        DRV_GPIO_IO_SetButtonEventByMask(PA, u32Flags);
    }
}

void GPC_IRQHandler(void)
{
    uint32_t u32Flags;

    u32Flags = GPIO_GET_INT_FLAG(PC, DRV_GPIO_BUTTON_PC_MASK);
    if (u32Flags != 0U)
    {
        GPIO_CLR_INT_FLAG(PC, u32Flags);
        DRV_GPIO_IO_SetButtonEventByMask(PC, u32Flags);
    }
}
