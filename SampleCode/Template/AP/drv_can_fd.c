/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include <string.h>
#include "NuMicro.h"

#include "drv_can_fd.h"
/*_____ D E C L A R A T I O N S ____________________________________________*/

/*_____ D E F I N I T I O N S ______________________________________________*/

CANFD_FD_MSG_T g_sRxMsgFrame;
CANFD_FD_MSG_T g_sTxMsgFrame;
uint8_t g_au8CanRxDataBC[32] = {0};
volatile uint8_t g_u8CanRxDataBCUpdated = 0U;

volatile uint32_t g_u32CanIrqStatus = 0;
static volatile uint8_t g_u8CanBusOffDetected = 0U;
static volatile uint32_t g_u32CanLastPsr = 0U;
static volatile uint32_t g_u32CanLastEcr = 0U;
static volatile uint32_t g_u32CanLastCccr = 0U;
static volatile uint32_t g_u32CanLastTxbrp = 0U;
static uint8_t g_u8CanFdModeOpened = 0U;
static uint8_t g_u8CanTxBufferCount = 0U;
static uint8_t g_u8CanNextTxBuffer = 0U;
static uint8_t g_u8CanRecoveryState = 0U;
static uint8_t g_u8CanErrorLogValid = 0U;
static uint8_t g_u8CanTxFailureReported = 0U;
static uint32_t g_u32CanRecoveryTick = 0U;
static uint32_t g_u32CanErrorLogTick = 0U;
static uint32_t g_u32CanBusOffCount = 0U;
static uint32_t g_u32CanRecoveryCount = 0U;
static uint32_t g_u32CanRecoveryFailCount = 0U;
/*_____ M A C R O S ________________________________________________________*/
#define CANFD_RX_INT_MASK         (CANFD_IR_RF0N_Msk | CANFD_IR_RF0L_Msk | CANFD_IR_RF1N_Msk | CANFD_IR_RF1L_Msk)
#define CANFD_ERROR_INT_MASK      (CANFD_IR_BO_Msk | CANFD_IR_EW_Msk | CANFD_IR_EP_Msk | CANFD_IR_PEA_Msk | \
                                   CANFD_IR_PED_Msk | CANFD_IR_MRAF_Msk)
#define CANFD_ALL_INT_MASK        (CANFD_RX_INT_MASK | CANFD_ERROR_INT_MASK)
#define CANFD_ERROR_IE_MASK       (CANFD_IE_BOE_Msk | CANFD_IE_EWE_Msk | CANFD_IE_EPE_Msk | CANFD_IE_PEAE_Msk | \
                                   CANFD_IE_PEDE_Msk | CANFD_IE_MRAFE_Msk)
#define CANFD_TX_DATA_MAX_LEN     (64U)
#define CANFD_TX_BUFFER_MAX       (32U)
#define CANFD_ERROR_LOG_INTERVAL  (1000U)
#define CANFD_RECOVERY_RETRY_MS   (100U)
#define CANFD_RECOVERY_TIMEOUT_MS (1000U)

#define CANFD_RECOVERY_IDLE       (0U)
#define CANFD_RECOVERY_REQUESTED  (1U)
#define CANFD_RECOVERY_WAIT       (2U)
#define CANFD_RECOVERY_RETRY_WAIT (3U)

/*_____ F U N C T I O N S __________________________________________________*/

static uint8_t CAN_HasElapsed(uint32_t u32Now, uint32_t u32Start, uint32_t u32Interval)
{
    return ((uint32_t)(u32Now - u32Start) >= u32Interval) ? 1U : 0U;
}

static uint32_t CAN_GetTxBufferMask(void)
{
    if (g_u8CanTxBufferCount >= CANFD_TX_BUFFER_MAX)
    {
        return 0xFFFFFFFFUL;
    }

    if (g_u8CanTxBufferCount == 0U)
    {
        return 0U;
    }

    return (1UL << g_u8CanTxBufferCount) - 1UL;
}

