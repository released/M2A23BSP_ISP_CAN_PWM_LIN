/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include <string.h>
#include "NuMicro.h"

#include "drv_lin_slave.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
#define DRV_LIN_SLAVE_UART                              UART0
#define DRV_LIN_SLAVE_UART_RST                          UART0_RST
#define DRV_LIN_SLAVE_TIMEOUT_COUNT                     (SystemCoreClock)

typedef enum
{
    eDRV_LIN_SLAVE_MODE_IDLE = 0,
    eDRV_LIN_SLAVE_MODE_RECEIVE,
    eDRV_LIN_SLAVE_MODE_RESPONSE
} E_DRV_LIN_SLAVE_MODE;

static volatile S_DRV_LIN_SLAVE_PACKET g_sLinRxPacket;
static volatile E_DRV_LIN_SLAVE_MODE g_eLinSlaveMode = eDRV_LIN_SLAVE_MODE_IDLE;
static volatile E_DRV_LIN_SLAVE_CHECKSUM_MODE g_eLinRxChecksumMode = eDRV_LIN_SLAVE_CHECKSUM_CLASSIC;
static volatile uint8_t g_u8LinRxCounter = 0U;
static volatile uint8_t g_u8LinRxReady = 0U;
static volatile uint8_t g_u8LinRxExpectedLen = 0U;
static volatile uint8_t g_u8LinActiveId = 0U;
static uint8_t g_au8LinResponse[DRV_LIN_SLAVE_MAX_DATA_LEN + 1U];
static uint8_t g_u8LinResponseLen = 0U;
static uint32_t g_u32LinSlaveBaudRate = 0U;

/*_____ F U N C T I O N S __________________________________________________*/
#if (ENABLE_LIN_SLAVE == 1)
static uint8_t DRV_LIN_SLAVE_GetProtectedId(uint8_t u8Id)
{
    uint8_t u8P0;
    uint8_t u8P1;

    u8Id &= 0x3FU;
    u8P0 = (uint8_t)(((u8Id >> 0U) ^ (u8Id >> 1U) ^ (u8Id >> 2U) ^ (u8Id >> 4U)) & 0x01U);
    u8P1 = (uint8_t)((~((u8Id >> 1U) ^ (u8Id >> 3U) ^ (u8Id >> 4U) ^ (u8Id >> 5U))) & 0x01U);

    return (uint8_t)(u8Id | (uint8_t)(u8P0 << 6U) | (uint8_t)(u8P1 << 7U));
}

static uint8_t DRV_LIN_SLAVE_ComputeChecksum(uint8_t u8Id,
                                            const uint8_t *pu8Data,
                                            uint8_t u8DataLen,
                                            E_DRV_LIN_SLAVE_CHECKSUM_MODE eChecksumMode)
{
    uint32_t u32Checksum;
    uint8_t i;

    u32Checksum = 0U;
    if (eChecksumMode == eDRV_LIN_SLAVE_CHECKSUM_ENHANCED)
    {
        u32Checksum = DRV_LIN_SLAVE_GetProtectedId(u8Id);
    }

    for (i = 0U; i < u8DataLen; i++)
    {
        u32Checksum += pu8Data[i];
        if (u32Checksum >= 256U)
        {
            u32Checksum -= 255U;
        }
    }

    return (uint8_t)(255U - u32Checksum);
}

static void DRV_LIN_SLAVE_ConfigLine(void)
{
    UART_SetLineConfig(DRV_LIN_SLAVE_UART,
                       g_u32LinSlaveBaudRate,
                       UART_WORD_LEN_8,
                       UART_PARITY_NONE,
                       UART_STOP_BIT_1);
    DRV_LIN_SLAVE_UART->FUNCSEL = UART_FUNCSEL_LIN;
}

static void DRV_LIN_SLAVE_ArmHeaderDetection(void)
{
    DRV_LIN_SLAVE_UART->LINCTL = UART_LINCTL_PID(g_u8LinActiveId) |
                                    UART_LINCTL_HSEL_BREAK_SYNC_ID |
                                    UART_LINCTL_SLVHDEN_Msk |
                                    UART_LINCTL_IDPEN_Msk |
                                    UART_LINCTL_BITERREN_Msk |
                                    UART_LINCTL_MUTE_Msk |
                                    UART_LINCTL_SLVEN_Msk;
}

