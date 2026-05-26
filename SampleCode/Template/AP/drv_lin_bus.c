/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include <string.h>
#include "NuMicro.h"

#include "drv_lin_bus.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
#define DRV_LIN_BUS_UART                                UART0
#define DRV_LIN_BUS_UART_RST                            UART0_RST
#define DRV_LIN_BUS_TIMEOUT_COUNT                       (SystemCoreClock)

static volatile S_DRV_LIN_BUS_PACKET g_sLinRxPacket;
static volatile uint8_t g_u8LinRxCounter = 0U;
static volatile uint8_t g_u8LinRxReady = 0U;
static volatile uint8_t g_u8LinRxExpectedLen = 0U;
static volatile uint8_t g_u8LinRxId = 0U;
static uint32_t g_u32LinBaudRate = 0U;

/*_____ F U N C T I O N S __________________________________________________*/
#if (ENABLE_LIN_BUS == 1)
static uint8_t DRV_LIN_BUS_ComputeChecksum(const uint8_t *pu8Buf, uint8_t u8ByteCnt)
{
    uint32_t u32CheckSum;
    uint8_t i;

    u32CheckSum = 0U;

    for (i = 0U; i < u8ByteCnt; i++)
    {
        u32CheckSum += pu8Buf[i];
        if (u32CheckSum >= 256U)
        {
            u32CheckSum -= 255U;
        }
    }

    return (uint8_t)(255U - u32CheckSum);
}

static void DRV_LIN_BUS_ConfigLine(void)
{
    UART_SetLineConfig(DRV_LIN_BUS_UART,
                       g_u32LinBaudRate,
                       UART_WORD_LEN_8,
                       UART_PARITY_NONE,
                       UART_STOP_BIT_1);
    DRV_LIN_BUS_UART->FUNCSEL = UART_FUNCSEL_LIN;
}

void DRV_LIN_BUS_SetEnable(uint8_t u8Enable)
{
    PB14 = (u8Enable != 0U) ? 1U : 0U;
}

void DRV_LIN_BUS_Init(uint32_t u32BaudRate)
{
    g_u32LinBaudRate = u32BaudRate;

    SYS_UnlockReg();
    #if 1   // M2A23 EVB
    SET_GPIO_PD15();
    #else
    SET_GPIO_PB14();
    #endif
    SYS_LockReg();

    #if 1   // M2A23 EVB
    PD15 = 1;
    GPIO_SetMode(PD, BIT15, GPIO_MODE_OUTPUT);
    #else
    GPIO_SetMode(PB, BIT14, GPIO_MODE_OUTPUT);
    DRV_LIN_BUS_SetEnable(1U);
    #endif

    SYS_ResetModule(DRV_LIN_BUS_UART_RST);
    UART_Open(DRV_LIN_BUS_UART, u32BaudRate);
    DRV_LIN_BUS_ConfigLine();
}

void DRV_LIN_BUS_StartRx(uint8_t u8Id, uint8_t u8DataLen)
{
    uint32_t u32TimeOutCnt;

    if ((u8DataLen == 0U) || (u8DataLen > DRV_LIN_BUS_MAX_DATA_LEN))
    {
        return;
    }

    u32TimeOutCnt = DRV_LIN_BUS_TIMEOUT_COUNT;

    DRV_LIN_BUS_UART->FIFO |= UART_FIFO_RXRST_Msk;
    while (DRV_LIN_BUS_UART->FIFO & UART_FIFO_RXRST_Msk)
    {
        if (u32TimeOutCnt-- == 0U)
        {
            break;
        }
    }

    __disable_irq();
    memset((void *)&g_sLinRxPacket, 0, sizeof(g_sLinRxPacket));
    g_u8LinRxCounter = 0U;
    g_u8LinRxReady = 0U;
    g_u8LinRxExpectedLen = u8DataLen;
    g_u8LinRxId = (uint8_t)(u8Id & 0x3FU);
    __enable_irq();

    DRV_LIN_BUS_ConfigLine();
    UART_ClearIntFlag(DRV_LIN_BUS_UART, UART_INTSTS_LININT_Msk | UART_INTSTS_RLSINT_Msk | UART_INTSTS_BUFERRINT_Msk);
    UART_ENABLE_INT(DRV_LIN_BUS_UART, UART_INTEN_RDAIEN_Msk | UART_INTEN_LINIEN_Msk | UART_INTEN_RXTOIEN_Msk);
    NVIC_EnableIRQ(UART0_IRQn);

    DRV_LIN_BUS_UART->LINCTL = UART_LINCTL_PID(g_u8LinRxId) |
                               UART_LINCTL_HSEL_BREAK_SYNC_ID |
                               UART_LINCTL_SLVHDEN_Msk |
                               UART_LINCTL_IDPEN_Msk |
                               UART_LINCTL_MUTE_Msk |
                               UART_LINCTL_SLVEN_Msk;
}

