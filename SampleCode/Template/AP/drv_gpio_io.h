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

#define DRV_GPIO_BUTTON1_EVENT                          (1UL << 0)
#define DRV_GPIO_BUTTON2_EVENT                          (1UL << 1)
#define DRV_GPIO_BUTTON3_EVENT                          (1UL << 2)
#define DRV_GPIO_BUTTON4_EVENT                          (1UL << 3)
#define DRV_GPIO_BUTTON5_EVENT                          (1UL << 4)
#define DRV_GPIO_BUTTON6_EVENT                          (1UL << 5)
#define DRV_GPIO_INPUT1_EVENT                           (1UL << 0)
#define DRV_GPIO_INPUT2_EVENT                           (1UL << 1)

/*_____ F U N C T I O N S __________________________________________________*/
void DRV_GPIO_IO_Init(void);
void DRV_GPIO_IO_SetLed(E_DRV_GPIO_LED eLed, uint8_t u8On);
void DRV_GPIO_IO_ToggleLed(E_DRV_GPIO_LED eLed);
uint32_t DRV_GPIO_IO_GetButtonMask(void);
uint32_t DRV_GPIO_IO_GetAndClearButtonEventFlags(void);
uint32_t DRV_GPIO_IO_GetInputMask(void);
uint32_t DRV_GPIO_IO_GetAndClearInputEventFlags(void);

#endif /* __DRV_GPIO_IO_H__ */