static int32_t CAN_FindFreeTxBuffer(void)
{
    uint32_t u32Pending;
    uint8_t u8Offset;
    uint8_t u8BufferIdx;

    u32Pending = CANFD0->TXBRP;
    for (u8Offset = 0U; u8Offset < g_u8CanTxBufferCount; u8Offset++)
    {
        u8BufferIdx = (uint8_t)((g_u8CanNextTxBuffer + u8Offset) % g_u8CanTxBufferCount);
        if ((u32Pending & (1UL << u8BufferIdx)) == 0U)
        {
            return (int32_t)u8BufferIdx;
        }
    }

    return -1;
}

static void CAN_PrintErrorSnapshot(uint32_t u32IrqStatus,
                                   uint32_t u32Psr,
                                   uint32_t u32Ecr,
                                   uint32_t u32Cccr,
                                   uint32_t u32Txbrp)
{
    printf("CAN error: IR=0x%08X PSR=0x%08X ECR=0x%08X CCCR=0x%08X TXBRP=0x%08X\r\n",
           u32IrqStatus, u32Psr, u32Ecr, u32Cccr, u32Txbrp);
}

static void CAN_RequestBusOffRecovery(void)
{
    if (g_u8CanRecoveryState == CANFD_RECOVERY_IDLE)
    {
        g_u8CanRecoveryState = CANFD_RECOVERY_REQUESTED;
        g_u32CanBusOffCount++;
    }
}

static void CAN_LatchPolledBusOff(void)
{
    if ((g_u8CanRecoveryState != CANFD_RECOVERY_IDLE) || (g_u8CanBusOffDetected != 0U))
    {
        return;
    }

    __disable_irq();
    g_u32CanLastPsr = CANFD0->PSR;
    g_u32CanLastEcr = CANFD0->ECR;
    g_u32CanLastCccr = CANFD0->CCCR;
    g_u32CanLastTxbrp = CANFD0->TXBRP;
    g_u8CanBusOffDetected = 1U;
    __enable_irq();
}

static void CAN_ServiceBusOffRecovery(uint32_t u32TickMs)
{
    uint32_t u32TxBufferMask;
    int32_t i32Result;

    if (g_u8CanRecoveryState == CANFD_RECOVERY_IDLE)
    {
        return;
    }

    if (g_u8CanRecoveryState == CANFD_RECOVERY_WAIT)
    {
        if (((CANFD0->PSR & CANFD_PSR_BO_Msk) == 0U) &&
            ((CANFD0->CCCR & CANFD_CCCR_INIT_Msk) == 0U))
        {
            g_u8CanRecoveryState = CANFD_RECOVERY_IDLE;
            g_u32CanRecoveryCount++;
            printf("CAN Bus-Off recovery completed (event=%u, recovered=%u)\r\n",
                   g_u32CanBusOffCount, g_u32CanRecoveryCount);
        }
        else if (CAN_HasElapsed(u32TickMs, g_u32CanRecoveryTick, CANFD_RECOVERY_TIMEOUT_MS) != 0U)
        {
            g_u8CanRecoveryState = CANFD_RECOVERY_RETRY_WAIT;
            g_u32CanRecoveryTick = u32TickMs;
            g_u32CanRecoveryFailCount++;
            printf("CAN Bus-Off recovery timeout (fail=%u), retry pending\r\n",
                   g_u32CanRecoveryFailCount);
        }
        return;
    }

    if (g_u8CanRecoveryState == CANFD_RECOVERY_RETRY_WAIT)
    {
        if (CAN_HasElapsed(u32TickMs, g_u32CanRecoveryTick, CANFD_RECOVERY_RETRY_MS) == 0U)
        {
            return;
        }
        g_u8CanRecoveryState = CANFD_RECOVERY_REQUESTED;
    }

    printf("CAN Bus-Off recovery start (event=%u)\r\n", g_u32CanBusOffCount);
    i32Result = CANFD_RunToNormal(CANFD0, FALSE);
    if (i32Result != CANFD_OK)
    {
        g_u8CanRecoveryState = CANFD_RECOVERY_RETRY_WAIT;
        g_u32CanRecoveryTick = u32TickMs;
        g_u32CanRecoveryFailCount++;
        printf("CAN Bus-Off enter INIT failed (%ld)\r\n", (long)i32Result);
        return;
    }

    u32TxBufferMask = CAN_GetTxBufferMask();
    CANFD0->TXBCR = CANFD0->TXBRP & u32TxBufferMask;
    CANFD0->CCCR &= ~CANFD_CCCR_ASM_Msk;
    CANFD_ClearStatusFlag(CANFD0, CANFD_ERROR_INT_MASK);

    i32Result = CANFD_RunToNormal(CANFD0, TRUE);
    if (i32Result != CANFD_OK)
    {
        g_u8CanRecoveryState = CANFD_RECOVERY_RETRY_WAIT;
        g_u32CanRecoveryTick = u32TickMs;
        g_u32CanRecoveryFailCount++;
        printf("CAN Bus-Off leave INIT failed (%ld)\r\n", (long)i32Result);
        return;
    }

    g_u8CanRecoveryState = CANFD_RECOVERY_WAIT;
    g_u32CanRecoveryTick = u32TickMs;
}

