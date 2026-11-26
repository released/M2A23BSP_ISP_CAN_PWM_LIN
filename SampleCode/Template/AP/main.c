/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include <string.h>
#include "NuMicro.h"
#include "memory_map.h"

#include "misc_config.h"
#include "timer_service.h"
#include "drv_can_fd.h"
#include "drv_pwm.h"
#include "drv_adc.h"
#include "drv_lin_slave.h"
#include "drv_gpio_io.h"

/*_____ D E C L A R A T I O N S ____________________________________________*/
volatile struct flag_32bit flag_PROJ_CTL;
#define FLAG_PROJ_ERASE_CHECKSUM                        (flag_PROJ_CTL.bit1)
#define FLAG_PROJ_STANDBY_REQUEST                       (flag_PROJ_CTL.bit2)
#define FLAG_PROJ_SEND_CAN_1                            (flag_PROJ_CTL.bit3)
#define FLAG_PROJ_SEND_CAN_2                            (flag_PROJ_CTL.bit4)

/*_____ D E F I N I T I O N S ______________________________________________*/
#define APP_DEBUG_UART                                  UART1
#define APP_DEBUG_UART_RST                              UART1_RST
#define APP_DEBUG_UART_IRQn                             UART1_IRQn
#define APP_DEBUG_UART_IRQHandler                       UART1_IRQHandler

#define APP_DEBUG_UART_BAUD_RATE                        (115200U)
#define APP_DEBUG_UART_INT_MASK                         (UART_INTEN_RDAIEN_Msk | UART_INTEN_RXTOIEN_Msk)

#define LIN_SLAVE_UART_BAUD_RATE                        (19200U)
#define LIN_SLAVE_PID_RESPONSE_2E                       (0x2EU)
#define LIN_SLAVE_PID_RECEIVE_C1                        (0xC1U)
#define LIN_SLAVE_PID_RESPONSE_D8                       (0xD8U)
#define LIN_SLAVE_PID_RECEIVE_50                        (0x50U)
#define LIN_SLAVE_PID_RESPONSE_E2                       (0xE2U)
#define LIN_SLAVE_PID_RESPONSE_A3                       (0xA3U)
#define LIN_SLAVE_PID_RECEIVE_42                        (0x42U)
#define LIN_SLAVE_PID_RECEIVE_03                        (0x03U)
/* 1U: D8/E2 byte 0 advances every second; 0U: retain initial test payloads. */
#define LIN_SLAVE_DYNAMIC_TX_EXAMPLE                    (1U)

volatile unsigned long counter_systick = 0;
volatile uint32_t counter_tick = 0;

static int g_timer_id_task1 = -1;
static int g_timer_id_task2 = -1;
static int g_timer_id_task3 = -1;
static uint16_t g_au16InputMeasureFreqHz[eDRV_GPIO_INPUT_MEASURE_MAX] = {0U, 0U};
static uint8_t g_au8InputMeasureDuty[eDRV_GPIO_INPUT_MEASURE_MAX] = {0U, 0U};

static uint8_t g_u8LinResponseSequence = 0U;
static uint8_t g_u8LinReady = 0U;
/* Independent application buffers; only valid received packets update them. */
static uint8_t g_au8LinRxC1[8];
static uint8_t g_au8LinRx50[8];
static uint8_t g_au8LinRx42[2];
static uint8_t g_au8LinRx03[4];

#define ENABLE_ADC_LOG                                  (0U)
#define ENABLE_INPUT_MEASURE_LOG                        (0U)
/*_____ F U N C T I O N S __________________________________________________*/
void SendChar_ToUART(int ch)
{
    if ((char)ch == '\n')
    {
        while (APP_DEBUG_UART->FIFOSTS & UART_FIFOSTS_TXFULL_Msk)
        {
        }
        APP_DEBUG_UART->DAT = '\r';
    }

    while (APP_DEBUG_UART->FIFOSTS & UART_FIFOSTS_TXFULL_Msk)
    {
    }
    APP_DEBUG_UART->DAT = (uint32_t)ch;
}

unsigned long get_systick(void)
{
    return counter_systick;
}

void set_systick(unsigned long t)
{
    counter_systick = t;
}

void systick_counter(void)
{
    counter_systick++;
}

void SysTick_Handler(void)
{
    systick_counter();

#if defined (ENABLE_TICK_EVENT)
    TickCheckTickEvent();
#endif
}

void SysTick_delay(unsigned long delay)
{
    unsigned long tickstart;
    unsigned long wait;

    tickstart = get_systick();
    wait = delay;

    while ((get_systick() - tickstart) < wait)
    {
    }
}

void SysTick_enable(unsigned long ticks_per_second)
{
    set_systick(0);
    if (SysTick_Config(SystemCoreClock / ticks_per_second))
    {
        printf("Set system tick error!!\n");
        while (1)
        {
        }
    }

#if defined (ENABLE_TICK_EVENT)
    TickInitTickEvent();
#endif
}

uint32_t get_tick(void)
{
    return counter_tick;
}

void set_tick(uint32_t t)
{
    counter_tick = t;
}

void tick_counter(void)
{
    counter_tick++;
}

void delay_ms(uint16_t ms)
{
    uint32_t start;

    start = get_tick();
    while ((uint32_t)(get_tick() - start) < (uint32_t)ms)
    {
    }
}

