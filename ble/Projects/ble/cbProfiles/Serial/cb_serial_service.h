#ifndef SERIAL_PORT_SERVICE_H
#define SERIAL_PORT_SERVICE_H
/*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : Serial Service
 * File        : cb_led_service.h
 *
 * Description : Declaration of Serial Port Service functionality.
 *-------------------------------------------------------------------------*/

/*===========================================================================
 * DEFINES
 *=========================================================================*/

// All UUIDs in Little Endian format so that memcpm can be used

// Service UUID
#define cbSPS_SERIAL_SERVICE_UUID                    0x01,0xd7,0xe9,0x01,0x4f,0xf3,0x44,0xe7,0x83,0x8f,0xe2,0x26,0xb9,0xe1,0x56,0x24

//Characteristics UUIDs
#define cbSPS_MODE_UUID                              0x02,0xd7,0xe9,0x01,0x4f,0xf3,0x44,0xe7,0x83,0x8f,0xe2,0x26,0xb9,0xe1,0x56,0x24
#define cbSPS_FIFO_UUID                              0x03,0xd7,0xe9,0x01,0x4f,0xf3,0x44,0xe7,0x83,0x8f,0xe2,0x26,0xb9,0xe1,0x56,0x24
#define cbSPS_CREDITS_UUID                           0x04,0xd7,0xe9,0x01,0x4f,0xf3,0x44,0xe7,0x83,0x8f,0xe2,0x26,0xb9,0xe1,0x56,0x24

#define cbSPS_FIFO_SIZE                              (ATT_MTU_SIZE-3) //20


/*===========================================================================
 * TYPES
 *=========================================================================*/
typedef void (*cbSPS_ConnectEvt)(uint16 connHandle);
typedef void (*cbSPS_DisconnectEvt)(uint16 connHandle);
typedef void (*cbSPS_DataEvt)(uint16 connHandle, uint8 *pBuf, uint8 size);
typedef void (*cbSPS_DataCnf)(uint16 connHandle);

typedef struct 
{
  cbSPS_ConnectEvt    connectEventCallback;
  cbSPS_DisconnectEvt disconnectEventCallback;
  cbSPS_DataEvt       dataEventCallback;
  cbSPS_DataCnf       dataCnfCallback;
} cbSPS_Callbacks;


/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/
extern void cbSPS_init(uint8 taskId);
extern uint16 cbSPS_processEvent(uint8 taskId, uint16 events);
extern void cbSPS_addService(void);
extern void cbSPS_setSecurity(bool encryption, bool authentication);
extern void cbSPS_register(cbSPS_Callbacks *pCallbacks);
extern uint8 cbSPS_reqData(uint16 connHandle, uint8 *pBuf, uint8 size);
extern uint8 cbSPS_setRemainingBufSize(uint16 connHandle, uint16 size);
extern void cbSPS_enable(void);
extern void cbSPS_disable(void);

#endif