static void CAN_ParseRxMessage(CANFD_FD_MSG_T *psRxMsg)
{
    if (psRxMsg->eIdType != eCANFD_SID)
    {
        return;
    }

    switch (psRxMsg->u32Id)
    {
        case 0xBCU:
            if (psRxMsg->u32DLC < 8U)
            {
                printf("SID 0xBC received, but DLC=%u is shorter than 8 bytes\r\n", psRxMsg->u32DLC);
                return;
            }

            g_au8CanRxDataBC[0] = psRxMsg->au8Data[0];
            g_au8CanRxDataBC[1] = psRxMsg->au8Data[1];
            g_au8CanRxDataBC[2] = psRxMsg->au8Data[2];
            g_au8CanRxDataBC[3] = psRxMsg->au8Data[3];
            g_au8CanRxDataBC[4] = psRxMsg->au8Data[4];
            g_au8CanRxDataBC[5] = psRxMsg->au8Data[5];
            g_au8CanRxDataBC[6] = psRxMsg->au8Data[6];
            g_au8CanRxDataBC[7] = psRxMsg->au8Data[7];

            g_u8CanRxDataBCUpdated = 1U;
            printf("SID 0xBC parsed into g_au8CanRxDataBC[0..7]\r\n");
            break;

        default:
            break;
    }
}

static void CAN_DumpBusStatus(void)
{
    uint8_t u8TxErr = 0;
    uint8_t u8RxErr = 0;

    CANFD_GetBusErrCount(CANFD0, &u8TxErr, &u8RxErr);
    printf("CAN status: PSR=0x%08X ECR=0x%08X IR=0x%08X (TEC=%u REC=%u)\r\n",
           CANFD0->PSR, CANFD0->ECR, CANFD0->IR, u8TxErr, u8RxErr);

    if (CANFD0->IR & CANFD_IR_PED_Msk)
    {
        printf("Hint: PED=1, check FD data phase settings (ISO/Non-ISO mode, data bitrate, BRS).\r\n");
    }
}

static uint32_t CAN_GetModuleClock(CANFD_T *psCanfd)
{
    uint32_t u32ClkSrc = 0U;
    uint32_t u32ClkDiv = 1U;
    uint32_t u32CanClk = 0U;

    if (psCanfd == CANFD0)
    {
        u32ClkSrc = CLK_GetModuleClockSource(CANFD0_MODULE);
        u32ClkDiv = ((CLK->CLKDIV1 & CLK_CLKDIV1_CANFD0DIV_Msk) >> CLK_CLKDIV1_CANFD0DIV_Pos) + 1U;
    }
#if defined(CANFD1)
    else if (psCanfd == CANFD1)
    {
        u32ClkSrc = CLK_GetModuleClockSource(CANFD1_MODULE);
        u32ClkDiv = ((CLK->CLKDIV1 & CLK_CLKDIV1_CANFD1DIV_Msk) >> CLK_CLKDIV1_CANFD1DIV_Pos) + 1U;
    }
#endif
#if defined(CANFD2)
    else if (psCanfd == CANFD2)
    {
        u32ClkSrc = CLK_GetModuleClockSource(CANFD2_MODULE);
        u32ClkDiv = ((CLK->CLKDIV1 & CLK_CLKDIV1_CANFD2DIV_Msk) >> CLK_CLKDIV1_CANFD2DIV_Pos) + 1U;
    }
#endif
    else
    {
        return 0U;
    }

    if (u32ClkSrc == 0U)
    {
        u32CanClk = CLK_GetHXTFreq();
    }
    else if (u32ClkSrc == 1U)
    {
        u32CanClk = CLK_GetPLLClockFreq() / 2U;
    }
    else if (u32ClkSrc == 2U)
    {
        u32CanClk = CLK_GetHCLKFreq();
    }
    else
    {
        u32CanClk = __HIRC;
    }

    return (u32CanClk / u32ClkDiv);
}