static void WDT_Init(void)
{
    int32_t i32Ret;

    /* WDT register is write-protected */
    SYS_UnlockReg();

    /* To check if system has been reset by WDT time-out reset or not */
    if (WDT_GET_RESET_FLAG() == 1U)
    {
        WDT_CLEAR_RESET_FLAG();
    }

    /*
     * WDT clock source in SYS_Init() is LIRC.
     *
     * Timeout table:
     * TOUTSEL  2^N     Time
     * 0000     2^4     0.417 ms
     * 0001     2^6     1.667 ms
     * 0010     2^8     6.667 ms
     * 0011     2^10    26.667 ms
     * 0100     2^12    106.667 ms
     * 0101     2^14    426.667 ms
     * 0110     2^16    1.707 s
     * 0111     2^18    6.827 s
     * 1000     2^20    27.307 s
     *
     * Reset delay table:
     * 3 clocks      78.125 us
     * 18 clocks     468.75 us
     * 130 clocks    3.385 ms
     * 1026 clocks   26.719 ms
     */
    /* Start WDT:
     * timeout      : 2^18 WDT clocks
     *                = 6.827 s
     * reset delay  : 18 WDT clocks
     *                = 468.75 us after timeout
     * reset enable : TRUE
     * wakeup       : FALSE
     */
    i32Ret = WDT_Open(WDT_TIMEOUT_2POW18,
                      WDT_RESET_DELAY_18CLK,
                      TRUE,
                      FALSE);
    if (i32Ret != WDT_OK)
    {
        printf("WDT open failed: %ld\r\n", (long)i32Ret);
        while (1)
        {
        }
    }

    /* Feed once after open */
    WDT_RESET_COUNTER();

    SYS_LockReg();
}

static int32_t APP_UpdateLinResponse(void)
{
    uint8_t au8Response2E[1];
    uint8_t au8ResponseD8[8];
    uint8_t au8ResponseE2[2];
    uint8_t au8ResponseA3[4];
    int32_t i32Result;

    /* Compose current application data here, outside the UART ISR. */
    au8Response2E[0] = 0x00U;
    i32Result = DRV_LIN_SLAVE_SetResponseData(LIN_SLAVE_PID_RESPONSE_2E, au8Response2E, 1U);
    if ((i32Result != DRV_LIN_SLAVE_OK) && (i32Result != DRV_LIN_SLAVE_NOT_FOUND))
    {
        return i32Result;
    }

    au8ResponseD8[0] = (uint8_t)(0x10U + g_u8LinResponseSequence);
    au8ResponseD8[1] = 0x20U;
    au8ResponseD8[2] = 0x30U;
    au8ResponseD8[3] = 0x40U;
    au8ResponseD8[4] = 0x50U;
    au8ResponseD8[5] = 0x60U;
    au8ResponseD8[6] = 0x70U;
    au8ResponseD8[7] = 0x80U;
    i32Result = DRV_LIN_SLAVE_SetResponseData(LIN_SLAVE_PID_RESPONSE_D8, au8ResponseD8, 8U);
    if ((i32Result != DRV_LIN_SLAVE_OK) && (i32Result != DRV_LIN_SLAVE_NOT_FOUND))
    {
        return i32Result;
    }

    au8ResponseE2[0] = g_u8LinResponseSequence;
    au8ResponseE2[1] = 0xA5U;
    i32Result = DRV_LIN_SLAVE_SetResponseData(LIN_SLAVE_PID_RESPONSE_E2, au8ResponseE2, 2U);
    if ((i32Result != DRV_LIN_SLAVE_OK) && (i32Result != DRV_LIN_SLAVE_NOT_FOUND))
    {
        return i32Result;
    }

    au8ResponseA3[0] = 0xDEU;
    au8ResponseA3[1] = 0xADU;
    au8ResponseA3[2] = 0xBEU;
    au8ResponseA3[3] = 0xEFU;
    i32Result = DRV_LIN_SLAVE_SetResponseData(LIN_SLAVE_PID_RESPONSE_A3, au8ResponseA3, 4U);

    /* Removing a registration disables it without requiring an updater edit. */
    return (i32Result == DRV_LIN_SLAVE_NOT_FOUND) ? DRV_LIN_SLAVE_OK : i32Result;
}