static int32_t DRV_LIN_SLAVE_ConfigureFrame(uint8_t u8Id)
{
    uint32_t u32TimeOutCnt;

    UART_DISABLE_INT(DRV_LIN_SLAVE_UART,
                     UART_INTEN_RDAIEN_Msk | UART_INTEN_LINIEN_Msk |
                     UART_INTEN_RXTOIEN_Msk | UART_INTEN_TXENDIEN_Msk);
    NVIC_DisableIRQ(UART0_IRQn);

    u32TimeOutCnt = DRV_LIN_SLAVE_TIMEOUT_COUNT;
    DRV_LIN_SLAVE_UART->FIFO |= UART_FIFO_RXRST_Msk;
    while ((DRV_LIN_SLAVE_UART->FIFO & UART_FIFO_RXRST_Msk) != 0U)
    {
        if (u32TimeOutCnt-- == 0U)
        {
            return -1;
        }
    }

    DRV_LIN_SLAVE_ConfigLine();
    DRV_LIN_SLAVE_UART->LINSTS = UART_LINSTS_SLVHDETF_Msk |
                                 UART_LINSTS_SLVHEF_Msk |
                                 UART_LINSTS_SLVIDPEF_Msk |
                                 UART_LINSTS_SLVSYNCF_Msk |
                                 UART_LINSTS_SLVHTOF_Msk |
                                 UART_LINSTS_RTOUTF_Msk |
                                 UART_LINSTS_BRKDETF_Msk |
                                 UART_LINSTS_BITEF_Msk;
    UART_ClearIntFlag(DRV_LIN_SLAVE_UART,
                      UART_INTSTS_LININT_Msk | UART_INTSTS_RLSINT_Msk | UART_INTSTS_BUFERRINT_Msk);
    g_u8LinActiveId = (uint8_t)(u8Id & 0x3FU);
    DRV_LIN_SLAVE_ArmHeaderDetection();

    UART_ENABLE_INT(DRV_LIN_SLAVE_UART,
                    UART_INTEN_RDAIEN_Msk | UART_INTEN_LINIEN_Msk | UART_INTEN_RXTOIEN_Msk);
    NVIC_EnableIRQ(UART0_IRQn);
    return 0;
}

void DRV_LIN_SLAVE_SetEnable(uint8_t u8Enable)
{
    PB14 = (u8Enable != 0U) ? 1U : 0U;
}

void DRV_LIN_SLAVE_Init(uint32_t u32BaudRate)
{
    g_u32LinSlaveBaudRate = u32BaudRate;

    SYS_UnlockReg();
    SET_GPIO_PB14();
    SYS_LockReg();

    GPIO_SetMode(PB, BIT14, GPIO_MODE_OUTPUT);
    DRV_LIN_SLAVE_SetEnable(0U);

    SYS_ResetModule(DRV_LIN_SLAVE_UART_RST);
    UART_Open(DRV_LIN_SLAVE_UART, u32BaudRate);
    DRV_LIN_SLAVE_ConfigLine();
    DRV_LIN_SLAVE_SetEnable(1U);
}

int32_t DRV_LIN_SLAVE_StartReceive(uint8_t u8Id,
                                  uint8_t u8DataLen,
                                  E_DRV_LIN_SLAVE_CHECKSUM_MODE eChecksumMode)
{
    uint32_t u32Primask;

    if ((u8DataLen == 0U) || (u8DataLen > DRV_LIN_SLAVE_MAX_DATA_LEN))
    {
        return -1;
    }

    UART_DISABLE_INT(DRV_LIN_SLAVE_UART,
                     UART_INTEN_RDAIEN_Msk | UART_INTEN_LINIEN_Msk |
                     UART_INTEN_RXTOIEN_Msk | UART_INTEN_TXENDIEN_Msk);
    NVIC_DisableIRQ(UART0_IRQn);

    u32Primask = __get_PRIMASK();
    __disable_irq();
    memset((void *)&g_sLinRxPacket, 0, sizeof(g_sLinRxPacket));
    g_eLinSlaveMode = eDRV_LIN_SLAVE_MODE_RECEIVE;
    g_eLinRxChecksumMode = eChecksumMode;
    g_u8LinRxCounter = 0U;
    g_u8LinRxReady = 0U;
    g_u8LinRxExpectedLen = u8DataLen;
    g_u8LinActiveId = (uint8_t)(u8Id & 0x3FU);
    if (u32Primask == 0U)
    {
        __enable_irq();
    }

    return DRV_LIN_SLAVE_ConfigureFrame(g_u8LinActiveId);
}