static void CAN_PrintRxMessage(uint8_t u8FifoIdx, CANFD_FD_MSG_T *psRxMsg)
{
    uint8_t u8Cnt;

    if (psRxMsg->eIdType == eCANFD_SID)
    {
        printf("Rx FIFO%u [%s] Standard ID = 0x%08X\r\n",
               u8FifoIdx, psRxMsg->bFDFormat ? "CAN FD" : "CAN", psRxMsg->u32Id);
    }
    else
    {
        printf("Rx FIFO%u [%s] Extended ID = 0x%08X\r\n",
               u8FifoIdx, psRxMsg->bFDFormat ? "CAN FD" : "CAN", psRxMsg->u32Id);
    }

    printf("Message Data(%02u bytes) : \r\n", psRxMsg->u32DLC);

    for (u8Cnt = 0; u8Cnt < psRxMsg->u32DLC; u8Cnt++)
    {
        printf("0x%02X ", psRxMsg->au8Data[u8Cnt]);

        if ((u8Cnt+1)%8 ==0)
        {
            printf("\r\n");
        }
    }

    printf("\r\n");
}

static uint32_t CAN_RxFifo0FillLevel(void)
{
    return (CANFD0->RXF0S & CANFD_RXF0S_F0FL_Msk);
}

static uint32_t CAN_RxFifo1FillLevel(void)
{
    return (CANFD0->RXF1S & CANFD_RXF1S_F1FL_Msk);
}

#if (CANFD_MODULE == 0)
void CANFD00_IRQHandler(void)
#elif (CANFD_MODULE == 1)
void CANFD10_IRQHandler(void)
#elif (CANFD_MODULE == 2)
void CANFD20_IRQHandler(void)
#else
void CANFD20_IRQHandler(void)
#endif
{
    uint32_t u32IrqStatus;

    u32IrqStatus = CANFD_GetStatusFlag(CANFD0, CANFD_ALL_INT_MASK);

    if (u32IrqStatus == 0)
    {
        return;
    }

    g_u32CanIrqStatus |= u32IrqStatus;
    if (u32IrqStatus & CANFD_ERROR_INT_MASK)
    {
        g_u32CanLastPsr = CANFD0->PSR;
        g_u32CanLastEcr = CANFD0->ECR;
        g_u32CanLastCccr = CANFD0->CCCR;
        g_u32CanLastTxbrp = CANFD0->TXBRP;

        if ((u32IrqStatus & CANFD_IR_BO_Msk) && (g_u32CanLastPsr & CANFD_PSR_BO_Msk))
        {
            g_u8CanBusOffDetected = 1U;
        }
    }
    CANFD_ClearStatusFlag(CANFD0, u32IrqStatus);
}