static int32_t APP_InitLinFrames(void)
{
    int32_t i32Result;

    /* Register each PID independently; no application frame count is needed. */
    i32Result = DRV_LIN_SLAVE_RegisterFrame(LIN_SLAVE_PID_RESPONSE_2E,
                                           eDRV_LIN_SLAVE_DIRECTION_RESPONSE,
                                           1U, DRV_LIN_SLAVE_DEFAULT_CHECKSUM_MODE);
    if (i32Result != DRV_LIN_SLAVE_OK)
    {
        return i32Result;
    }
    i32Result = DRV_LIN_SLAVE_RegisterFrame(LIN_SLAVE_PID_RECEIVE_C1,
                                           eDRV_LIN_SLAVE_DIRECTION_RECEIVE,
                                           8U, DRV_LIN_SLAVE_DEFAULT_CHECKSUM_MODE);
    if (i32Result != DRV_LIN_SLAVE_OK)
    {
        return i32Result;
    }
    i32Result = DRV_LIN_SLAVE_RegisterFrame(LIN_SLAVE_PID_RESPONSE_D8,
                                           eDRV_LIN_SLAVE_DIRECTION_RESPONSE,
                                           8U, DRV_LIN_SLAVE_DEFAULT_CHECKSUM_MODE);
    if (i32Result != DRV_LIN_SLAVE_OK)
    {
        return i32Result;
    }
    /* Standard ID 0x10 uses PID 0x50, not parity-invalid PID 0xD0. */
    i32Result = DRV_LIN_SLAVE_RegisterFrame(LIN_SLAVE_PID_RECEIVE_50,
                                           eDRV_LIN_SLAVE_DIRECTION_RECEIVE,
                                           8U, DRV_LIN_SLAVE_DEFAULT_CHECKSUM_MODE);
    if (i32Result != DRV_LIN_SLAVE_OK)
    {
        return i32Result;
    }

    i32Result = DRV_LIN_SLAVE_RegisterFrame(LIN_SLAVE_PID_RESPONSE_E2,
                                           eDRV_LIN_SLAVE_DIRECTION_RESPONSE,
                                           2U, DRV_LIN_SLAVE_DEFAULT_CHECKSUM_MODE);
    if (i32Result != DRV_LIN_SLAVE_OK)
    {
        return i32Result;
    }
    i32Result = DRV_LIN_SLAVE_RegisterFrame(LIN_SLAVE_PID_RESPONSE_A3,
                                           eDRV_LIN_SLAVE_DIRECTION_RESPONSE,
                                           4U, DRV_LIN_SLAVE_DEFAULT_CHECKSUM_MODE);
    if (i32Result != DRV_LIN_SLAVE_OK)
    {
        return i32Result;
    }
    i32Result = DRV_LIN_SLAVE_RegisterFrame(LIN_SLAVE_PID_RECEIVE_42,
                                           eDRV_LIN_SLAVE_DIRECTION_RECEIVE,
                                           2U, DRV_LIN_SLAVE_DEFAULT_CHECKSUM_MODE);
    if (i32Result != DRV_LIN_SLAVE_OK)
    {
        return i32Result;
    }
    i32Result = DRV_LIN_SLAVE_RegisterFrame(LIN_SLAVE_PID_RECEIVE_03,
                                           eDRV_LIN_SLAVE_DIRECTION_RECEIVE,
                                           4U, DRV_LIN_SLAVE_DEFAULT_CHECKSUM_MODE);
    if (i32Result != DRV_LIN_SLAVE_OK)
    {
        return i32Result;
    }

    i32Result = APP_UpdateLinResponse();
    if (i32Result != DRV_LIN_SLAVE_OK)
    {
        return i32Result;
    }
    i32Result = DRV_LIN_SLAVE_Start();
    g_u8LinReady = (i32Result == DRV_LIN_SLAVE_OK) ? 1U : 0U;
    return i32Result;
}

static void APP_PrintLinRxPacket(const S_DRV_LIN_SLAVE_PACKET *psPacket)
{
    uint8_t i;

    printf("LIN RX PID=0x%02X ID=0x%02X LEN=%u DATA=",
           psPacket->u8Pid,
           psPacket->u8Id,
           psPacket->u8DataLen);
    for (i = 0U; i < psPacket->u8DataLen; i++)
    {
        printf("%02X ", psPacket->au8Data[i]);
    }
    printf("CHK=%02X %s\r\n",
           psPacket->u8Checksum,
           (psPacket->u8ChecksumValid != 0U) ? "OK" : "ERROR");
}

static void APP_ProcessLinRx(void)
{
    S_DRV_LIN_SLAVE_PACKET sLinPacket;
    uint32_t u32ErrorFlags;
    uint8_t u8LastPid;

    u32ErrorFlags = DRV_LIN_SLAVE_GetAndClearErrorFlags(&u8LastPid);
    if ((u32ErrorFlags & DRV_LIN_SLAVE_ERROR_PID_PARITY) != 0U)
    {
        printf("LIN PID parity error: 0x%02X\r\n", u8LastPid);
    }
    if ((u32ErrorFlags & DRV_LIN_SLAVE_ERROR_HEADER) != 0U)
    {
        printf("LIN header error\r\n");
    }
    if ((u32ErrorFlags & DRV_LIN_SLAVE_ERROR_TX) != 0U)
    {
        printf("LIN response TX error\r\n");
    }
    if ((u32ErrorFlags & DRV_LIN_SLAVE_ERROR_RX_OVERFLOW) != 0U)
    {
        printf("LIN RX event overflow\r\n");
    }
    if ((u32ErrorFlags & DRV_LIN_SLAVE_ERROR_TX_EVENT_OVERFLOW) != 0U)
    {
        printf("LIN TX log overflow\r\n");
    }

    if (DRV_LIN_SLAVE_GetAndClearRxPacketByPid(LIN_SLAVE_PID_RECEIVE_C1, &sLinPacket) != 0U)
    {
        APP_PrintLinRxPacket(&sLinPacket);
        if ((sLinPacket.u8ChecksumValid != 0U) && (sLinPacket.u8DataLen == sizeof(g_au8LinRxC1)))
        {
            memcpy(g_au8LinRxC1, sLinPacket.au8Data, sizeof(g_au8LinRxC1));
            /* Handle PID C1 application data in g_au8LinRxC1 here. */
        }
    }

    if (DRV_LIN_SLAVE_GetAndClearRxPacketByPid(LIN_SLAVE_PID_RECEIVE_50, &sLinPacket) != 0U)
    {
        APP_PrintLinRxPacket(&sLinPacket);
        if ((sLinPacket.u8ChecksumValid != 0U) && (sLinPacket.u8DataLen == sizeof(g_au8LinRx50)))
        {
            memcpy(g_au8LinRx50, sLinPacket.au8Data, sizeof(g_au8LinRx50));
            /* Handle PID 50 application data in g_au8LinRx50 here. */
        }
    }

    if (DRV_LIN_SLAVE_GetAndClearRxPacketByPid(LIN_SLAVE_PID_RECEIVE_42, &sLinPacket) != 0U)
    {
        APP_PrintLinRxPacket(&sLinPacket);
        if ((sLinPacket.u8ChecksumValid != 0U) && (sLinPacket.u8DataLen == sizeof(g_au8LinRx42)))
        {
            memcpy(g_au8LinRx42, sLinPacket.au8Data, sizeof(g_au8LinRx42));
            /* Handle the two PID 42 bytes in g_au8LinRx42 here. */
        }
    }

    if (DRV_LIN_SLAVE_GetAndClearRxPacketByPid(LIN_SLAVE_PID_RECEIVE_03, &sLinPacket) != 0U)
    {
        APP_PrintLinRxPacket(&sLinPacket);
        if ((sLinPacket.u8ChecksumValid != 0U) && (sLinPacket.u8DataLen == sizeof(g_au8LinRx03)))
        {
            memcpy(g_au8LinRx03, sLinPacket.au8Data, sizeof(g_au8LinRx03));
            /* Handle the four PID 03 bytes in g_au8LinRx03 here. */
        }
    }
}

