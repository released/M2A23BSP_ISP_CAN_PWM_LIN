#ifndef __DRV_LIN_SLAVE_H__
#define __DRV_LIN_SLAVE_H__

/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include "NuMicro.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
#define ENABLE_LIN_SLAVE                                (1U)
#define DRV_LIN_SLAVE_MAX_DATA_LEN                      (8U)

typedef enum
{
    eDRV_LIN_SLAVE_CHECKSUM_CLASSIC = 0,
    eDRV_LIN_SLAVE_CHECKSUM_ENHANCED
} E_DRV_LIN_SLAVE_CHECKSUM_MODE;

typedef struct
{
    uint8_t u8Id;
    uint8_t u8DataLen;
    uint8_t au8Data[DRV_LIN_SLAVE_MAX_DATA_LEN];
    uint8_t u8Checksum;
    uint8_t u8ChecksumValid;
} S_DRV_LIN_SLAVE_PACKET;

/*_____ F U N C T I O N S __________________________________________________*/
void DRV_LIN_SLAVE_Init(uint32_t u32BaudRate);
void DRV_LIN_SLAVE_SetEnable(uint8_t u8Enable);
int32_t DRV_LIN_SLAVE_StartReceive(uint8_t u8Id,
                                  uint8_t u8DataLen,
                                  E_DRV_LIN_SLAVE_CHECKSUM_MODE eChecksumMode);
int32_t DRV_LIN_SLAVE_StartResponse(uint8_t u8Id,
                                   const uint8_t *pu8Data,
                                   uint8_t u8DataLen,
                                   E_DRV_LIN_SLAVE_CHECKSUM_MODE eChecksumMode);
uint8_t DRV_LIN_SLAVE_GetAndClearRxPacket(S_DRV_LIN_SLAVE_PACKET *psPacket);

#endif /* __DRV_LIN_SLAVE_H__ */