void CAN_Process(uint32_t u32TickMs)
{
    uint32_t u32IrqStatus;
    uint32_t u32LastPsr;
    uint32_t u32LastEcr;
    uint32_t u32LastCccr;
    uint32_t u32LastTxbrp;
    uint32_t u32RxResult;
    uint32_t u32Fifo0FillLevel;
    uint32_t u32Fifo1FillLevel;
    uint8_t u8BusOffDetected;

    __disable_irq();
    u32IrqStatus = g_u32CanIrqStatus;
    g_u32CanIrqStatus = 0;
    u8BusOffDetected = g_u8CanBusOffDetected;
    g_u8CanBusOffDetected = 0U;
    u32LastPsr = g_u32CanLastPsr;
    u32LastEcr = g_u32CanLastEcr;
    u32LastCccr = g_u32CanLastCccr;
    u32LastTxbrp = g_u32CanLastTxbrp;
    __enable_irq();

    if (u8BusOffDetected != 0U)
    {
        CAN_PrintErrorSnapshot(u32IrqStatus | CANFD_IR_BO_Msk,
                               u32LastPsr,
                               u32LastEcr,
                               u32LastCccr,
                               u32LastTxbrp);
        CAN_RequestBusOffRecovery();
    }
    else if ((u32IrqStatus & CANFD_ERROR_INT_MASK) != 0U)
    {
        if ((g_u8CanErrorLogValid == 0U) ||
            (CAN_HasElapsed(u32TickMs, g_u32CanErrorLogTick, CANFD_ERROR_LOG_INTERVAL) != 0U))
        {
            g_u8CanErrorLogValid = 1U;
            g_u32CanErrorLogTick = u32TickMs;
            CAN_PrintErrorSnapshot(u32IrqStatus, u32LastPsr, u32LastEcr, u32LastCccr, u32LastTxbrp);
        }
    }

    if ((CANFD0->CCCR & CANFD_CCCR_ASM_Msk) != 0U)
    {
        CANFD0->CCCR &= ~CANFD_CCCR_ASM_Msk;
    }

    if (((CANFD0->PSR & CANFD_PSR_BO_Msk) != 0U) &&
        (g_u8CanRecoveryState == CANFD_RECOVERY_IDLE))
    {
        CAN_PrintErrorSnapshot(CANFD_IR_BO_Msk,
                               CANFD0->PSR,
                               CANFD0->ECR,
                               CANFD0->CCCR,
                               CANFD0->TXBRP);
        CAN_RequestBusOffRecovery();
    }

    CAN_ServiceBusOffRecovery(u32TickMs);

    u32Fifo0FillLevel = CAN_RxFifo0FillLevel();
    u32Fifo1FillLevel = CAN_RxFifo1FillLevel();

    if ((u32IrqStatus == 0U) && (u32Fifo0FillLevel == 0U) && (u32Fifo1FillLevel == 0U))
    {
        return;
    }

    if ((u32IrqStatus & CANFD_IR_RF0L_Msk) || (CANFD0->RXF0S & CANFD_RXF0S_RF0L_Msk))
    {
        printf("Warning: RX FIFO0 message lost\r\n");
    }

    if ((u32IrqStatus & (CANFD_IR_RF0N_Msk | CANFD_IR_RF0L_Msk)) || (u32Fifo0FillLevel != 0U))
    {
        do
        {
            u32RxResult = CANFD_ReadRxFifoMsg(CANFD0, 0, &g_sRxMsgFrame);
            if (u32RxResult != eCANFD_RECEIVE_EMPTY)
            {
                CAN_PrintRxMessage(0, &g_sRxMsgFrame);
                CAN_ParseRxMessage(&g_sRxMsgFrame);
            }
        }
        while ((u32RxResult != eCANFD_RECEIVE_EMPTY) && (CAN_RxFifo0FillLevel() != 0U));
    }

    if ((u32IrqStatus & CANFD_IR_RF1L_Msk) || (CANFD0->RXF1S & CANFD_RXF1S_RF1L_Msk))
    {
        printf("Warning: RX FIFO1 message lost\r\n");
    }

    if ((u32IrqStatus & (CANFD_IR_RF1N_Msk | CANFD_IR_RF1L_Msk)) || (u32Fifo1FillLevel != 0U))
    {
        do
        {
            u32RxResult = CANFD_ReadRxFifoMsg(CANFD0, 1, &g_sRxMsgFrame);
            if (u32RxResult != eCANFD_RECEIVE_EMPTY)
            {
                CAN_PrintRxMessage(1, &g_sRxMsgFrame);
                CAN_ParseRxMessage(&g_sRxMsgFrame);
            }
        }
        while ((u32RxResult != eCANFD_RECEIVE_EMPTY) && (CAN_RxFifo1FillLevel() != 0U));
    }

    memset(&g_sRxMsgFrame, 0, sizeof(g_sRxMsgFrame));
}