static void APP_ProcessLinTx(void)
{
    S_DRV_LIN_SLAVE_PACKET sLinPacket;
    uint8_t u8Event;
    uint8_t i;

    /* Bound foreground logging work even when the master keeps polling. */
    for (u8Event = 0U; u8Event < DRV_LIN_SLAVE_TX_EVENT_COUNT; u8Event++)
    {
        if (DRV_LIN_SLAVE_GetAndClearTxPacket(&sLinPacket) == 0U)
        {
            break;
        }
        printf("LIN TX PID=0x%02X ID=0x%02X LEN=%u DATA=",
               sLinPacket.u8Pid,
               sLinPacket.u8Id,
               sLinPacket.u8DataLen);
        for (i = 0U; i < sLinPacket.u8DataLen; i++)
        {
            printf("%02X ", sLinPacket.au8Data[i]);
        }
        printf("CHK=%02X DONE\r\n", sLinPacket.u8Checksum);
    }
}

static void APP_ProcessADCEvents(void)
{
    #if (ENABLE_ADC_LOG == 1)
    printf("ADC AVDD=%lumV, B0(PB0/CH0)=%u(%lumV), B1(PB2/CH2)=%u(%lumV)\r\n",
           (unsigned long)DRV_ADC_GetAvddMv(),
           DRV_ADC_GetSample(eDRV_ADC_B0),
           (unsigned long)DRV_ADC_GetSampleMilliVolt(eDRV_ADC_B0),
           DRV_ADC_GetSample(eDRV_ADC_B1),
           (unsigned long)DRV_ADC_GetSampleMilliVolt(eDRV_ADC_B1));
    #endif
}

static void APP_ProcessInputMeasureEvents(void)
{
    uint16_t u16FreqHz;
    uint8_t u8Duty;

    if (DRV_GPIO_IO_GetAndClearInputMeasure(eDRV_GPIO_INPUT_MEASURE1, &u16FreqHz, &u8Duty) != 0U)
    {
        g_au16InputMeasureFreqHz[eDRV_GPIO_INPUT_MEASURE1] = u16FreqHz;
        g_au8InputMeasureDuty[eDRV_GPIO_INPUT_MEASURE1] = u8Duty;
    }

    if (DRV_GPIO_IO_GetAndClearInputMeasure(eDRV_GPIO_INPUT_MEASURE2, &u16FreqHz, &u8Duty) != 0U)
    {
        g_au16InputMeasureFreqHz[eDRV_GPIO_INPUT_MEASURE2] = u16FreqHz;
        g_au8InputMeasureDuty[eDRV_GPIO_INPUT_MEASURE2] = u8Duty;
    }
}

void Task_1000ms_Callback(void *user_data)
{
    UNUSED(user_data);

    PA0 = (PA0 == 0U) ? 1U : 0U;
    APP_ProcessADCEvents();

    #if ((ENABLE_LIN_SLAVE == 1) && (LIN_SLAVE_DYNAMIC_TX_EXAMPLE == 1))
    if (g_u8LinReady != 0U)
    {
        g_u8LinResponseSequence++;
        if (APP_UpdateLinResponse() != DRV_LIN_SLAVE_OK)
        {
            printf("LIN payload update failed\r\n");
        }
    }
    #endif

    #if (ENABLE_INPUT_MEASURE_LOG == 1)
    printf("INPUT_MEASURE");
    printf(" IN1 duty=%u%% freq=%uHz",
           (unsigned int)g_au8InputMeasureDuty[eDRV_GPIO_INPUT_MEASURE1],
           (unsigned int)g_au16InputMeasureFreqHz[eDRV_GPIO_INPUT_MEASURE1]);
    printf(" IN2 duty=%u%% freq=%uHz",
           (unsigned int)g_au8InputMeasureDuty[eDRV_GPIO_INPUT_MEASURE2],
           (unsigned int)g_au16InputMeasureFreqHz[eDRV_GPIO_INPUT_MEASURE2]);
    printf("\r\n");
    #endif
}

