/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include "NuMicro.h"

#include "drv_lin_master.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
#define DRV_LIN_MASTER_UART                             UART0
#define DRV_LIN_MASTER_UART_RST                         UART0_RST
#define DRV_LIN_MASTER_TIMEOUT_COUNT                    (SystemCoreClock)

static uint32_t g_u32LinMasterBaudRate = 0U;

/*_____ F U N C T I O N S __________________________________________________*/
static uint8_t DRV_LIN_MASTER_GetProtectedId(uint8_t u8Id)
{
    uint8_t u8P0;
    uint8_t u8P1;

    u8Id &= 0x3FU;
    u8P0 = (uint8_t)(((u8Id >> 0U) ^ (u8Id >> 1U) ^ (u8Id >> 2U) ^ (u8Id >> 4U)) & 0x01U);
    u8P1 = (uint8_t)((~((u8Id >> 1U) ^ (u8Id >> 3U) ^ (u8Id >> 4U) ^ (u8Id >> 5U))) & 0x01U);

    return (uint8_t)(u8Id | (uint8_t)(u8P0 << 6U) | (uint8_t)(u8P1 << 7U));
}

static uint8_t DRV_LIN_MASTER_ComputeChecksum(uint8_t u8Id,
                                             const uint8_t *pu8Data,
                                             uint8_t u8DataLen,
                                             E_DRV_LIN_MASTER_CHECKSUM_MODE eChecksumMode)
{
    uint32_t u32Checksum;
    uint8_t i;

    u32Checksum = 0U;
    if (eChecksumMode == eDRV_LIN_MASTER_CHECKSUM_ENHANCED)
    {
        u32Checksum = DRV_LIN_MASTER_GetProtectedId(u8Id);
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

static void DRV_LIN_MASTER_ConfigLine(void)
{
    UART_SetLineConfig(DRV_LIN_MASTER_UART,
                       g_u32LinMasterBaudRate,
                       UART_WORD_LEN_8,
                       UART_PARITY_NONE,
                       UART_STOP_BIT_1);
    DRV_LIN_MASTER_UART->FUNCSEL = UART_FUNCSEL_LIN;
}

void DRV_LIN_MASTER_SetEnable(uint8_t u8Enable)
{
    PB14 = (u8Enable != 0U) ? 1U : 0U;
}

void DRV_LIN_MASTER_Init(uint32_t u32BaudRate)
{
    g_u32LinMasterBaudRate = u32BaudRate;

    SYS_UnlockReg();
    SET_GPIO_PB14();
    SYS_LockReg();

    GPIO_SetMode(PB, BIT14, GPIO_MODE_OUTPUT);
    DRV_LIN_MASTER_SetEnable(0U);

    SYS_ResetModule(DRV_LIN_MASTER_UART_RST);
    UART_Open(DRV_LIN_MASTER_UART, u32BaudRate);
    DRV_LIN_MASTER_ConfigLine();
    DRV_LIN_MASTER_SetEnable(1U);
}

int32_t DRV_LIN_MASTER_SendPacket(uint8_t u8Id,
                                 const uint8_t *pu8Data,
                                 uint8_t u8DataLen,
                                 E_DRV_LIN_MASTER_CHECKSUM_MODE eChecksumMode)
{
    uint8_t au8TxBuffer[DRV_LIN_MASTER_MAX_DATA_LEN + 1U];
    uint32_t u32TimeOutCnt;
    uint8_t i;

    if ((pu8Data == (const uint8_t *)0) ||
        (u8DataLen == 0U) ||
        (u8DataLen > DRV_LIN_MASTER_MAX_DATA_LEN))
    {
        return -1;
    }

    DRV_LIN_MASTER_ConfigLine();
    DRV_LIN_MASTER_UART->LINCTL = UART_LINCTL_PID((uint8_t)(u8Id & 0x3FU)) |
                                     UART_LINCTL_HSEL_BREAK_SYNC_ID |
                                     UART_LINCTL_BSL(1) |
                                     UART_LINCTL_BRKFL(12) |
                                     UART_LINCTL_IDPEN_Msk;
    DRV_LIN_MASTER_UART->LINCTL |= UART_LINCTL_SENDH_Msk;

    u32TimeOutCnt = DRV_LIN_MASTER_TIMEOUT_COUNT;
    while ((DRV_LIN_MASTER_UART->LINCTL & UART_LINCTL_SENDH_Msk) == UART_LINCTL_SENDH_Msk)
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
    au8TxBuffer[u8DataLen] = DRV_LIN_MASTER_ComputeChecksum(u8Id,
                                                            au8TxBuffer,
                                                            u8DataLen,
                                                            eChecksumMode);

    if (UART_Write(DRV_LIN_MASTER_UART,
                   au8TxBuffer,
                   (uint32_t)u8DataLen + 1UL) != ((uint32_t)u8DataLen + 1UL))
    {
        return -1;
    }

    return 0;
}