E_DRV_CAN_TX_RESULT CAN_SendMessage(uint8_t en_can_fd,
                                    CANFD_FD_MSG_T *psTxMsg,
                                    E_CANFD_ID_TYPE eIdType,
                                    uint32_t u32Id,
                                    uint8_t u8Len)
{
    int32_t i32TxBufferIdx;

    if ((psTxMsg == NULL) || (g_u8CanTxBufferCount == 0U))
    {
        return eDRV_CAN_TX_INVALID;
    }

    if ((en_can_fd != 0U) && (g_u8CanFdModeOpened == 0U))
    {
        printf("Blocked FD TX (ID:0x%08X): controller is in Classical CAN mode\r\n", u32Id);
        printf("Set CAN_APP_ENABLE_FD_MODE=1 and set PCAN to CAN FD mode first.\r\n");
        return eDRV_CAN_TX_INVALID;
    }

    if (((CANFD0->PSR & CANFD_PSR_BO_Msk) != 0U) ||
        (g_u8CanRecoveryState != CANFD_RECOVERY_IDLE))
    {
        if ((CANFD0->PSR & CANFD_PSR_BO_Msk) != 0U)
        {
            CAN_LatchPolledBusOff();
        }
        return eDRV_CAN_TX_BUS_OFF;
    }

    psTxMsg->u32Id = u32Id;
    psTxMsg->eIdType = eIdType;
    psTxMsg->eFrmType = eCANFD_DATA_FRM;
    psTxMsg->bFDFormat = (en_can_fd != 0U) ? 1U : 0U;
    psTxMsg->bBitRateSwitch = (en_can_fd != 0U) ? 1U : 0U;

    if ((en_can_fd == 0U) && (u8Len > 8U))
    {
        u8Len = 8U;
    }
    else if (u8Len > CANFD_TX_DATA_MAX_LEN)
    {
        u8Len = CANFD_TX_DATA_MAX_LEN;
    }

    psTxMsg->u32DLC = u8Len;

    i32TxBufferIdx = CAN_FindFreeTxBuffer();
    if (i32TxBufferIdx < 0)
    {
        return eDRV_CAN_TX_BUSY;
    }

    if (CANFD_TransmitTxMsg(CANFD0, (uint32_t)i32TxBufferIdx, psTxMsg) == eCANFD_TRANSMIT_SUCCESS)
    {
        g_u8CanNextTxBuffer = (uint8_t)(((uint32_t)i32TxBufferIdx + 1U) % g_u8CanTxBufferCount);
        g_u8CanTxFailureReported = 0U;
        return eDRV_CAN_TX_QUEUED;
    }

    if ((CANFD0->PSR & CANFD_PSR_BO_Msk) != 0U)
    {
        CAN_LatchPolledBusOff();
        return eDRV_CAN_TX_BUS_OFF;
    }

    if (g_u8CanTxFailureReported == 0U)
    {
        g_u8CanTxFailureReported = 1U;
        printf("Failed to queue CAN message (ID:0x%08X, buffer=%ld)\r\n",
               psTxMsg->u32Id, (long)i32TxBufferIdx);
        CAN_DumpBusStatus();
    }

    return eDRV_CAN_TX_CONTROLLER_ERROR;
}