void Task_100ms_Callback(void *user_data)
{
    UNUSED(user_data);
}

void Task_10ms_Callback(void *user_data)
{
    UNUSED(user_data);
    DRV_ADC_SampleAll();
}

void TimerService_CreateTask(void)
{
    g_timer_id_task1 = TimerService_CreateTimer(10U, Task_10ms_Callback, (void *)0);
    if (g_timer_id_task1 >= 0)
    {
        TimerService_StartTimer((unsigned int)g_timer_id_task1);
        printf("task1 id = %d\r\n", g_timer_id_task1);
    }

    g_timer_id_task2 = TimerService_CreateTimer(100U, Task_100ms_Callback, (void *)0);
    if (g_timer_id_task2 >= 0)
    {
        TimerService_StartTimer((unsigned int)g_timer_id_task2);
        printf("task2 id = %d\r\n", g_timer_id_task2);
    }

    g_timer_id_task3 = TimerService_CreateTimer(1000U, Task_1000ms_Callback, (void *)0);
    if (g_timer_id_task3 >= 0)
    {
        TimerService_StartTimer((unsigned int)g_timer_id_task3);
        printf("task3 id = %d\r\n", g_timer_id_task3);
    }
}

static void FMC_ISP_Program(uint32_t u32Cmd, uint32_t u32Addr, uint32_t u32Data)
{
    uint32_t u32TimeOutCnt;

    FMC_ENABLE_AP_UPDATE();
    FMC->ISPCMD = u32Cmd;
    FMC->ISPADDR = u32Addr;
    FMC->ISPDAT = u32Data;
    FMC->ISPTRG = FMC_ISPTRG_ISPGO_Msk;
    __ISB();

    u32TimeOutCnt = FMC_TIMEOUT_READ;
    while (FMC->ISPTRG & FMC_ISPTRG_ISPGO_Msk)
    {
        if (--u32TimeOutCnt == 0U)
        {
            while (1)
            {
            }
        }
    }
}

static void APP_InvalidateChecksumAndReset(void)
{
    uint32_t u32TimeOutCnt;

    printf("erase APP checksum at 0x%08lX\r\n", (unsigned long)APP_CHECKSUM_ADDR);

    SYS_UnlockReg();
    FMC_Open();
    FMC_ENABLE_AP_UPDATE();

    if ((APP_CHECKSUM_ADDR & (FMC_FLASH_PAGE_SIZE - 1U)) == 0U)
    {
        FMC_ISP_Program(FMC_ISPCMD_PAGE_ERASE, APP_CHECKSUM_ADDR, 0U);
    }

    FMC_ISP_Program(FMC_ISPCMD_PROGRAM, APP_CHECKSUM_ADDR, 0x00000000U);
    printf("Checksum now: 0x%08X\r\n", FMC_Read(APP_CHECKSUM_ADDR));

    u32TimeOutCnt = SystemCoreClock;
    while (!UART_IS_TX_EMPTY(APP_DEBUG_UART))
    {
        if (--u32TimeOutCnt == 0U)
        {
            break;
        }
    }

    __set_PRIMASK(1);
    FMC_SetVectorPageAddr(FMC_APROM_BASE);
    FMC_SET_APROM_BOOT();
    SYS_ResetChip();
}

uint8_t check_reset_source(void)
{
    uint32_t src;

    src = SYS_GetResetSrc();

    SYS->RSTSTS |= 0x1FF;
    printf("Reset Source <0x%08X>\r\n", src);

    if (src & BIT0)
    {
        printf("0)POR Reset Flag\r\n");
    }
    if (src & BIT1)
    {
        printf("1)NRESET Pin Reset Flag\r\n");
    }
    if (src & BIT2)
    {
        printf("2)WDT Reset Flag\r\n");
    }
    if (src & BIT3)
    {
        printf("3)LVR Reset Flag\r\n");
    }
    if (src & BIT4)
    {
        printf("4)BOD Reset Flag\r\n");
    }
    if (src & BIT5)
    {
        printf("5)System Reset Flag \r\n");
    }
    if (src & BIT6)
    {
        printf("6)Reserved.\r\n");
    }
    if (src & BIT7)
    {
        printf("7)CPU Reset Flag\r\n");
    }
    if (src & BIT8)
    {
        printf("8)CPU Lockup Reset Flag\r\n");
    }

    if (src & SYS_RSTSTS_PORF_Msk)
    {
        SYS_ClearResetSrc(SYS_RSTSTS_PORF_Msk);
        printf("power on from POR\r\n");
        return FALSE;
    }
    else if (src & SYS_RSTSTS_PINRF_Msk)
    {
        SYS_ClearResetSrc(SYS_RSTSTS_PINRF_Msk);
        printf("power on from nRESET pin\r\n");
        return FALSE;
    }
    else if (src & SYS_RSTSTS_WDTRF_Msk)
    {
        SYS_ClearResetSrc(SYS_RSTSTS_WDTRF_Msk);
        printf("power on from WDT Reset\r\n");
        return FALSE;
    }
    else if (src & SYS_RSTSTS_LVRF_Msk)
    {
        SYS_ClearResetSrc(SYS_RSTSTS_LVRF_Msk);
        printf("power on from LVR Reset\r\n");
        return FALSE;
    }
    else if (src & SYS_RSTSTS_BODRF_Msk)
    {
        SYS_ClearResetSrc(SYS_RSTSTS_BODRF_Msk);
        printf("power on from BOD Reset\r\n");
        return FALSE;
    }
    else if (src & SYS_RSTSTS_SYSRF_Msk)
    {
        SYS_ClearResetSrc(SYS_RSTSTS_SYSRF_Msk);
        printf("power on from System Reset\r\n");
        return FALSE;
    }
    else if (src & SYS_RSTSTS_CPURF_Msk)
    {
        SYS_ClearResetSrc(SYS_RSTSTS_CPURF_Msk);
        printf("power on from CPU reset\r\n");
        return FALSE;
    }
    else if (src & SYS_RSTSTS_CPULKRF_Msk)
    {
        SYS_ClearResetSrc(SYS_RSTSTS_CPULKRF_Msk);
        printf("power on from CPU Lockup Reset\r\n");
        return FALSE;
    }

    printf("power on from unhandle reset source\r\n");
    return FALSE;
}

