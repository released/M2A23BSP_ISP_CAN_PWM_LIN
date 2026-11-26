#ifndef __DRV_LIN_SLAVE_H__
#define __DRV_LIN_SLAVE_H__

/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include "NuMicro.h"

/*_____ D E F I N I T I O N S ______________________________________________*/
#define ENABLE_LIN_SLAVE                                (1U)
#define DRV_LIN_SLAVE_MAX_DATA_LEN                      (8U)
#define DRV_LIN_SLAVE_TX_EVENT_COUNT                    (8U)
/* Registry capacity, not the number of application registrations. */
#ifndef DRV_LIN_SLAVE_MAX_FRAME_COUNT
#define DRV_LIN_SLAVE_MAX_FRAME_COUNT                   (16U)
#endif
#if ((DRV_LIN_SLAVE_MAX_FRAME_COUNT < 1U) || (DRV_LIN_SLAVE_MAX_FRAME_COUNT > 64U))
#error "DRV_LIN_SLAVE_MAX_FRAME_COUNT must be between 1U and 64U"
#endif

#define DRV_LIN_SLAVE_OK                                (0)
#define DRV_LIN_SLAVE_INVALID_ARGUMENT                  (-1)
#define DRV_LIN_SLAVE_BUSY                              (-2)
#define DRV_LIN_SLAVE_NOT_FOUND                         (-3)
#define DRV_LIN_SLAVE_REGISTRY_FULL                     (-4)
#define DRV_LIN_SLAVE_ALREADY_REGISTERED                (-5)
#define DRV_LIN_SLAVE_ERROR_PID_PARITY                  (1UL << 0)
#define DRV_LIN_SLAVE_ERROR_HEADER                      (1UL << 1)
#define DRV_LIN_SLAVE_ERROR_TX                          (1UL << 2)
#define DRV_LIN_SLAVE_ERROR_RX_OVERFLOW                 (1UL << 3)
#define DRV_LIN_SLAVE_ERROR_TX_EVENT_OVERFLOW           (1UL << 4)

typedef enum
{
    eDRV_LIN_SLAVE_CHECKSUM_CLASSIC = 0,
    eDRV_LIN_SLAVE_CHECKSUM_ENHANCED
} E_DRV_LIN_SLAVE_CHECKSUM_MODE;

#define DRV_LIN_SLAVE_DEFAULT_CHECKSUM_MODE             eDRV_LIN_SLAVE_CHECKSUM_ENHANCED

typedef enum
{
    eDRV_LIN_SLAVE_DIRECTION_RECEIVE = 0,
    eDRV_LIN_SLAVE_DIRECTION_RESPONSE
} E_DRV_LIN_SLAVE_DIRECTION;

typedef struct
{
    uint8_t u8Pid;
    E_DRV_LIN_SLAVE_DIRECTION eDirection;
    uint8_t u8DataLen;
    E_DRV_LIN_SLAVE_CHECKSUM_MODE eChecksumMode;
    const uint8_t *pu8InitialData;
} S_DRV_LIN_SLAVE_FRAME_CONFIG;

typedef struct
{
    uint8_t u8Id;
    uint8_t u8Pid;
    uint8_t u8DataLen;
    uint8_t au8Data[DRV_LIN_SLAVE_MAX_DATA_LEN];
    uint8_t u8Checksum;
    uint8_t u8ChecksumValid;
} S_DRV_LIN_SLAVE_PACKET;

/*_____ F U N C T I O N S __________________________________________________*/
void DRV_LIN_SLAVE_Init(uint32_t u32BaudRate);
void DRV_LIN_SLAVE_SetEnable(uint8_t u8Enable);
/* Foreground APIs. PID includes parity; data length excludes checksum.
 * Register/unregister returns BUSY while a header or response is in progress.
 * A newly registered publisher remains silent until SetResponseData succeeds.
 */
int32_t DRV_LIN_SLAVE_RegisterFrame(uint8_t u8Pid,
                                   E_DRV_LIN_SLAVE_DIRECTION eDirection,
                                   uint8_t u8DataLen,
                                   E_DRV_LIN_SLAVE_CHECKSUM_MODE eChecksumMode);
int32_t DRV_LIN_SLAVE_UnregisterFrame(uint8_t u8Pid);
int32_t DRV_LIN_SLAVE_Start(void);
/* Legacy bulk/single-frame configuration replaces the complete registry. */
int32_t DRV_LIN_SLAVE_StartFrames(const S_DRV_LIN_SLAVE_FRAME_CONFIG *psFrameConfig,
                                  uint8_t u8FrameCount);
/* Copies data; the caller can reuse its buffer after return. The next header
 * snapshots the latest payload. An active response is not changed mid-frame.
 */
int32_t DRV_LIN_SLAVE_SetResponseData(uint8_t u8Pid,
                                     const uint8_t *pu8Data,
                                     uint8_t u8DataLen);
int32_t DRV_LIN_SLAVE_StartReceive(uint8_t u8Id,
                                  uint8_t u8DataLen,
                                  E_DRV_LIN_SLAVE_CHECKSUM_MODE eChecksumMode);
int32_t DRV_LIN_SLAVE_StartResponse(uint8_t u8Id,
                                   const uint8_t *pu8Data,
                                   uint8_t u8DataLen,
                                   E_DRV_LIN_SLAVE_CHECKSUM_MODE eChecksumMode);
/* Each RX PID retains one packet, including its checksum status. A second
 * packet for that PID is dropped with RX_OVERFLOW until the first is consumed.
 * Either getter consumes the same mailbox; do not use both for one consumer.
 */
uint8_t DRV_LIN_SLAVE_GetAndClearRxPacketByPid(uint8_t u8Pid,
                                             S_DRV_LIN_SLAVE_PACKET *psPacket);
/* Compatibility getter: scans ready mailboxes in registration order, not time. */
uint8_t DRV_LIN_SLAVE_GetAndClearRxPacket(S_DRV_LIN_SLAVE_PACKET *psPacket);
/* FIFO of completed UART responses, not payload updates or remote ACKs.
 * A full event FIFO drops only the new log event; LIN transmission continues.
 */
uint8_t DRV_LIN_SLAVE_GetAndClearTxPacket(S_DRV_LIN_SLAVE_PACKET *psPacket);
uint32_t DRV_LIN_SLAVE_GetAndClearErrorFlags(uint8_t *pu8LastPid);

#endif /* __DRV_LIN_SLAVE_H__ */
