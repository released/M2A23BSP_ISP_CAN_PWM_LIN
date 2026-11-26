/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include <string.h>
#include "NuMicro.h"

#include "drv_lin_slave.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
#define DRV_LIN_SLAVE_UART                              UART0
#define DRV_LIN_SLAVE_UART_RST                          UART0_RST
#define DRV_LIN_SLAVE_TIMEOUT_COUNT                     (SystemCoreClock)
#define DRV_LIN_SLAVE_INVALID_FRAME_INDEX               (0xFFU)

typedef enum
{
    eDRV_LIN_SLAVE_STATE_WAIT_HEADER = 0,
    eDRV_LIN_SLAVE_STATE_WAIT_SYNC,
    eDRV_LIN_SLAVE_STATE_WAIT_PID,
    eDRV_LIN_SLAVE_STATE_RECEIVE,
    eDRV_LIN_SLAVE_STATE_RESPONSE
} E_DRV_LIN_SLAVE_STATE;

typedef struct
{
    uint8_t u8Pid;
    E_DRV_LIN_SLAVE_DIRECTION eDirection;
    uint8_t u8DataLen;
    E_DRV_LIN_SLAVE_CHECKSUM_MODE eChecksumMode;
    uint8_t u8ResponseReady;
    uint8_t au8Data[DRV_LIN_SLAVE_MAX_DATA_LEN];
    volatile uint8_t u8RxReady;
    volatile S_DRV_LIN_SLAVE_PACKET sRxPacket;
} S_DRV_LIN_SLAVE_FRAME;

static volatile S_DRV_LIN_SLAVE_PACKET g_sLinRxWorkingPacket;
static volatile S_DRV_LIN_SLAVE_PACKET g_asLinTxEvent[DRV_LIN_SLAVE_TX_EVENT_COUNT];
static volatile uint8_t g_u8LinTxEventRead = 0U;
static volatile uint8_t g_u8LinTxEventWrite = 0U;
static volatile uint8_t g_u8LinTxEventCount = 0U;
static volatile E_DRV_LIN_SLAVE_STATE g_eLinSlaveState = eDRV_LIN_SLAVE_STATE_WAIT_HEADER;
static volatile uint8_t g_u8LinActiveFrameIndex = DRV_LIN_SLAVE_INVALID_FRAME_INDEX;
static volatile uint8_t g_u8LinRxCounter = 0U;
static volatile uint32_t g_u32LinErrorFlags = 0U;
static volatile uint8_t g_u8LinLastErrorPid = 0U;
static S_DRV_LIN_SLAVE_FRAME g_asLinFrame[DRV_LIN_SLAVE_MAX_FRAME_COUNT];
static uint8_t g_u8LinFrameCount = 0U;
static uint8_t g_au8LinResponse[DRV_LIN_SLAVE_MAX_DATA_LEN + 1U];
static uint8_t g_u8LinTxCounter = 0U;
static uint8_t g_u8LinTxLength = 0U;
static uint32_t g_u32LinSlaveBaudRate = 0U;
static uint8_t g_u8LinStarted = 0U;

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

static uint8_t DRV_LIN_SLAVE_IsProtectedIdValid(uint8_t u8Pid)
{
    return (DRV_LIN_SLAVE_GetProtectedId((uint8_t)(u8Pid & 0x3FU)) == u8Pid) ? 1U : 0U;
}

