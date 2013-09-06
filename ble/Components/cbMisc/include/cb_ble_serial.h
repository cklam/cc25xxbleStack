#ifndef _CB_BLE_SERIAL_H_
#define _CB_BLE_SERIAL_H_

/*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : Serial
 * File        : cb_ble_serial.h
 *
 * Description : Implementation of serial data handling using the 
 *               connectBlue Serial Port Service. 
 *-------------------------------------------------------------------------*/

/*===========================================================================
 * DEFINES
 *=========================================================================*/
#ifndef cbBLS_CREDITS_TOTAL
  #define cbBLS_CREDITS_TOTAL               10
#endif

#define cbBLS_PORT_0                        (0)

#define cbBLS_SERVER_PROFILE_SPP_LE         14
#define cbBLS_SERVER_PROFILE_NONE           255

/*===========================================================================
 * TYPES
 *=========================================================================*/
typedef void (*cbBLS_DataAvailableCallback)(uint8 port);
typedef void (*cbBLS_WriteCompleteCallback)(uint8 port, uint16 bufSize);
typedef void (*cbBLS_ErrorCallback)(uint8 port, uint8 error);
#ifndef WITHOUT_ESCAPE_SEQUENCE
typedef void (*cbBLS_EscapeCallback)(uint8 port);
#endif
typedef uint8 (*cbBLS_RequestConnectionCallback)(uint8 port);


typedef struct
{
  cbBLS_DataAvailableCallback dataAvailableCallback;
  cbBLS_WriteCompleteCallback writeCompleteCallback;
  cbBLS_ErrorCallback errorCallback;
#ifndef WITHOUT_ESCAPE_SEQUENCE
  cbBLS_EscapeCallback escapeCallback;
#endif
  cbBLS_RequestConnectionCallback requestConnectionCallback;
} cbBLS_Callbacks;

/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/
//extern void cbBLS_init(uint8 taskId);
extern void cbBLS_init(void);
//extern uint16 cbBLS_processEvent(uint8 taskId, uint16 events);
extern Status_t cbBLS_registerCallbacks(cbBLS_Callbacks *pCallb);
extern Status_t cbBLS_open(uint8 port, void* pCfg);
extern Status_t cbBLS_close(uint8 port);

extern Status_t cbBLS_write(uint8 port, uint8 *pBuf, uint16 bufSize);
extern Status_t cbBLS_getReadBuf(uint8 port, uint8** ppBuf, uint16* pBufSize);
extern Status_t cbBLS_readBufConsumed(uint8 port, uint16 nBytes);
extern Status_t cbBLS_readByte(uint8 port, uint8* pByte);

extern Status_t cbBLS_setServerProfile(uint8 val);
extern Status_t cbBLS_getServerProfile(uint8* pVal);

extern Status_t cbBLS_setWatchdogConfig(uint16 writeTimeout, uint16 connectTimeout, uint16 inactivityTimeout, bool disconnectReset);
extern Status_t cbBLS_getWatchdogConfig(uint16 *pWriteTimeout, uint16 *pConnectTimeout, uint16 *pInactivityTimeout, bool *pDisconnectReset);

#endif 