void TIMER2_Init(void)
{
    TIMER_Stop(TIMER2);
    TIMER_SET_PRESCALE_VALUE(TIMER2, 47U);
    TIMER_SET_OPMODE(TIMER2, TIMER_CONTINUOUS_MODE);
    TIMER_SET_CMP_VALUE(TIMER2, 0xFFFFFFUL);
    TIMER2->CNT = 0U;
    TIMER_Start(TIMER2);
}

void TMR1_IRQHandler(void)
{
    if (TIMER_GetIntFlag(TIMER1) == 1U)
    {
        TIMER_ClearIntFlag(TIMER1);
        tick_counter();
        TimerService_Tick1ms();
    }
}

void TIMER1_Init(void)
{
    TIMER_Open(TIMER1, TIMER_PERIODIC_MODE, 1000);
    TIMER_EnableInt(TIMER1);
    NVIC_EnableIRQ(TMR1_IRQn);
    TIMER_Start(TIMER1);
}

static void APP_StandbyWaitCanWake(void)
{
    uint32_t u32TimeOutCnt;
    uint32_t u32SysTickCtrl;
    uint32_t u32UartIntEn;
    uint8_t u8WdtWasEnabled;

    u32SysTickCtrl = SysTick->CTRL;
    u32UartIntEn = APP_DEBUG_UART->INTEN;
    u8WdtWasEnabled = ((WDT->CTL & WDT_CTL_WDTEN_Msk) != 0U) ? 1U : 0U;

    printf("Enter standby (CPU sleep). Wake-up source: CAN RX IRQ\r\n");
    u32TimeOutCnt = SystemCoreClock;
    while (!UART_IS_TX_EMPTY(APP_DEBUG_UART))
    {
        if (--u32TimeOutCnt == 0U)
        {
            break;
        }
    }

    if (u8WdtWasEnabled != 0U)
    {
        SYS_UnlockReg();
        WDT_RESET_COUNTER();
        WDT_Close();
        SYS_LockReg();
    }

    TIMER_DisableInt(TIMER1);
    TIMER_Stop(TIMER1);
    NVIC_DisableIRQ(TMR1_IRQn);
    TIMER_Stop(TIMER2);
    UART_DisableInt(APP_DEBUG_UART, APP_DEBUG_UART_INT_MASK);
    NVIC_DisableIRQ(APP_DEBUG_UART_IRQn);

    SysTick->CTRL &= ~(SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk);

    g_u32CanIrqStatus = 0U;
    while (g_u32CanIrqStatus == 0U)
    {
        CLK_Idle();
    }

    SysTick->CTRL = u32SysTickCtrl;
    APP_DEBUG_UART->INTEN = u32UartIntEn;
    NVIC_EnableIRQ(APP_DEBUG_UART_IRQn);
    NVIC_EnableIRQ(TMR1_IRQn);
    TIMER_EnableInt(TIMER1);
    TIMER_Start(TIMER1);
    TIMER_Start(TIMER2);
    if (u8WdtWasEnabled != 0U)
    {
        WDT_Init();
    }

    printf("Wake-up by CAN activity\r\n");
}