int32_t DRV_LIN_BUS_SendPacket(uint8_t u8Id, const uint8_t *pu8Data, uint8_t u8DataLen)
{
    uint8_t au8TxBuffer[DRV_LIN_BUS_MAX_DATA_LEN + 1U];
    uint32_t u32TimeOutCnt;
    uint8_t i;

    if ((pu8Data == (const uint8_t *)0) || (u8DataLen == 0U) || (u8DataLen > DRV_LIN_BUS_MAX_DATA_LEN))
    {
        return -1;
    }

    UART_DISABLE_INT(DRV_LIN_BUS_UART, UART_INTEN_RDAIEN_Msk | UART_INTEN_LINIEN_Msk | UART_INTEN_RXTOIEN_Msk);
    NVIC_DisableIRQ(UART0_IRQn);

    DRV_LIN_BUS_ConfigLine();

    DRV_LIN_BUS_UART->LINCTL = UART_LINCTL_PID((uint8_t)(u8Id & 0x3FU)) |
                               UART_LINCTL_HSEL_BREAK_SYNC_ID |
                               UART_LINCTL_BSL(1) |
                               UART_LINCTL_BRKFL(12) |
                               UART_LINCTL_IDPEN_Msk;
    DRV_LIN_BUS_UART->LINCTL |= UART_LINCTL_SENDH_Msk;

    u32TimeOutCnt = DRV_LIN_BUS_TIMEOUT_COUNT;
    while ((DRV_LIN_BUS_UART->LINCTL & UART_LINCTL_SENDH_Msk) == UART_LINCTL_SENDH_Msk)
    {
        if (u32TimeOutCnt-- == 0U)
        {
            return -1;
        }
    }

    for (i = 0U; i < u8DataLen; i++)
    {
        au8TxBuffer[i] = pu8Data[i];
    }
    au8TxBuffer[u8DataLen] = DRV_LIN_BUS_ComputeChecksum(au8TxBuffer, u8DataLen);

    UART_Write(DRV_LIN_BUS_UART, au8TxBuffer, (uint32_t)u8DataLen + 1UL);
    return 0;
}

uint8_t DRV_LIN_BUS_GetAndClearRxPacket(S_DRV_LIN_BUS_PACKET *psPacket)
{
    uint8_t u8Ready;

    if (psPacket == (S_DRV_LIN_BUS_PACKET *)0)
    {
        return 0U;
    }

    __disable_irq();
    u8Ready = g_u8LinRxReady;
    if (u8Ready != 0U)
    {
        memcpy((void *)psPacket, (const void *)&g_sLinRxPacket, sizeof(S_DRV_LIN_BUS_PACKET));
        g_u8LinRxReady = 0U;
    }
    __enable_irq();

    return u8Ready;
}

void UART0_IRQHandler(void)
{
    uint32_t u32IntSts;
    uint32_t u32Data;

    u32IntSts = DRV_LIN_BUS_UART->INTSTS;

    if ((u32IntSts & UART_INTSTS_LININT_Msk) != 0U)
    {
        if ((DRV_LIN_BUS_UART->LINSTS & UART_LINSTS_SLVHDETF_Msk) != 0U)
        {
            DRV_LIN_BUS_UART->LINSTS = UART_LINSTS_SLVHDETF_Msk;
            g_u8LinRxCounter = 0U;
            g_sLinRxPacket.u8Id = g_u8LinRxId;
            g_sLinRxPacket.u8DataLen = g_u8LinRxExpectedLen;
        }

        if ((DRV_LIN_BUS_UART->LINSTS & (UART_LINSTS_SLVHEF_Msk | UART_LINSTS_SLVIDPEF_Msk | UART_LINSTS_BITEF_Msk)) != 0U)
        {
            DRV_LIN_BUS_UART->LINSTS = UART_LINSTS_SLVHEF_Msk | UART_LINSTS_SLVIDPEF_Msk | UART_LINSTS_BITEF_Msk;
            g_u8LinRxCounter = 0U;
        }
    }

    if ((u32IntSts & (UART_INTSTS_RDAINT_Msk | UART_INTSTS_RXTOINT_Msk)) != 0U)
    {
        while (UART_GET_RX_EMPTY(DRV_LIN_BUS_UART) == 0U)
        {
            u32Data = DRV_LIN_BUS_UART->DAT;

            if (g_u8LinRxCounter < g_u8LinRxExpectedLen)
            {
                g_sLinRxPacket.au8Data[g_u8LinRxCounter] = (uint8_t)u32Data;
            }
            else if (g_u8LinRxCounter == g_u8LinRxExpectedLen)
            {
                g_sLinRxPacket.u8Checksum = (uint8_t)u32Data;
                g_u8LinRxReady = 1U;
            }
            else
            {
            }

            if (g_u8LinRxCounter < (uint8_t)(DRV_LIN_BUS_MAX_DATA_LEN + 1U))
            {
                g_u8LinRxCounter++;
            }
        }
    }

    if ((DRV_LIN_BUS_UART->FIFOSTS & (UART_FIFOSTS_BIF_Msk | UART_FIFOSTS_FEF_Msk | UART_FIFOSTS_PEF_Msk | UART_FIFOSTS_RXOVIF_Msk)) != 0U)
    {
        UART_ClearIntFlag(DRV_LIN_BUS_UART, UART_INTSTS_RLSINT_Msk | UART_INTSTS_BUFERRINT_Msk);
    }
}
#endif