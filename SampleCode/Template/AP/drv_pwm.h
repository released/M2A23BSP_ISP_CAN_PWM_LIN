#ifndef __DRV_PWM_H__
#define __DRV_PWM_H__

/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include "NuMicro.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
#define DRV_PWM_GROUP_NUM                               (3U)
#define DRV_PWM_DUTY_MAX                                (100U)
#define DRV_PWM_DUTY_RESOLUTION                         (100U)

#define DRV_PWM_GROUP1_CHANNEL                          (0U)
#define DRV_PWM_GROUP2_CHANNEL                          (2U)
#define DRV_PWM_GROUP3_CHANNEL                          (4U)

#define DRV_PWM_GROUP1_FREQ_HZ                          (100UL)
#define DRV_PWM_GROUP2_FREQ_HZ                          (200UL)
#define DRV_PWM_GROUP3_FREQ_HZ                          (400UL)

#define DRV_BPWM_CHANNEL                                (3U)
#define DRV_BPWM_FREQ_HZ                                (250UL)

typedef enum
{
    eDRV_PWM_GROUP_1 = 0,
    eDRV_PWM_GROUP_2,
    eDRV_PWM_GROUP_3
} E_DRV_PWM_GROUP;

/*_____ F U N C T I O N S __________________________________________________*/
/* PA3 is switched to BPWM0_CH3 when DRV_GPIO_LED_SET4_GPIO_ENABLE is 0U. */
void DRV_PWM_Init(void);
void DRV_PWM_SetOutputDutyCycle(E_DRV_PWM_GROUP eGroup, uint8_t u8Duty);
/* 0% duty forces PA3 output to logic low. */
void DRV_BPWM_SetOutputDutyCycle(uint8_t u8Duty);

#endif /* __DRV_PWM_H__ */
