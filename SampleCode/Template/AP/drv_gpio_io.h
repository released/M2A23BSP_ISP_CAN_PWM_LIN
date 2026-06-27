#ifndef __DRV_GPIO_IO_H__
#define __DRV_GPIO_IO_H__

/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include "NuMicro.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
typedef enum
{
    eDRV_GPIO_LED1 = 0,
    eDRV_GPIO_LED2,
    eDRV_GPIO_LED3,
    eDRV_GPIO_LED4,
    eDRV_GPIO_LED5,
    eDRV_GPIO_LED6,
    eDRV_GPIO_LED_MAX
} E_DRV_GPIO_LED;

typedef enum
{
    eDRV_GPIO_INPUT_MEASURE1 = 0,
    eDRV_GPIO_INPUT_MEASURE2,
    eDRV_GPIO_INPUT_MEASURE_MAX
} E_DRV_GPIO_INPUT_MEASURE;

#define LED_SET1                                        (PA0)
#define LED_SET2                                        (PA1)
#define LED_SET3                                        (PA2)
#define LED_SET4                                        (PA3)
#define LED_SET5                                        (PA4)
#define LED_SET6                                        (PA5)
#define DRV_GPIO_LED_SET4_GPIO_ENABLE                   (0U)
#define SW_1                                            (PA15)
#define SW_2                                            (PA14)
#define SW_3                                            (PA13)
#define SW_4                                            (PA12)
#define SW_5                                            (PC0)
#define SW_6                                            (PC1)

/*_____ F U N C T I O N S __________________________________________________*/
void DRV_GPIO_IO_Init(void);
void GPIO_IN_OUT_proccess(void);
void DRV_GPIO_IO_SetLed(E_DRV_GPIO_LED eLed, uint8_t u8On);
void DRV_GPIO_IO_ToggleLed(E_DRV_GPIO_LED eLed);
uint8_t DRV_GPIO_IO_GetAndClearInputMeasure(E_DRV_GPIO_INPUT_MEASURE eInput, uint16_t *pu16FreqHz, uint8_t *pu8Duty);

#endif /* __DRV_GPIO_IO_H__ */