void loop(void)
{
    uint8_t u8Idx = 0;
    E_DRV_CAN_TX_RESULT eCanTxResult;

    WDT_RESET_COUNTER();
    TimerService_Dispatch();
    CAN_Process(get_tick());
    GPIO_IN_OUT_proccess();
    APP_ProcessInputMeasureEvents();
    
    #if (ENABLE_LIN_SLAVE == 1)
    APP_ProcessLinRx();
    APP_ProcessLinTx();
    #endif

    //  CAN RX
    if (g_u8CanRxDataBCUpdated == 1U)
    {
        g_u8CanRxDataBCUpdated = 0U;
        printf("g_au8CanRxDataBC[0..7] : ");
        for (u8Idx = 0U; u8Idx < 8U; u8Idx++)
        {
            printf("0x%02X ", g_au8CanRxDataBC[u8Idx]);
        }
        printf("\r\n");
    }

    if (FLAG_PROJ_ERASE_CHECKSUM)
    {
        FLAG_PROJ_ERASE_CHECKSUM = 0;
        APP_InvalidateChecksumAndReset();
    }

    if (FLAG_PROJ_STANDBY_REQUEST)
    {
        FLAG_PROJ_STANDBY_REQUEST = 0;
        APP_StandbyWaitCanWake();
    }

    if (FLAG_PROJ_SEND_CAN_1)
    {
        g_sTxMsgFrame.au8Data[0] = 0x40U;
        g_sTxMsgFrame.au8Data[1] = 0x20U;
        g_sTxMsgFrame.au8Data[2] = 0x85U;
        g_sTxMsgFrame.au8Data[3] = 0x83U;
        g_sTxMsgFrame.au8Data[4] = 0x85U;
        g_sTxMsgFrame.au8Data[5] = 0x59U;
        g_sTxMsgFrame.au8Data[6] = 0x5AU;
        g_sTxMsgFrame.au8Data[7] = 0xFFU;

        eCanTxResult = CAN_SendMessage(TRUE, &g_sTxMsgFrame, eCANFD_SID, 0x99, 8);
        if ((eCanTxResult == eDRV_CAN_TX_QUEUED) || (eCanTxResult == eDRV_CAN_TX_INVALID))
        {
            FLAG_PROJ_SEND_CAN_1 = 0;
        }
    }

    if (FLAG_PROJ_SEND_CAN_2)
    {
        uint8_t u8Idx;

        for (u8Idx = 0U; u8Idx < 32U; u8Idx++)
        {
            g_sTxMsgFrame.au8Data[u8Idx] = 0x20U + u8Idx;
        }

        eCanTxResult = CAN_SendMessage(TRUE, &g_sTxMsgFrame, eCANFD_XID, 0x4444, 32);
        if ((eCanTxResult == eDRV_CAN_TX_QUEUED) || (eCanTxResult == eDRV_CAN_TX_INVALID))
        {
            FLAG_PROJ_SEND_CAN_2 = 0;
        }
    }

}

void UARTx_Process(void)
{
    uint8_t res;

    res = (uint8_t)UART_READ(APP_DEBUG_UART);

    if (res > 0x7FU)
    {
        printf("invalid command\r\n");
    }
    else
    {
        printf("press : %c\r\n", res);
        switch (res)
        {
            case '7':
                FLAG_PROJ_STANDBY_REQUEST = 1;
                break;

            case '8':
                FLAG_PROJ_SEND_CAN_1 = 1;
                break;

            case '9':
                FLAG_PROJ_SEND_CAN_2 = 1;
                break;

            case 'E':
            case 'e':
                FLAG_PROJ_ERASE_CHECKSUM = 1;
                break;

            case 'X':
            case 'x':
            case 'Z':
            case 'z':
                SYS_UnlockReg();
                SYS_ResetChip();
                break;

            default:
                break;
        }
    }
}

void APP_DEBUG_UART_IRQHandler(void)
{
    if (UART_GET_INT_FLAG(APP_DEBUG_UART, UART_INTSTS_RDAINT_Msk | UART_INTSTS_RXTOINT_Msk))
    {
        while (UART_GET_RX_EMPTY(APP_DEBUG_UART) == 0U)
        {
            UARTx_Process();
        }
    }

    if (APP_DEBUG_UART->FIFOSTS & (UART_FIFOSTS_BIF_Msk | UART_FIFOSTS_FEF_Msk | UART_FIFOSTS_PEF_Msk | UART_FIFOSTS_RXOVIF_Msk))
    {
        UART_ClearIntFlag(APP_DEBUG_UART, UART_INTSTS_RLSINT_Msk | UART_INTSTS_BUFERRINT_Msk);
    }
}

void Debug_UART_Init(void)
{
    SYS_ResetModule(APP_DEBUG_UART_RST);
    UART_Open(APP_DEBUG_UART, APP_DEBUG_UART_BAUD_RATE);
    UART_EnableInt(APP_DEBUG_UART, APP_DEBUG_UART_INT_MASK);
    NVIC_EnableIRQ(APP_DEBUG_UART_IRQn);

#if (_debug_log_UART_ == 1)
    printf("\r\nCLK_GetCPUFreq : %8d\r\n", CLK_GetCPUFreq());
    printf("CLK_GetHCLKFreq : %8d\r\n", CLK_GetHCLKFreq());
    printf("CLK_GetHXTFreq : %8d\r\n", CLK_GetHXTFreq());
    printf("CLK_GetLXTFreq : %8d\r\n", CLK_GetLXTFreq());
    printf("CLK_GetPCLK0Freq : %8d\r\n", CLK_GetPCLK0Freq());
    printf("CLK_GetPCLK1Freq : %8d\r\n", CLK_GetPCLK1Freq());
#endif
}