/*---------------------------------------------------------------------------*/
/*  Get the CANFD interface Nominal bit rate Function                        */
/*---------------------------------------------------------------------------*/
uint32_t Get_CANFD_NominalBitRate(CANFD_T *psCanfd)
{
    uint32_t u32BitRate = 0;
    uint32_t u32CanClk  = 0;
    uint8_t  u8Tq = 0;
    uint8_t  u8NtSeg1 = 0;
    uint8_t  u8NtSeg2 = 0;

    u32CanClk = CAN_GetModuleClock(psCanfd);
    if (u32CanClk == 0U)
    {
        return 0U;
    }

    u8Tq = ((psCanfd->NBTP & CANFD_NBTP_NBRP_Msk) >> CANFD_NBTP_NBRP_Pos) + 1 ;
    u8NtSeg1 = ((psCanfd->NBTP & CANFD_NBTP_NTSEG1_Msk) >> CANFD_NBTP_NTSEG1_Pos);
    u8NtSeg2 = ((psCanfd->NBTP & CANFD_NBTP_NTSEG2_Msk) >> CANFD_NBTP_NTSEG2_Pos);
    u32BitRate = u32CanClk / u8Tq / (u8NtSeg1 + u8NtSeg2 + 3);

    return u32BitRate;
}
/*---------------------------------------------------------------------------*/
/*  Get the CANFD interface Data bit rate Function                           */
/*---------------------------------------------------------------------------*/
uint32_t Get_CANFD_DataBitRate(CANFD_T *psCanfd)
{
    uint32_t u32BitRate = 0;
    uint32_t u32CanClk  = 0;
    uint8_t  u8Tq = 0;
    uint8_t  u8NtSeg1 = 0;
    uint8_t  u8NtSeg2 = 0;

    u32CanClk = CAN_GetModuleClock(psCanfd);
    if (u32CanClk == 0U)
    {
        return 0U;
    }

    u8Tq = ((psCanfd->DBTP & CANFD_DBTP_DBRP_Msk) >> CANFD_DBTP_DBRP_Pos) + 1 ;
    u8NtSeg1 = ((psCanfd->DBTP & CANFD_DBTP_DTSEG1_Msk) >> CANFD_DBTP_DTSEG1_Pos);
    u8NtSeg2 = ((psCanfd->DBTP & CANFD_DBTP_DTSEG2_Msk) >> CANFD_DBTP_DTSEG2_Pos);
    u32BitRate = u32CanClk / u8Tq / (u8NtSeg1 + u8NtSeg2 + 3);

    return u32BitRate;
}

