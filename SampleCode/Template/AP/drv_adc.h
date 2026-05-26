#ifndef __DRV_ADC_H__
#define __DRV_ADC_H__

/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include "NuMicro.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
typedef enum
{
    eDRV_ADC_B0 = 0,
    eDRV_ADC_B1,
    eDRV_ADC_CHANNEL_MAX
} E_DRV_ADC_CHANNEL;

/*_____ F U N C T I O N S __________________________________________________*/
void DRV_ADC_Init(void);
void DRV_ADC_SampleAll(void);
uint16_t DRV_ADC_GetSample(E_DRV_ADC_CHANNEL eChannel);
void DRV_ADC_UpdateAvdd(void);
uint32_t DRV_ADC_GetAvddMv(void);
uint32_t DRV_ADC_GetSampleMilliVolt(E_DRV_ADC_CHANNEL eChannel);

#endif /* __DRV_ADC_H__ */