void SYS_Init(void)
{
    SYS_UnlockReg();

    PF->MODE &= ~(GPIO_MODE_MODE2_Msk | GPIO_MODE_MODE3_Msk);

    CLK_EnableXtalRC(CLK_PWRCTL_HIRCEN_Msk);
    CLK_WaitClockReady(CLK_STATUS_HIRCSTB_Msk);

    CLK_EnableXtalRC(CLK_PWRCTL_HXTEN_Msk);
    CLK_WaitClockReady(CLK_STATUS_HXTSTB_Msk);

    CLK_EnableXtalRC(CLK_PWRCTL_LIRCEN_Msk);
    CLK_WaitClockReady(CLK_STATUS_LIRCSTB_Msk);

    CLK_SetCoreClock(72000000);
    CLK_SetHCLK(CLK_CLKSEL0_HCLKSEL_HIRC, CLK_CLKDIV0_HCLK(1));

    CLK->AHBCLK |= CLK_AHBCLK_GPIOACKEN_Msk | CLK_AHBCLK_GPIOBCKEN_Msk |
                   CLK_AHBCLK_GPIOCCKEN_Msk | CLK_AHBCLK_GPIODCKEN_Msk |
                   CLK_AHBCLK_GPIOFCKEN_Msk;

    #if (ENABLE_LIN_SLAVE == 1)
    CLK_EnableModuleClock(UART0_MODULE);
    CLK_SetModuleClock(UART0_MODULE, CLK_CLKSEL2_UART0SEL_HIRC, CLK_CLKDIV0_UART0(1));
    #endif

    CLK_EnableModuleClock(UART1_MODULE);
    CLK_SetModuleClock(UART1_MODULE, CLK_CLKSEL2_UART1SEL_HIRC, CLK_CLKDIV0_UART1(1));

    CLK_EnableModuleClock(TMR1_MODULE);
    CLK_SetModuleClock(TMR1_MODULE, CLK_CLKSEL1_TMR1SEL_HIRC, 0);
    CLK_EnableModuleClock(TMR2_MODULE);
    CLK_SetModuleClock(TMR2_MODULE, CLK_CLKSEL1_TMR2SEL_HIRC, 0);

    CLK_EnableModuleClock(PWM0_MODULE);
    CLK_SetModuleClock(PWM0_MODULE, CLK_CLKSEL3_PWM0SEL_PCLK0, 0);
    CLK_EnableModuleClock(BPWM0_MODULE);
    CLK_SetModuleClock(BPWM0_MODULE, CLK_CLKSEL3_BPWM0SEL_PCLK0, 0);

    CLK_EnableModuleClock(ADC0_MODULE);
    CLK_SetModuleClock(ADC0_MODULE, CLK_CLKSEL3_ADC0SEL_HCLK, CLK_CLKDIV0_ADC0(32));

    CLK_SetModuleClock(CANFD0_MODULE, CLK_CLKSEL0_CANFD0SEL_PLL_DIV2, CLK_CLKDIV1_CANFD0(1));
    CLK_EnableModuleClock(CANFD0_MODULE);
    
    /* Enable WDT module clock */
    CLK_EnableModuleClock(WDT_MODULE);
    CLK_SetModuleClock(WDT_MODULE, CLK_CLKSEL1_WDTSEL_LIRC, 0);

    #if (ENABLE_LIN_SLAVE == 1)
    SET_UART0_RXD_PA6();
    SET_UART0_TXD_PA7();
    #endif
    SET_UART1_RXD_PB6();
    SET_UART1_TXD_PB7();
    SET_ADC0_CH0_PB0();
    SET_ADC0_CH2_PB2();
    SET_PWM0_CH0_PF5();
    SET_PWM0_CH2_PB3();
    SET_PWM0_CH4_PB1();
    SET_BPWM0_CH3_PA3();
    SET_INT0_PB5();
    SET_INT1_PB4();
    SET_CANFD0_RXD_PC4();
    SET_CANFD0_TXD_PC5();

    SystemCoreClockUpdate();

    SYS_LockReg();
}

int main(void)
{
    SYS_Init();
    Debug_UART_Init();

    DRV_GPIO_IO_Init();
    
    #if (ENABLE_LIN_SLAVE == 1)
    DRV_LIN_SLAVE_Init(LIN_SLAVE_UART_BAUD_RATE);
    #endif
    TIMER1_Init();
    TIMER2_Init();
    check_reset_source();
    WDT_Init();

    SysTick_enable(1000);
#if defined (ENABLE_TICK_EVENT)
    TickSetTickEvent(1000, TickCallback_processA);
    TickSetTickEvent(5000, TickCallback_processB);
#endif

    CAN_Init();
    DRV_ADC_Init();
    DRV_PWM_Init();
    #if (ENABLE_LIN_SLAVE == 1)
    if (APP_InitLinFrames() != DRV_LIN_SLAVE_OK)
    {
        printf("LIN slave registration/start failed\r\n");
    }
    else
    {
        printf("LIN slave: 19200 bps, Enhanced checksum default\r\n");
        printf("LIN dynamic D8/E2 payload example: %u\r\n", (unsigned int)LIN_SLAVE_DYNAMIC_TX_EXAMPLE);
        printf("LIN header: HW Break -> SW Sync(55) -> PID, EN(PB14)=%lu\r\n", (unsigned long)PB14);
        printf("LIN registry ready, capacity=%u\r\n", (unsigned int)DRV_LIN_SLAVE_MAX_FRAME_COUNT);
    }
    #endif

    printf("UART key map:\r\n");
    printf("7: standby until CAN RX wake-up\r\n");
    printf("8: send CAN message:0x99, 8 bytes\r\n");
    printf("9: send CAN message:0x4444, 32 bytes\r\n");
    printf("E/e: invalidate APP checksum + reset\r\n");

    TimerService_Init();
    TimerService_CreateTask();

    while (1)
    {
        loop();
    }
}

/*** (C) COPYRIGHT 2017 Nuvoton Technology Corp. ***/