int32_t DRV_LIN_SLAVE_StartResponse(uint8_t u8Id,
                                   const uint8_t *pu8Data,
                                   uint8_t u8DataLen,
                                   E_DRV_LIN_SLAVE_CHECKSUM_MODE eChecksumMode)
{
    uint32_t u32Primask;
    uint8_t i;

    if ((pu8Data == (const uint8_t *)0) ||
        (u8DataLen == 0U) ||
        (u8DataLen > DRV_LIN_SLAVE_MAX_DATA_LEN))
    {
        return -1;
    }

    UART_DISABLE_INT(DRV_LIN_SLAVE_UART,
                     UART_INTEN_RDAIEN_Msk | UART_INTEN_LINIEN_Msk |
                     UART_INTEN_RXTOIEN_Msk | UART_INTEN_TXENDIEN_Msk);
    NVIC_DisableIRQ(UART0_IRQn);

    u32Primask = __get_PRIMASK();
    __disable_irq();
    g_eLinSlaveMode = eDRV_LIN_SLAVE_MODE_RESPONSE;
    g_u8LinActiveId = (uint8_t)(u8Id & 0x3FU);
    g_u8LinResponseLen = (uint8_t)(u8DataLen + 1U);
    for (i = 0U; i < u8DataLen; i++)
    {
        g_au8LinResponse[i] = pu8Data[i];
    }
    g_au8LinResponse[u8DataLen] = DRV_LIN_SLAVE_ComputeChecksum(g_u8LinActiveId,
                                                                g_au8LinResponse,
                                                                u8DataLen,
                                                                eChecksumMode);
    if (u32Primask == 0U)
    {
        __enable_irq();
    }

    return DRV_LIN_SLAVE_ConfigureFrame(g_u8LinActiveId);
}

uint8_t DRV_LIN_SLAVE_GetAndClearRxPacket(S_DRV_LIN_SLAVE_PACKET *psPacket)
{
    uint32_t u32Primask;
    uint8_t u8Ready;

    if (psPacket == (S_DRV_LIN_SLAVE_PACKET *)0)
    {
        return 0U;
    }

    u32Primask = __get_PRIMASK();
    __disable_irq();
    u8Ready = g_u8LinRxReady;
    if (u8Ready != 0U)
    {
        memcpy((void *)psPacket, (const void *)&g_sLinRxPacket, sizeof(S_DRV_LIN_SLAVE_PACKET));
        g_u8LinRxReady = 0U;
    }
    if (u32Primask == 0U)
    {
        __enable_irq();
    }

    return u8Ready;
}