static uint8_t DRV_LIN_SLAVE_ComputeChecksum(uint8_t u8Pid,
                                            const uint8_t *pu8Data,
                                            uint8_t u8DataLen,
                                            E_DRV_LIN_SLAVE_CHECKSUM_MODE eChecksumMode)
{
    uint32_t u32Checksum;
    uint8_t i;

    u32Checksum = 0U;
    if (eChecksumMode == eDRV_LIN_SLAVE_CHECKSUM_ENHANCED)
    {
        u32Checksum = u8Pid;
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

static int32_t DRV_LIN_SLAVE_FindFrame(uint8_t u8Pid)
{
    uint8_t i;

    for (i = 0U; i < g_u8LinFrameCount; i++)
    {
        if (g_asLinFrame[i].u8Pid == u8Pid)
        {
            return (int32_t)i;
        }
    }

    return -1;
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
    g_eLinSlaveState = eDRV_LIN_SLAVE_STATE_WAIT_HEADER;
    g_u8LinActiveFrameIndex = DRV_LIN_SLAVE_INVALID_FRAME_INDEX;
    g_u8LinRxCounter = 0U;

    /* Use hardware Break detection, not the complete-header decoder or PID filter.
     * With MUTE and SLVHDEN disabled, Sync and PID are ordinary RX FIFO bytes.
     */
    DRV_LIN_SLAVE_UART->LINCTL = UART_LINCTL_BRKDETEN_Msk |
                                 UART_LINCTL_BITERREN_Msk |
                                 UART_LINCTL_SLVEN_Msk;
}

static void DRV_LIN_SLAVE_FillTxFifo(void)
{
    while ((g_u8LinTxCounter < g_u8LinTxLength) &&
           (UART_GET_TX_FULL(DRV_LIN_SLAVE_UART) == 0U))
    {
        UART_WRITE(DRV_LIN_SLAVE_UART, g_au8LinResponse[g_u8LinTxCounter]);
        g_u8LinTxCounter++;
    }

    if (g_u8LinTxCounter == g_u8LinTxLength)
    {
        UART_DISABLE_INT(DRV_LIN_SLAVE_UART, UART_INTEN_THREIEN_Msk);
        UART_ENABLE_INT(DRV_LIN_SLAVE_UART, UART_INTEN_TXENDIEN_Msk);
    }
    else
    {
        UART_ENABLE_INT(DRV_LIN_SLAVE_UART, UART_INTEN_THREIEN_Msk);
    }
}

static void DRV_LIN_SLAVE_StopResponse(void)
{
    UART_DISABLE_INT(DRV_LIN_SLAVE_UART, UART_INTEN_THREIEN_Msk | UART_INTEN_TXENDIEN_Msk);
    g_u8LinTxCounter = 0U;
    g_u8LinTxLength = 0U;
}

static void DRV_LIN_SLAVE_RecordTxPacket(void)
{
    volatile S_DRV_LIN_SLAVE_PACKET *psPacket;
    uint8_t u8DataLen;

    if (g_u8LinTxEventCount >= DRV_LIN_SLAVE_TX_EVENT_COUNT)
    {
        g_u32LinErrorFlags |= DRV_LIN_SLAVE_ERROR_TX_EVENT_OVERFLOW;
        return;
    }

    /* Record the transmitted snapshot, not a newer application payload. */
    psPacket = &g_asLinTxEvent[g_u8LinTxEventWrite];
    u8DataLen = (uint8_t)(g_u8LinTxLength - 1U);
    memset((void *)psPacket, 0, sizeof(*psPacket));
    psPacket->u8Pid = g_asLinFrame[g_u8LinActiveFrameIndex].u8Pid;
    psPacket->u8Id = (uint8_t)(psPacket->u8Pid & 0x3FU);
    psPacket->u8DataLen = u8DataLen;
    memcpy((void *)psPacket->au8Data, g_au8LinResponse, u8DataLen);
    psPacket->u8Checksum = g_au8LinResponse[u8DataLen];
    psPacket->u8ChecksumValid = 1U;
    g_u8LinTxEventWrite++;
    if (g_u8LinTxEventWrite >= DRV_LIN_SLAVE_TX_EVENT_COUNT)
    {
        g_u8LinTxEventWrite = 0U;
    }
    g_u8LinTxEventCount++;
}

static void DRV_LIN_SLAVE_ClearStatus(void)
{
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
}

static int32_t DRV_LIN_SLAVE_ConfigureFrames(void)
{
    uint32_t u32TimeOutCnt;

    UART_DISABLE_INT(DRV_LIN_SLAVE_UART,
                     UART_INTEN_RDAIEN_Msk | UART_INTEN_LINIEN_Msk |
                     UART_INTEN_RXTOIEN_Msk | UART_INTEN_TXENDIEN_Msk |
                     UART_INTEN_THREIEN_Msk | UART_INTEN_RLSIEN_Msk | UART_INTEN_BUFERRIEN_Msk);
    NVIC_DisableIRQ(UART0_IRQn);

    u32TimeOutCnt = DRV_LIN_SLAVE_TIMEOUT_COUNT;
    DRV_LIN_SLAVE_UART->FIFO |= UART_FIFO_RXRST_Msk | UART_FIFO_TXRST_Msk;
    while ((DRV_LIN_SLAVE_UART->FIFO & (UART_FIFO_RXRST_Msk | UART_FIFO_TXRST_Msk)) != 0U)
    {
        if (u32TimeOutCnt-- == 0U)
        {
            return -1;
        }
    }

    DRV_LIN_SLAVE_ConfigLine();
    DRV_LIN_SLAVE_ClearStatus();
    DRV_LIN_SLAVE_StopResponse();
    DRV_LIN_SLAVE_ArmHeaderDetection();

    UART_ENABLE_INT(DRV_LIN_SLAVE_UART,
                    UART_INTEN_RDAIEN_Msk | UART_INTEN_LINIEN_Msk | UART_INTEN_RXTOIEN_Msk |
                    UART_INTEN_RLSIEN_Msk | UART_INTEN_BUFERRIEN_Msk);
    NVIC_EnableIRQ(UART0_IRQn);
    return 0;
}

void DRV_LIN_SLAVE_SetEnable(uint8_t u8Enable)
{
    PB14 = (u8Enable != 0U) ? 1U : 0U;
}

void DRV_LIN_SLAVE_Init(uint32_t u32BaudRate)
{
    NVIC_DisableIRQ(UART0_IRQn);
    UART_DISABLE_INT(DRV_LIN_SLAVE_UART, 0xFFFFFFFFUL);
    g_u32LinSlaveBaudRate = u32BaudRate;
    g_u8LinFrameCount = 0U;
    g_u8LinTxEventRead = 0U;
    g_u8LinTxEventWrite = 0U;
    g_u8LinTxEventCount = 0U;
    g_u32LinErrorFlags = 0U;
    g_u8LinStarted = 0U;
    g_eLinSlaveState = eDRV_LIN_SLAVE_STATE_WAIT_HEADER;
    memset((void *)g_asLinFrame, 0, sizeof(g_asLinFrame));

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

int32_t DRV_LIN_SLAVE_RegisterFrame(uint8_t u8Pid,
                                   E_DRV_LIN_SLAVE_DIRECTION eDirection,
                                   uint8_t u8DataLen,
                                   E_DRV_LIN_SLAVE_CHECKSUM_MODE eChecksumMode)
{
    uint32_t u32Primask;
    int32_t i32Result;
    S_DRV_LIN_SLAVE_FRAME *psFrame;

    if ((DRV_LIN_SLAVE_IsProtectedIdValid(u8Pid) == 0U) ||
        (u8DataLen == 0U) || (u8DataLen > DRV_LIN_SLAVE_MAX_DATA_LEN) ||
        ((eDirection != eDRV_LIN_SLAVE_DIRECTION_RECEIVE) &&
         (eDirection != eDRV_LIN_SLAVE_DIRECTION_RESPONSE)) ||
        ((eChecksumMode != eDRV_LIN_SLAVE_CHECKSUM_CLASSIC) &&
         (eChecksumMode != eDRV_LIN_SLAVE_CHECKSUM_ENHANCED)))
    {
        return DRV_LIN_SLAVE_INVALID_ARGUMENT;
    }

    u32Primask = __get_PRIMASK();
    __disable_irq();
    i32Result = DRV_LIN_SLAVE_OK;
    if (g_eLinSlaveState != eDRV_LIN_SLAVE_STATE_WAIT_HEADER)
    {
        i32Result = DRV_LIN_SLAVE_BUSY;
    }
    else if (DRV_LIN_SLAVE_FindFrame(u8Pid) >= 0)
    {
        i32Result = DRV_LIN_SLAVE_ALREADY_REGISTERED;
    }
    else if (g_u8LinFrameCount >= DRV_LIN_SLAVE_MAX_FRAME_COUNT)
    {
        i32Result = DRV_LIN_SLAVE_REGISTRY_FULL;
    }
    else
    {
        psFrame = &g_asLinFrame[g_u8LinFrameCount];
        memset((void *)psFrame, 0, sizeof(*psFrame));
        psFrame->u8Pid = u8Pid;
        psFrame->eDirection = eDirection;
        psFrame->u8DataLen = u8DataLen;
        psFrame->eChecksumMode = eChecksumMode;
        g_u8LinFrameCount++;
    }
    if (u32Primask == 0U)
    {
        __enable_irq();
    }
    return i32Result;
}

int32_t DRV_LIN_SLAVE_UnregisterFrame(uint8_t u8Pid)
{
    uint32_t u32Primask;
    int32_t i32FrameIndex;
    int32_t i32Result;
    uint8_t i;

    u32Primask = __get_PRIMASK();
    __disable_irq();
    i32FrameIndex = DRV_LIN_SLAVE_FindFrame(u8Pid);
    i32Result = DRV_LIN_SLAVE_OK;
    if (g_eLinSlaveState != eDRV_LIN_SLAVE_STATE_WAIT_HEADER)
    {
        i32Result = DRV_LIN_SLAVE_BUSY;
    }
    else if (i32FrameIndex < 0)
    {
        i32Result = DRV_LIN_SLAVE_NOT_FOUND;
    }
    else
    {
        for (i = (uint8_t)i32FrameIndex; (uint8_t)(i + 1U) < g_u8LinFrameCount; i++)
        {
            g_asLinFrame[i] = g_asLinFrame[i + 1U];
        }
        g_u8LinFrameCount--;
        memset((void *)&g_asLinFrame[g_u8LinFrameCount], 0, sizeof(g_asLinFrame[0]));
    }
    if (u32Primask == 0U)
    {
        __enable_irq();
    }
    return i32Result;
}

int32_t DRV_LIN_SLAVE_Start(void)
{
    int32_t i32Result;

    if ((g_u8LinFrameCount == 0U) || (g_u32LinSlaveBaudRate == 0U) || (g_u8LinStarted != 0U))
    {
        return DRV_LIN_SLAVE_INVALID_ARGUMENT;
    }
    i32Result = DRV_LIN_SLAVE_ConfigureFrames();
    if (i32Result == 0)
    {
        g_u8LinStarted = 1U;
    }
    return i32Result;
}

int32_t DRV_LIN_SLAVE_StartFrames(const S_DRV_LIN_SLAVE_FRAME_CONFIG *psFrameConfig,
                                  uint8_t u8FrameCount)
{
    uint32_t u32Primask;
    uint8_t i;
    uint8_t j;

    if ((psFrameConfig == (const S_DRV_LIN_SLAVE_FRAME_CONFIG *)0) ||
        (u8FrameCount == 0U) ||
        (u8FrameCount > DRV_LIN_SLAVE_MAX_FRAME_COUNT))
    {
        return -1;
    }

    for (i = 0U; i < u8FrameCount; i++)
    {
        if ((DRV_LIN_SLAVE_IsProtectedIdValid(psFrameConfig[i].u8Pid) == 0U) ||
            (psFrameConfig[i].u8DataLen == 0U) ||
            (psFrameConfig[i].u8DataLen > DRV_LIN_SLAVE_MAX_DATA_LEN) ||
            (psFrameConfig[i].eDirection > eDRV_LIN_SLAVE_DIRECTION_RESPONSE) ||
            (psFrameConfig[i].eChecksumMode > eDRV_LIN_SLAVE_CHECKSUM_ENHANCED) ||
            ((psFrameConfig[i].eDirection == eDRV_LIN_SLAVE_DIRECTION_RESPONSE) &&
             (psFrameConfig[i].pu8InitialData == (const uint8_t *)0)))
        {
            return -1;
        }

        for (j = 0U; j < i; j++)
        {
            if (psFrameConfig[j].u8Pid == psFrameConfig[i].u8Pid)
            {
                return -1;
            }
        }
    }

    UART_DISABLE_INT(DRV_LIN_SLAVE_UART,
                     UART_INTEN_RDAIEN_Msk | UART_INTEN_LINIEN_Msk |
                     UART_INTEN_RXTOIEN_Msk | UART_INTEN_TXENDIEN_Msk |
                     UART_INTEN_THREIEN_Msk | UART_INTEN_RLSIEN_Msk | UART_INTEN_BUFERRIEN_Msk);
    NVIC_DisableIRQ(UART0_IRQn);

    u32Primask = __get_PRIMASK();
    __disable_irq();
    memset((void *)g_asLinFrame, 0, sizeof(g_asLinFrame));
    memset((void *)&g_sLinRxWorkingPacket, 0, sizeof(g_sLinRxWorkingPacket));
    g_u8LinFrameCount = u8FrameCount;
    g_u8LinTxEventRead = 0U;
    g_u8LinTxEventWrite = 0U;
    g_u8LinTxEventCount = 0U;
    g_u32LinErrorFlags = 0U;

    for (i = 0U; i < u8FrameCount; i++)
    {
        g_asLinFrame[i].u8Pid = psFrameConfig[i].u8Pid;
        g_asLinFrame[i].eDirection = psFrameConfig[i].eDirection;
        g_asLinFrame[i].u8DataLen = psFrameConfig[i].u8DataLen;
        g_asLinFrame[i].eChecksumMode = psFrameConfig[i].eChecksumMode;
        if (psFrameConfig[i].eDirection == eDRV_LIN_SLAVE_DIRECTION_RESPONSE)
        {
            memcpy(g_asLinFrame[i].au8Data,
                   psFrameConfig[i].pu8InitialData,
                   psFrameConfig[i].u8DataLen);
            g_asLinFrame[i].u8ResponseReady = 1U;
        }
    }

    if (u32Primask == 0U)
    {
        __enable_irq();
    }

    g_u8LinStarted = 0U;
    return DRV_LIN_SLAVE_Start();
}

int32_t DRV_LIN_SLAVE_SetResponseData(uint8_t u8Pid,
                                     const uint8_t *pu8Data,
                                     uint8_t u8DataLen)
{
    uint32_t u32Primask;
    int32_t i32FrameIndex;

    if ((pu8Data == (const uint8_t *)0) ||
        (u8DataLen == 0U) ||
        (u8DataLen > DRV_LIN_SLAVE_MAX_DATA_LEN))
    {
        return DRV_LIN_SLAVE_INVALID_ARGUMENT;
    }

    u32Primask = __get_PRIMASK();
    __disable_irq();
    i32FrameIndex = DRV_LIN_SLAVE_FindFrame(u8Pid);
    if (i32FrameIndex < 0)
    {
        if (u32Primask == 0U)
        {
            __enable_irq();
        }
        return DRV_LIN_SLAVE_NOT_FOUND;
    }
    if ((g_asLinFrame[i32FrameIndex].eDirection != eDRV_LIN_SLAVE_DIRECTION_RESPONSE) ||
        (g_asLinFrame[i32FrameIndex].u8DataLen != u8DataLen))
    {
        if (u32Primask == 0U)
        {
            __enable_irq();
        }
        return -1;
    }

    memcpy(g_asLinFrame[i32FrameIndex].au8Data, pu8Data, u8DataLen);
    g_asLinFrame[i32FrameIndex].u8ResponseReady = 1U;
    if (u32Primask == 0U)
    {
        __enable_irq();
    }

    return 0;
}

int32_t DRV_LIN_SLAVE_StartReceive(uint8_t u8Id,
                                  uint8_t u8DataLen,
                                  E_DRV_LIN_SLAVE_CHECKSUM_MODE eChecksumMode)
{
    S_DRV_LIN_SLAVE_FRAME_CONFIG sFrameConfig;

    sFrameConfig.u8Pid = DRV_LIN_SLAVE_GetProtectedId(u8Id);
    sFrameConfig.eDirection = eDRV_LIN_SLAVE_DIRECTION_RECEIVE;
    sFrameConfig.u8DataLen = u8DataLen;
    sFrameConfig.eChecksumMode = eChecksumMode;
    sFrameConfig.pu8InitialData = (const uint8_t *)0;

    return DRV_LIN_SLAVE_StartFrames(&sFrameConfig, 1U);
}

int32_t DRV_LIN_SLAVE_StartResponse(uint8_t u8Id,
                                   const uint8_t *pu8Data,
                                   uint8_t u8DataLen,
                                   E_DRV_LIN_SLAVE_CHECKSUM_MODE eChecksumMode)
{
    S_DRV_LIN_SLAVE_FRAME_CONFIG sFrameConfig;

    sFrameConfig.u8Pid = DRV_LIN_SLAVE_GetProtectedId(u8Id);
    sFrameConfig.eDirection = eDRV_LIN_SLAVE_DIRECTION_RESPONSE;
    sFrameConfig.u8DataLen = u8DataLen;
    sFrameConfig.eChecksumMode = eChecksumMode;
    sFrameConfig.pu8InitialData = pu8Data;

    return DRV_LIN_SLAVE_StartFrames(&sFrameConfig, 1U);
}

uint8_t DRV_LIN_SLAVE_GetAndClearRxPacketByPid(uint8_t u8Pid,
                                             S_DRV_LIN_SLAVE_PACKET *psPacket)
{
    uint32_t u32Primask;
    int32_t i32FrameIndex;
    uint8_t u8Ready;

    if (psPacket == (S_DRV_LIN_SLAVE_PACKET *)0)
    {
        return 0U;
    }

    u32Primask = __get_PRIMASK();
    __disable_irq();
    u8Ready = 0U;
    i32FrameIndex = DRV_LIN_SLAVE_FindFrame(u8Pid);
    if ((i32FrameIndex >= 0) && (g_asLinFrame[i32FrameIndex].u8RxReady != 0U))
    {
        memcpy((void *)psPacket, (const void *)&g_asLinFrame[i32FrameIndex].sRxPacket,
               sizeof(S_DRV_LIN_SLAVE_PACKET));
        g_asLinFrame[i32FrameIndex].u8RxReady = 0U;
        u8Ready = 1U;
    }
    if (u32Primask == 0U)
    {
        __enable_irq();
    }

    return u8Ready;
}

uint8_t DRV_LIN_SLAVE_GetAndClearRxPacket(S_DRV_LIN_SLAVE_PACKET *psPacket)
{
    uint32_t u32Primask;
    uint8_t u8Ready;
    uint8_t i;

    if (psPacket == (S_DRV_LIN_SLAVE_PACKET *)0)
    {
        return 0U;
    }

    u32Primask = __get_PRIMASK();
    __disable_irq();
    u8Ready = 0U;
    for (i = 0U; i < g_u8LinFrameCount; i++)
    {
        if (g_asLinFrame[i].u8RxReady != 0U)
        {
            memcpy((void *)psPacket, (const void *)&g_asLinFrame[i].sRxPacket,
                   sizeof(S_DRV_LIN_SLAVE_PACKET));
            g_asLinFrame[i].u8RxReady = 0U;
            u8Ready = 1U;
            break;
        }
    }
    if (u32Primask == 0U)
    {
        __enable_irq();
    }

    return u8Ready;
}

uint8_t DRV_LIN_SLAVE_GetAndClearTxPacket(S_DRV_LIN_SLAVE_PACKET *psPacket)
{
    uint32_t u32Primask;
    uint8_t u8Ready;

    if (psPacket == (S_DRV_LIN_SLAVE_PACKET *)0)
    {
        return 0U;
    }

    u32Primask = __get_PRIMASK();
    __disable_irq();
    u8Ready = 0U;
    if (g_u8LinTxEventCount != 0U)
    {
        memcpy((void *)psPacket, (const void *)&g_asLinTxEvent[g_u8LinTxEventRead],
               sizeof(S_DRV_LIN_SLAVE_PACKET));
        g_u8LinTxEventRead++;
        if (g_u8LinTxEventRead >= DRV_LIN_SLAVE_TX_EVENT_COUNT)
        {
            g_u8LinTxEventRead = 0U;
        }
        g_u8LinTxEventCount--;
        u8Ready = 1U;
    }
    if (u32Primask == 0U)
    {
        __enable_irq();
    }

    return u8Ready;
}

uint32_t DRV_LIN_SLAVE_GetAndClearErrorFlags(uint8_t *pu8LastPid)
{
    uint32_t u32Primask;
    uint32_t u32ErrorFlags;

    u32Primask = __get_PRIMASK();
    __disable_irq();
    u32ErrorFlags = g_u32LinErrorFlags;
    g_u32LinErrorFlags = 0U;
    if (pu8LastPid != (uint8_t *)0)
    {
        *pu8LastPid = g_u8LinLastErrorPid;
    }
    if (u32Primask == 0U)
    {
        __enable_irq();
    }

    return u32ErrorFlags;
}

void UART0_IRQHandler(void)
{
    uint32_t u32IntSts;
    uint32_t u32LinSts;
    uint32_t u32FifoSts;
    uint32_t u32Data;
    int32_t i32FrameIndex;
    uint8_t u8Pid;
    uint8_t u8DataLen;
    uint8_t i;

    u32IntSts = DRV_LIN_SLAVE_UART->INTSTS;
    u32LinSts = DRV_LIN_SLAVE_UART->LINSTS;
    u32FifoSts = DRV_LIN_SLAVE_UART->FIFOSTS;

    if ((u32IntSts & UART_INTSTS_LININT_Msk) != 0U)
    {
        if ((u32LinSts &
             (UART_LINSTS_SLVHEF_Msk | UART_LINSTS_SLVIDPEF_Msk |
              UART_LINSTS_SLVHTOF_Msk | UART_LINSTS_RTOUTF_Msk |
              UART_LINSTS_BITEF_Msk)) != 0U)
        {
            DRV_LIN_SLAVE_ClearStatus();
            g_u32LinErrorFlags |= (g_eLinSlaveState == eDRV_LIN_SLAVE_STATE_RESPONSE) ?
                                  DRV_LIN_SLAVE_ERROR_TX : DRV_LIN_SLAVE_ERROR_HEADER;
            DRV_LIN_SLAVE_StopResponse();
            DRV_LIN_SLAVE_UART->FIFO |= UART_FIFO_RXRST_Msk | UART_FIFO_TXRST_Msk;
            DRV_LIN_SLAVE_ArmHeaderDetection();
        }
        else if ((u32LinSts & UART_LINSTS_BRKDETF_Msk) != 0U)
        {
            DRV_LIN_SLAVE_UART->LINSTS = UART_LINSTS_BRKDETF_Msk;
            if (g_eLinSlaveState == eDRV_LIN_SLAVE_STATE_RESPONSE)
            {
                g_u32LinErrorFlags |= DRV_LIN_SLAVE_ERROR_TX;
                DRV_LIN_SLAVE_UART->FIFO |= UART_FIFO_TXRST_Msk;
            }
            DRV_LIN_SLAVE_StopResponse();
            g_eLinSlaveState = eDRV_LIN_SLAVE_STATE_WAIT_SYNC;
            g_u8LinActiveFrameIndex = DRV_LIN_SLAVE_INVALID_FRAME_INDEX;
            g_u8LinRxCounter = 0U;
        }
        else
        {
        }
    }

    if ((u32FifoSts & UART_FIFOSTS_RXOVIF_Msk) != 0U)
    {
        g_u32LinErrorFlags |= DRV_LIN_SLAVE_ERROR_RX_OVERFLOW;
        DRV_LIN_SLAVE_StopResponse();
        DRV_LIN_SLAVE_UART->FIFO |= UART_FIFO_RXRST_Msk | UART_FIFO_TXRST_Msk;
        DRV_LIN_SLAVE_ArmHeaderDetection();
        UART_ClearIntFlag(DRV_LIN_SLAVE_UART, UART_INTSTS_RLSINT_Msk | UART_INTSTS_BUFERRINT_Msk);
        return;
    }

    if ((u32FifoSts & (UART_FIFOSTS_BIF_Msk | UART_FIFOSTS_FEF_Msk | UART_FIFOSTS_PEF_Msk)) != 0U)
    {
        /* A Break can also set UART BIF/FEF before BRKDETF arrives. */
        if ((g_eLinSlaveState != eDRV_LIN_SLAVE_STATE_WAIT_HEADER) &&
            (g_eLinSlaveState != eDRV_LIN_SLAVE_STATE_WAIT_SYNC))
        {
            g_u32LinErrorFlags |= DRV_LIN_SLAVE_ERROR_HEADER;
            DRV_LIN_SLAVE_StopResponse();
            DRV_LIN_SLAVE_UART->FIFO |= UART_FIFO_TXRST_Msk;
            DRV_LIN_SLAVE_ArmHeaderDetection();
        }
        UART_ClearIntFlag(DRV_LIN_SLAVE_UART, UART_INTSTS_RLSINT_Msk);
    }

    if ((u32IntSts & (UART_INTSTS_RDAINT_Msk | UART_INTSTS_RXTOINT_Msk)) != 0U)
    {
        while (UART_GET_RX_EMPTY(DRV_LIN_SLAVE_UART) == 0U)
        {
            u32Data = UART_READ(DRV_LIN_SLAVE_UART);

            if (g_eLinSlaveState == eDRV_LIN_SLAVE_STATE_WAIT_SYNC)
            {
                if ((uint8_t)u32Data == 0x55U)
                {
                    g_eLinSlaveState = eDRV_LIN_SLAVE_STATE_WAIT_PID;
                }
                else if ((uint8_t)u32Data != 0U)
                {
                    /* Ignore only the zero character generated by the Break. */
                    g_u32LinErrorFlags |= DRV_LIN_SLAVE_ERROR_HEADER;
                    DRV_LIN_SLAVE_ArmHeaderDetection();
                }
                else
                {
                }
            }
            else if (g_eLinSlaveState == eDRV_LIN_SLAVE_STATE_WAIT_PID)
            {
                u8Pid = (uint8_t)u32Data;
                if (DRV_LIN_SLAVE_IsProtectedIdValid(u8Pid) == 0U)
                {
                    g_u8LinLastErrorPid = u8Pid;
                    g_u32LinErrorFlags |= DRV_LIN_SLAVE_ERROR_PID_PARITY;
                    DRV_LIN_SLAVE_ArmHeaderDetection();
                }
                else
                {
                    i32FrameIndex = DRV_LIN_SLAVE_FindFrame(u8Pid);
                    if (i32FrameIndex < 0)
                    {
                        DRV_LIN_SLAVE_ArmHeaderDetection();
                    }
                    else
                    {
                        g_u8LinActiveFrameIndex = (uint8_t)i32FrameIndex;
                        if (g_asLinFrame[i32FrameIndex].eDirection == eDRV_LIN_SLAVE_DIRECTION_RESPONSE)
                        {
                            if (g_asLinFrame[i32FrameIndex].u8ResponseReady == 0U)
                            {
                                DRV_LIN_SLAVE_ArmHeaderDetection();
                                continue;
                            }
                            u8DataLen = g_asLinFrame[i32FrameIndex].u8DataLen;
                            for (i = 0U; i < u8DataLen; i++)
                            {
                                g_au8LinResponse[i] = g_asLinFrame[i32FrameIndex].au8Data[i];
                            }
                            g_au8LinResponse[u8DataLen] =
                                DRV_LIN_SLAVE_ComputeChecksum(u8Pid,
                                                              g_au8LinResponse,
                                                              u8DataLen,
                                                              g_asLinFrame[i32FrameIndex].eChecksumMode);
                            g_eLinSlaveState = eDRV_LIN_SLAVE_STATE_RESPONSE;
                            g_u8LinTxCounter = 0U;
                            g_u8LinTxLength = (uint8_t)(u8DataLen + 1U);
                            DRV_LIN_SLAVE_FillTxFifo();
                        }
                        else
                        {
                            memset((void *)&g_sLinRxWorkingPacket, 0, sizeof(g_sLinRxWorkingPacket));
                            g_sLinRxWorkingPacket.u8Id = (uint8_t)(u8Pid & 0x3FU);
                            g_sLinRxWorkingPacket.u8Pid = u8Pid;
                            g_sLinRxWorkingPacket.u8DataLen = g_asLinFrame[i32FrameIndex].u8DataLen;
                            g_u8LinRxCounter = 0U;
                            g_eLinSlaveState = eDRV_LIN_SLAVE_STATE_RECEIVE;
                        }
                    }
                }
            }
            else if ((g_eLinSlaveState == eDRV_LIN_SLAVE_STATE_RECEIVE) &&
                     (g_u8LinActiveFrameIndex < g_u8LinFrameCount))
            {
                u8DataLen = g_asLinFrame[g_u8LinActiveFrameIndex].u8DataLen;
                if (g_u8LinRxCounter < u8DataLen)
                {
                    g_sLinRxWorkingPacket.au8Data[g_u8LinRxCounter] = (uint8_t)u32Data;
                    g_u8LinRxCounter++;
                }
                else
                {
                    g_sLinRxWorkingPacket.u8Checksum = (uint8_t)u32Data;
                    g_sLinRxWorkingPacket.u8ChecksumValid =
                        (DRV_LIN_SLAVE_ComputeChecksum(g_sLinRxWorkingPacket.u8Pid,
                                                       (const uint8_t *)g_sLinRxWorkingPacket.au8Data,
                                                       u8DataLen,
                                                       g_asLinFrame[g_u8LinActiveFrameIndex].eChecksumMode) ==
                         (uint8_t)u32Data) ? 1U : 0U;
                    if (g_asLinFrame[g_u8LinActiveFrameIndex].u8RxReady == 0U)
                    {
                        memcpy((void *)&g_asLinFrame[g_u8LinActiveFrameIndex].sRxPacket,
                               (const void *)&g_sLinRxWorkingPacket,
                               sizeof(S_DRV_LIN_SLAVE_PACKET));
                        g_asLinFrame[g_u8LinActiveFrameIndex].u8RxReady = 1U;
                    }
                    else
                    {
                        g_u32LinErrorFlags |= DRV_LIN_SLAVE_ERROR_RX_OVERFLOW;
                    }
                    DRV_LIN_SLAVE_ArmHeaderDetection();
                }
            }
            else
            {
                /* Ignore UART RX echo while publishing a slave response. */
            }
        }
    }

    if ((g_eLinSlaveState == eDRV_LIN_SLAVE_STATE_RESPONSE) &&
        (UART_GET_INT_FLAG(DRV_LIN_SLAVE_UART, UART_INTSTS_THREINT_Msk) != 0U))
    {
        DRV_LIN_SLAVE_FillTxFifo();
    }

    /* Re-read status: the entry snapshot may predate writes to the TX FIFO. */
    if ((g_eLinSlaveState == eDRV_LIN_SLAVE_STATE_RESPONSE) &&
        (g_u8LinTxCounter == g_u8LinTxLength) &&
        (UART_GET_INT_FLAG(DRV_LIN_SLAVE_UART, UART_INTSTS_TXENDINT_Msk) != 0U))
    {
        DRV_LIN_SLAVE_RecordTxPacket();
        DRV_LIN_SLAVE_StopResponse();
        DRV_LIN_SLAVE_ArmHeaderDetection();
    }
}
#endif
