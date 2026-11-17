#ifndef __DRV_LIN_MASTER_H__
#define __DRV_LIN_MASTER_H__

/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include "NuMicro.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
#define DRV_LIN_MASTER_MAX_DATA_LEN                     (8U)

typedef enum
{
    eDRV_LIN_MASTER_CHECKSUM_CLASSIC = 0,
    eDRV_LIN_MASTER_CHECKSUM_ENHANCED
} E_DRV_LIN_MASTER_CHECKSUM_MODE;

/*_____ F U N C T I O N S __________________________________________________*/
void DRV_LIN_MASTER_Init(uint32_t u32BaudRate);
void DRV_LIN_MASTER_SetEnable(uint8_t u8Enable);
int32_t DRV_LIN_MASTER_SendPacket(uint8_t u8Id,
                                 const uint8_t *pu8Data,
                                 uint8_t u8DataLen,
                                 E_DRV_LIN_MASTER_CHECKSUM_MODE eChecksumMode);

#endif /* __DRV_LIN_MASTER_H__ */