void UART0_IRQHandler(void)
{
    uint32_t u32IntSts;
    uint32_t u32LinSts;
    uint32_t u32Data;
    uint32_t u32WriteCount;

    u32IntSts = DRV_LIN_SLAVE_UART->INTSTS;
    u32LinSts = DRV_LIN_SLAVE_UART->LINSTS;

    if ((u32IntSts & UART_INTSTS_LININT_Msk) != 0U)
    {
        if ((u32LinSts &
             (UART_LINSTS_SLVHEF_Msk | UART_LINSTS_SLVIDPEF_Msk |
              UART_LINSTS_SLVHTOF_Msk | UART_LINSTS_BITEF_Msk)) != 0U)
        {
            DRV_LIN_SLAVE_UART->LINSTS = UART_LINSTS_SLVHDETF_Msk |
                                         UART_LINSTS_SLVHEF_Msk |
                                         UART_LINSTS_SLVIDPEF_Msk |
                                         UART_LINSTS_SLVSYNCF_Msk |
                                         UART_LINSTS_SLVHTOF_Msk |
                                         UART_LINSTS_BITEF_Msk;
            g_u8LinRxCounter = 0U;
            DRV_LIN_SLAVE_ArmHeaderDetection();
        }
        else if ((u32LinSts & UART_LINSTS_SLVHDETF_Msk) != 0U)
        {
            DRV_LIN_SLAVE_UART->LINSTS = UART_LINSTS_SLVHDETF_Msk;
            if ((DRV_LIN_SLAVE_UART->LINCTL & UART_LINCTL_MUTE_Msk) != 0U)
            {
                /* The header PID did not match the configured slave PID. */
            }
            else if (g_eLinSlaveMode == eDRV_LIN_SLAVE_MODE_RECEIVE)
            {
                g_u8LinRxCounter = 0U;
                g_sLinRxPacket.u8Id = g_u8LinActiveId;
                g_sLinRxPacket.u8DataLen = g_u8LinRxExpectedLen;
            }
            else if (g_eLinSlaveMode == eDRV_LIN_SLAVE_MODE_RESPONSE)
            {
                u32WriteCount = UART_Write(DRV_LIN_SLAVE_UART,
                                           g_au8LinResponse,
                                           g_u8LinResponseLen);
                if (u32WriteCount != g_u8LinResponseLen)
                {
                    DRV_LIN_SLAVE_UART->FIFO |= UART_FIFO_TXRST_Msk;
                    DRV_LIN_SLAVE_ArmHeaderDetection();
                }
                else
                {
                    UART_ENABLE_INT(DRV_LIN_SLAVE_UART, UART_INTEN_TXENDIEN_Msk);
                }
            }
            else
            {
            }
        }

    }

    if ((u32IntSts & (UART_INTSTS_RDAINT_Msk | UART_INTSTS_RXTOINT_Msk)) != 0U)
    {
        while (UART_GET_RX_EMPTY(DRV_LIN_SLAVE_UART) == 0U)
        {
            u32Data = DRV_LIN_SLAVE_UART->DAT;

            if (g_eLinSlaveMode == eDRV_LIN_SLAVE_MODE_RECEIVE)
            {
                if (g_u8LinRxCounter < g_u8LinRxExpectedLen)
                {
                    g_sLinRxPacket.au8Data[g_u8LinRxCounter] = (uint8_t)u32Data;
                }
                else if (g_u8LinRxCounter == g_u8LinRxExpectedLen)
                {
                    g_sLinRxPacket.u8Checksum = (uint8_t)u32Data;
                    g_sLinRxPacket.u8ChecksumValid =
                        (DRV_LIN_SLAVE_ComputeChecksum(g_u8LinActiveId,
                                                       (const uint8_t *)g_sLinRxPacket.au8Data,
                                                       g_u8LinRxExpectedLen,
                                                       g_eLinRxChecksumMode) == (uint8_t)u32Data) ? 1U : 0U;
                    g_u8LinRxReady = 1U;
                    DRV_LIN_SLAVE_ArmHeaderDetection();
                }
                else
                {
                }

                if (g_u8LinRxCounter < (uint8_t)(DRV_LIN_SLAVE_MAX_DATA_LEN + 1U))
                {
                    g_u8LinRxCounter++;
                }
            }
        }
    }

    if ((u32IntSts & UART_INTSTS_TXENDINT_Msk) != 0U)
    {
        UART_DISABLE_INT(DRV_LIN_SLAVE_UART, UART_INTEN_TXENDIEN_Msk);
        DRV_LIN_SLAVE_ArmHeaderDetection();
    }

    if ((DRV_LIN_SLAVE_UART->FIFOSTS &
         (UART_FIFOSTS_BIF_Msk | UART_FIFOSTS_FEF_Msk |
          UART_FIFOSTS_PEF_Msk | UART_FIFOSTS_RXOVIF_Msk)) != 0U)
    {
        UART_ClearIntFlag(DRV_LIN_SLAVE_UART,
                          UART_INTSTS_RLSINT_Msk | UART_INTSTS_BUFERRINT_Msk);
    }
}
#endif
