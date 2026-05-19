#ifndef __DRV_LIN_BUS_H__
#define __DRV_LIN_BUS_H__

/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include "NuMicro.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
#define DRV_LIN_BUS_MAX_DATA_LEN                        (8U)

typedef struct
{
    uint8_t u8Id;
    uint8_t u8DataLen;
    uint8_t au8Data[DRV_LIN_BUS_MAX_DATA_LEN];
    uint8_t u8Checksum;
} S_DRV_LIN_BUS_PACKET;

/*_____ F U N C T I O N S __________________________________________________*/
void DRV_LIN_BUS_Init(uint32_t u32BaudRate);
void DRV_LIN_BUS_SetEnable(uint8_t u8Enable);
void DRV_LIN_BUS_StartRx(uint8_t u8Id, uint8_t u8DataLen);
int32_t DRV_LIN_BUS_SendPacket(uint8_t u8Id, const uint8_t *pu8Data, uint8_t u8DataLen);
uint8_t DRV_LIN_BUS_GetAndClearRxPacket(S_DRV_LIN_BUS_PACKET *psPacket);

#endif /* __DRV_LIN_BUS_H__ */
