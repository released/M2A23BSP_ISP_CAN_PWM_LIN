#ifndef __DRV_PWM_H__
#define __DRV_PWM_H__

/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include "NuMicro.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
#define DRV_PWM_GROUP_NUM                               (3U)

typedef enum
{
    eDRV_PWM_GROUP_1 = 0,
    eDRV_PWM_GROUP_2,
    eDRV_PWM_GROUP_3
} E_DRV_PWM_GROUP;

/*_____ F U N C T I O N S __________________________________________________*/
void DRV_PWM_Init(void);
void DRV_PWM_SetOutputDutyCycle(E_DRV_PWM_GROUP eGroup, uint8_t u8Duty);

#endif /* __DRV_PWM_H__ */