void CAN_Init(void)
{
    CANFD_FD_T sCANFD_Config;
    uint32_t u32ActualNomBitRate;
    uint32_t u32ActualDataBitRate;

    uint32_t u32NormBitRate = CAN_APP_NOMINAL_BITRATE;
    uint32_t u32DataBitRate = CAN_APP_DATA_BITRATE;

    SYS_ResetModule(CANFD0_RST);

#if (CAN_APP_ENABLE_FD_MODE == 1U)
    CANFD_GetDefaultConfig(&sCANFD_Config, CANFD_OP_CAN_FD_MODE);
    g_u8CanFdModeOpened = 1U;
#else
    CANFD_GetDefaultConfig(&sCANFD_Config, CANFD_OP_CAN_MODE);
    g_u8CanFdModeOpened = 0U;
#endif

    g_u8CanTxBufferCount = (uint8_t)sCANFD_Config.sElemSize.u32TxBuf;
    if (g_u8CanTxBufferCount > CANFD_TX_BUFFER_MAX)
    {
        g_u8CanTxBufferCount = CANFD_TX_BUFFER_MAX;
    }
    g_u8CanNextTxBuffer = 0U;
    g_u8CanRecoveryState = CANFD_RECOVERY_IDLE;
    g_u8CanBusOffDetected = 0U;
    g_u8CanErrorLogValid = 0U;
    g_u8CanTxFailureReported = 0U;
    g_u32CanIrqStatus = 0U;
    g_u32CanLastPsr = 0U;
    g_u32CanLastEcr = 0U;
    g_u32CanLastCccr = 0U;
    g_u32CanLastTxbrp = 0U;
    g_u32CanRecoveryTick = 0U;
    g_u32CanErrorLogTick = 0U;
    g_u32CanBusOffCount = 0U;
    g_u32CanRecoveryCount = 0U;
    g_u32CanRecoveryFailCount = 0U;

    sCANFD_Config.sBtConfig.sNormBitRate.u32BitRate = u32NormBitRate;
    if (g_u8CanFdModeOpened)
    {
        sCANFD_Config.sBtConfig.sDataBitRate.u32BitRate = u32DataBitRate;
    }
    else
    {
        sCANFD_Config.sBtConfig.sDataBitRate.u32BitRate = 0U;
    }
    CANFD_Open(CANFD0, &sCANFD_Config);

#if (CAN_APP_ENABLE_FD_MODE == 1U)
    if (g_u8CanFdModeOpened)
    {
#if (CAN_APP_FD_NON_ISO_MODE == 1U)
        CANFD0->CCCR |= CANFD_CCCR_NISO_Msk;
#else
        CANFD0->CCCR &= ~CANFD_CCCR_NISO_Msk;
#endif
    }
#endif

    u32ActualNomBitRate = Get_CANFD_NominalBitRate(CANFD0);
    u32ActualDataBitRate = Get_CANFD_DataBitRate(CANFD0);

    printf("CAN mode: %s\r\n", g_u8CanFdModeOpened ? "CAN FD" : "Classical CAN");
    if (g_u8CanFdModeOpened)
    {
        printf("FD protocol: %s\r\n", (CANFD0->CCCR & CANFD_CCCR_NISO_Msk) ? "Non-ISO" : "ISO");
    }
    printf("Nominal baud rate(bps): %u\r\n", u32ActualNomBitRate);
    printf("Data baud rate(bps): %u\r\n", u32ActualDataBitRate);
    if (g_u8CanFdModeOpened)
    {
        printf("PCAN setting: CAN FD mode, Nominal=%u, Data=%u\r\n", u32NormBitRate, u32DataBitRate);
        if ((u32ActualNomBitRate != u32NormBitRate) || (u32ActualDataBitRate != u32DataBitRate))
        {
            printf("Warning: CAN FD timing mismatch, check CAN clock source / bitrate settings.\r\n");
        }
    }
    else
    {
        printf("PCAN setting: Classical CAN mode, Baud=%u\r\n", u32NormBitRate);
        if (u32ActualNomBitRate != u32NormBitRate)
        {
            printf("Warning: CAN timing mismatch, check CAN clock source / bitrate settings.\r\n");
        }
    }

    #if defined (ENABLE_MONITOR_MODE)
    /*Enable the Bus Monitoring Mode */
    CANFD0->CCCR |= CANFD_CCCR_MON_Msk;
    #endif

    /* Clear all pending status before enabling interrupt */
    CANFD_ClearStatusFlag(CANFD0, CANFD_INT_ALL_SIGNALS);

    /* Non-matching Frames with Extended ID and Standard ID are stored in Rx FIFO0 or Rx FIFO1, reject all remote frames with 11-bit standard IDs and 29-bit extended IDs */
    CANFD_SetGFC(CANFD0, eCANFD_ACC_NON_MATCH_FRM_RX_FIFO0, eCANFD_ACC_NON_MATCH_FRM_RX_FIFO1, 1, 1);
    /* RX and CAN error interrupts use interrupt line 0. Recovery runs in CAN_Process(). */
    CANFD_EnableInt(CANFD0,
                    (CANFD_IE_RF0NE_Msk | CANFD_IE_RF0LE_Msk | CANFD_IE_RF1NE_Msk | CANFD_IE_RF1LE_Msk |
                     CANFD_ERROR_IE_MASK),
                    0,
                    0,
                    0);

#if (CANFD_MODULE == 0)
    NVIC_EnableIRQ(CANFD00_IRQn);
#elif (CANFD_MODULE == 1)
    NVIC_EnableIRQ(CANFD10_IRQn);
#elif (CANFD_MODULE == 2)
    NVIC_EnableIRQ(CANFD20_IRQn);
#else
    NVIC_EnableIRQ(CANFD20_IRQn);
#endif

    /* CAN FD Run to Normal mode */
    CANFD_RunToNormal(CANFD0, TRUE);
}
