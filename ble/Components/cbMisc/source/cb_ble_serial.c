/*---------------------------------------------------------------------------
* Copyright (c) 2000, 2001 connectBlue AB, Sweden.
* Any reproduction without written permission is prohibited by law.
*
* Component   : Serial
* File        : cb_ble_serial.c
*
* Description : Implementation of serial data handling using the 
*               serial service provided by "serial service". Implements
*               buffer, credits and connection management.
*               Currently only the peripheral side is implemented.
*-------------------------------------------------------------------------*/
#include "bcomdef.h"
#include "OSAL.h"
#include "OSAL_PwrMgr.h"
#include "osal_cbtimer.h"

#include "hal_mcu.h"
#include "hal_adc.h"
#include "hal_led.h"
#include "hal_key.h"

#include "linkdb.h"
#include "gatt.h"
#include "gattservapp.h"

#include "cb_assert.h"
#include "cb_log.h"
#include "cb_ble_serial.h"
#include "cb_serial_service.h"
#include "cb_led.h"
#include "cb_buffer.h"
#include "cb_snv_ids.h"
#ifndef WITHOUT_ESCAPE_SEQUENCE
#include "cb_esc.h"
#endif


/*===========================================================================
* DEFINES
*=========================================================================*/

#define cbBLS_BUFFER_SIZE           (cbBLS_CREDITS_TOTAL*cbSPS_FIFO_SIZE)

#define UNITIALIZED_BUF_ID          (0xFF)


// TODO: Move all default values to a separate file
#define DEFAULT_SERVER_PROFILE      cbBLS_SERVER_PROFILE_SPP_LE

#ifndef WITHOUT_BLS_WATCHDOGS    
#define cbBLS_DEFAULT_WD_WRITE_TIMEOUT      0
#define cbBLS_DEFAULT_WD_CONNECTION_TIMEOUT 0
#define cbBLS_DEFAULT_WD_INACTIVITY_TIMEOUT 0
#define cbBLS_DEFAULT_WD_DISCONNECT_RESET   FALSE
#endif

#define cbBLS_MAX_CALLBACKS         (2)

#define cbBLS_CONNECTION_TIMEOUT    (5000) //ms

/*===========================================================================
 * TYPES
 *=========================================================================*/
typedef enum
{
  cbBLS_S_INVALID = 0,

  cbBLS_S_INACTIVE,
  cbBLS_S_CLOSED,
  cbBLS_S_IDLE,
  cbBLS_S_WAIT_CONNECT,
  cbBLS_S_CONNECTED,
  cbBLS_S_CLOSING,

  cbBLS_S_RX_BUF_EMPTY,
  cbBLS_S_RX_DATA_AVAILABLE,
  cbBLS_S_RX_BUF_FULL,

  cbBLS_S_TX_IDLE,
  cbBLS_S_TX_IN_PROGRESS,

} cbBLS_State;

#ifndef WITHOUT_ESCAPE_SEQUENCE
typedef enum
{
  cbBLS_S_ESC_INVALID = 0,

  cbBLS_S_ESC_IDLE,
  cbBLS_S_ESC_PRE_ESCAPE_SEQ_IGNORED,
  cbBLS_S_ESC_PRE_ESCAPE_SEQ,
  cbBLS_S_ESC_WITHIN_ESCAPE_SEQ,
  cbBLS_S_ESC_POST_ESCAPE_SEQ

} cbBLS_EscState;
#endif 


#ifndef WITHOUT_BLS_WATCHDOGS    
typedef struct  
{
    uint16 writeTimeout;
    uint16 connectionTimeout;
    uint16 inactivityTimeout;
    bool disconnectReset;
} cbBLS_Watchdog;
#endif

typedef struct 
{
  uint8                 taskId;
  cbBLS_State           state;
  cbBLS_State           rxState;
  cbBLS_State           txState;
  
  
  uint8                 bufId;
  uint16                connHandle;

  uint8                 *pWriteBuf;
  uint16                writeBufTotalSize;
  uint16                writeBufCurrentSize;
  uint16                writeBufTransmittedSize;

#ifndef WITHOUT_ESCAPE_SEQUENCE
  // Escape sequence
  cbBLS_EscState        escState;
  bool                  escEnabled;
  uint8                 nEscBytes;
  uint8                 escChar;
  uint8                 escTimerId;
#endif
  
  uint8                 serverProfile;

  uint8                 connTimerId;

#ifndef WITHOUT_BLS_WATCHDOGS    
  // Watchdog functionality
  cbBLS_Watchdog        wd;
  uint8                 wdWriteTimerId;
  uint8                 wdConnectTimerId;
  uint8                 wdInactivityTimerId;
#endif

} cbBLS_class;

/*===========================================================================
* DECLARATIONS
*=========================================================================*/

static void spsConnectCallback(uint16 connHandle);
static void spsDisconnectCallback(uint16 connHandle);
static void spsDataEventCallback(uint16 connHandle, uint8 *pBuf, uint8 size);
static void spsDataConfCallback(uint16 connHandle);

static void disconnect(void);
static void resetLink(void);
static void updateRemainingBufSize(void);
static cbBLS_State entryIdle(void);

static void copyDataToBuf(int16 bufId, uint8* pData, int16 nBytes);

#ifndef WITHOUT_ESCAPE_SEQUENCE
static bool checkEsc(uint8* pData, int16 nBytes);
static void abortEsc(uint8 nBytes);
#endif

static void blsCallbackNotifyDataAvailable(uint8 port);
static void blsCallbackNotifyWriteComplete(uint8 port, uint16 bufSize);
//static void blsCallbackNotifyError(uint8 port, uint8 error);
static uint8 blsCallbackNotifyRequestConnection(uint8 port);

#ifndef WITHOUT_ESCAPE_SEQUENCE
static void blsCallbackNotifyEscape(uint8 port);

static void startPreEscTimer(void);
static void startWithinEscTimer(void);
static void startPostEscTimer(bool ignoreTiming);

static void preEscTimeout(uint8* pData);
static void withinEscTimeout(uint8* pData);
static void postEscTimeout(uint8* pData);
#endif

static void connTimeout(uint8* idp);

#ifndef WITHOUT_BLS_WATCHDOGS    
static void startWriteTimeoutWd(void);
static void stopWriteTimeoutWd(void);
static void startConnectionTimeoutWd(void);
static void stopConnectionTimeoutWd(void);
static void kickInactivityTimeoutWd(void);
static void stopAllWdTimers(void);
static void wdTimeout(uint8* pData);
#endif


/*===========================================================================
* DEFINITIONS
*=========================================================================*/
cbSPS_Callbacks serialServiceCallbacks = 
{
  spsConnectCallback,
  spsDisconnectCallback,
  spsDataEventCallback,
  spsDataConfCallback,
};

cbBLS_class bls;

static cbBLS_Callbacks *blsCallbacks[cbBLS_MAX_CALLBACKS] = {NULL, NULL};

// Filename used by cb_ASSERT macro
static const char *file = "bls";

/*===========================================================================
* FUNCTIONS
*=========================================================================*/
/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void cbBLS_init(void)
{
#ifndef WITHOUT_BLS_WATCHDOGS
    uint8 status;
#endif

    bls.state = cbBLS_S_CLOSED;
    bls.serverProfile = cbBLS_SERVER_PROFILE_SPP_LE; // The server profile is currently always SPP_LE

    bls.rxState = cbBLS_S_INVALID;
    bls.txState = cbBLS_S_INVALID;
#ifndef WITHOUT_ESCAPE_SEQUENCE
    bls.escState = cbBLS_S_ESC_INVALID;
#endif

    bls.bufId = UNITIALIZED_BUF_ID;
    bls.connHandle = INVALID_CONNHANDLE;

    bls.pWriteBuf = NULL;
    bls.writeBufTotalSize = 0;

#ifndef WITHOUT_ESCAPE_SEQUENCE
    bls.escEnabled = cbESC_getAtOverAirEnabled();
    bls.nEscBytes = 0;
    bls.escChar = cbESC_getEscapeChar();   
#endif
    
#ifndef WITHOUT_BLS_WATCHDOGS    
    status = osal_snv_read(cbNVI_WATCHDOG_ID, sizeof(cbBLS_Watchdog), &bls.wd);
    if (status != SUCCESS)
    {
        bls.wd.writeTimeout = cbBLS_DEFAULT_WD_WRITE_TIMEOUT;
        bls.wd.connectionTimeout = cbBLS_DEFAULT_WD_CONNECTION_TIMEOUT;
        bls.wd.inactivityTimeout = cbBLS_DEFAULT_WD_INACTIVITY_TIMEOUT;       
        bls.wd.disconnectReset = cbBLS_DEFAULT_WD_DISCONNECT_RESET;
    }
    
    bls.wdWriteTimerId = INVALID_TIMER_ID;
    bls.wdConnectTimerId = INVALID_TIMER_ID;
    bls.wdInactivityTimerId = INVALID_TIMER_ID;
#endif
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
Status_t cbBLS_registerCallbacks(cbBLS_Callbacks *pCallb)
{
    Status_t    result = FAILURE;
    uint8       i;

    cb_ASSERT(pCallb != NULL);

    for (i = 0; ((i < cbBLS_MAX_CALLBACKS) && (result == FAILURE)); i++)
    {
        if (blsCallbacks[i] == NULL)
        {
            blsCallbacks[i] = pCallb;
            result = SUCCESS;
        }
    }
    return result;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
Status_t cbBLS_open(uint8 port, void* pCfg)
{
  uint8 result;
  cb_ASSERT(port == cbBLS_PORT_0);

  switch (bls.state)
  {
  case cbBLS_S_CLOSED:
    if (bls.bufId == UNITIALIZED_BUF_ID)
    {
      /* First time only */
      result = cbBUF_open(cbBLS_BUFFER_SIZE, 0, 0, &bls.bufId);    
      cb_ASSERT(result == cbBUF_OK);
#ifndef CB_CENTRAL
      cbSPS_register(&serialServiceCallbacks);
#endif
    }
#ifndef CB_CENTRAL
    cbSPS_enable();
#endif

    bls.state = cbBLS_S_IDLE;
    break;

  case cbBLS_S_INACTIVE: //TODO return FAILURE?
    break;

  default:
#ifndef CB_CENTRAL
    cb_ASSERT(FALSE);
#endif
    break;
  }  
  
  return SUCCESS;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
Status_t cbBLS_close(uint8 port)
{
  cb_ASSERT(port == cbBLS_PORT_0);

  switch (bls.state)
  {
  case cbBLS_S_CONNECTED:
    cbSPS_disable();
    /* In connected state, the disconnect callback is expected */
    bls.state = cbBLS_S_CLOSING;
    break;

  case cbBLS_S_WAIT_CONNECT:
      osal_CbTimerStop(bls.connTimerId);

      bls.pWriteBuf = NULL;
      bls.writeBufCurrentSize = 0;

      /* Fall through */
  case cbBLS_S_IDLE:
    cbSPS_disable();
    /* In idle state, the service is immediately disabled */
    bls.state = cbBLS_S_CLOSED;
    break;

  case cbBLS_S_INACTIVE:
      break;

  default:
    cb_ASSERT(FALSE);
    break;
  }  
  return SUCCESS; 
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
Status_t cbBLS_write(uint8 port,
                     uint8 *pBuf, 
                     uint16 bufSize)
{
  int16   result = SUCCESS;
  int16   res;

#ifndef CB_CENTRAL
  
  cb_ASSERT(port == cbBLS_PORT_0);
  cb_ASSERT((pBuf != NULL) && (bufSize > 0));


  switch(bls.state)
  {
  case cbBLS_S_CONNECTED:
    switch (bls.txState)
    {
    case cbBLS_S_TX_IDLE:
      {
        if (bufSize > cbSPS_FIFO_SIZE)
        {
          bls.writeBufCurrentSize = cbSPS_FIFO_SIZE;
        }
        else
        {
          bls.writeBufCurrentSize = bufSize;
        }

        bls.writeBufTransmittedSize = 0;

        res = cbSPS_reqData(
          bls.connHandle,
          pBuf, 
          bls.writeBufCurrentSize);

        if(res == SUCCESS)
        {
#ifndef WITHOUT_BLS_WATCHDOGS    
          kickInactivityTimeoutWd();
          startWriteTimeoutWd();
#endif
          bls.pWriteBuf = pBuf;
          bls.writeBufTotalSize = bufSize;

          bls.txState = cbBLS_S_TX_IN_PROGRESS;
        }
        else
        {
          bls.writeBufCurrentSize = 0;
          result = FAILURE;
        }
      }
      break;   
          
    case cbBLS_S_TX_IN_PROGRESS:
    default:
      cb_ASSERT(FALSE);
      break;
    }
    break;

  case cbBLS_S_IDLE:
      result = blsCallbackNotifyRequestConnection(cbBLS_PORT_0);

      if (result == SUCCESS)
      {
          bls.pWriteBuf = pBuf;
          bls.writeBufTotalSize = bufSize;
          bls.state = cbBLS_S_WAIT_CONNECT;

          osal_CbTimerStart(connTimeout, NULL, cbBLS_CONNECTION_TIMEOUT, &(bls.connTimerId));
      }
    break;

  default:
    result = FAILURE;
    break;
  }

#endif 
  
  return result;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
Status_t cbBLS_getReadBuf(uint8 port, uint8** ppBuf, uint16* pBufSize)
{
  Status_t result = SUCCESS;

  cb_ASSERT(port == cbBLS_PORT_0);
  cb_ASSERT(pBufSize != NULL);

  result = cbBUF_getReadBuf(bls.bufId, ppBuf, pBufSize);

  if(result != cbBUF_OK)
  {
    // RX buffer empty
    switch(bls.rxState)
    {
    case cbBLS_S_RX_DATA_AVAILABLE:
    case cbBLS_S_RX_BUF_FULL:
      bls.rxState = cbBLS_S_RX_BUF_EMPTY;        
      break;

    default:
      /* Ignore */
      break;
    }
  }

  return result;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
Status_t cbBLS_readBufConsumed(uint8 port, uint16 nBytes)
{
  int16   result;
  bool    empty;

  cb_ASSERT(port == cbBLS_PORT_0);
  cb_ASSERT(nBytes != 0);

  if (bls.state == cbBLS_S_CONNECTED)
  {
    result = cbBUF_readBufConsumed(bls.bufId, nBytes);
    cb_ASSERT(result == cbBUF_OK); 

    updateRemainingBufSize();

    switch (bls.rxState)
    {
    case cbBLS_S_RX_DATA_AVAILABLE:
    case cbBLS_S_RX_BUF_FULL:
      empty = cbBUF_isBufferEmpty(bls.bufId);
      if(empty == TRUE)
      {
        bls.rxState = cbBLS_S_RX_BUF_EMPTY;
      }
      else
      {
        bls.rxState = cbBLS_S_RX_DATA_AVAILABLE;
      }
      break;
  
    default:
      cb_ASSERT(FALSE);
      break;
    }
  }

  return SUCCESS;
}

/*---------------------------------------------------------------------------
 * Description of function. Optional verbose description.
 *-------------------------------------------------------------------------*/
Status_t cbBLS_readByte(uint8 port, uint8* pByte)
{
    Status_t   res;
    uint8*  pBuf;
    uint16  nBytes;

    cb_ASSERT(port == cbBLS_PORT_0);

    res = cbBLS_getReadBuf(port, &pBuf, &nBytes);

    if(res == SUCCESS)
    {
        cb_ASSERT(nBytes > 0);
        cb_ASSERT(pBuf != NULL);

        *pByte = pBuf[0];

        res = cbBLS_readBufConsumed(port, 1);
        cb_ASSERT(res == SUCCESS);
    }

    return res;
}

/*---------------------------------------------------------------------------
 * Description of function. Optional verbose description.
 *-------------------------------------------------------------------------*/
uint8 cbBLS_setServerProfile(uint8 val)
{
    uint8 status;

    cb_ASSERT(val == cbBLS_SERVER_PROFILE_SPP_LE ||
              val == cbBLS_SERVER_PROFILE_NONE);

    status = osal_snv_write(cbNVI_SERVER_PROFILE_ID, sizeof(uint8), &val);

    return status;
}

/*---------------------------------------------------------------------------
 * Description of function. Optional verbose description.
 *-------------------------------------------------------------------------*/
uint8 cbBLS_getServerProfile(uint8* pVal)
{
    uint8 status = SUCCESS;

    cb_ASSERT(pVal != NULL);

    *pVal = bls.serverProfile;

    return status;
}

#ifndef WITHOUT_BLS_WATCHDOGS    
/*---------------------------------------------------------------------------
 * Description of function. Optional verbose description.
 *-------------------------------------------------------------------------*/
uint8 cbBLS_setWatchdogConfig(uint16 writeTimeout, uint16 inactivityTimeout, uint16 connectTimeout, bool disconnectReset)
{
    uint8 status;
    cbBLS_Watchdog wd;

    wd.writeTimeout = writeTimeout;
    wd.inactivityTimeout = inactivityTimeout;
    wd.connectionTimeout = connectTimeout;    
    wd.disconnectReset = disconnectReset;

    status = osal_snv_write(cbNVI_WATCHDOG_ID, sizeof(cbBLS_Watchdog), &wd);

    return status;
}

/*---------------------------------------------------------------------------
 * Description of function. Optional verbose description.
 *-------------------------------------------------------------------------*/
uint8 cbBLS_getWatchdogConfig(uint16 *pWriteTimeout, uint16 *pInactivityTimeout, uint16 *pConnectTimeout, bool *pDisconnectReset)
{
    *pWriteTimeout = bls.wd.writeTimeout;
    *pInactivityTimeout = bls.wd.inactivityTimeout;
    *pConnectTimeout = bls.wd.connectionTimeout;    
    *pDisconnectReset = bls.wd.disconnectReset;
    return SUCCESS;
}
#endif
/*===========================================================================
* STATIC FUNCTIONS
*=========================================================================*/

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void spsDataEventCallback(uint16 connHandle, uint8 *pBuf, uint8 nBytes)
{
    bool isEscData = FALSE;

    cb_ASSERT(pBuf != NULL);
    cb_ASSERT((nBytes > 0) && (nBytes <= cbSPS_FIFO_SIZE));        

#ifndef WITHOUT_ESCAPE_SEQUENCE
    if (bls.escEnabled == TRUE)
    {
        isEscData = checkEsc(pBuf, nBytes);
    }
#endif
    
#ifndef WITHOUT_BLS_WATCHDOGS    
    kickInactivityTimeoutWd();
#endif
    
    switch(bls.rxState)
    {
    case cbBLS_S_RX_BUF_EMPTY:    
        if (isEscData == FALSE)
        {
            copyDataToBuf(bls.bufId, pBuf, nBytes);
            bls.rxState = cbBLS_S_RX_DATA_AVAILABLE;
            blsCallbackNotifyDataAvailable(cbBLS_PORT_0);
        }
        break;

    case cbBLS_S_RX_DATA_AVAILABLE:
        if (isEscData == FALSE)
        {
            copyDataToBuf(bls.bufId, pBuf, nBytes);
        }
        break;

    default:
        /* Buffer is already full - ignore */
        //cb_ASSERT(FALSE);
        break;    
    }
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void spsDataConfCallback(uint16 connHandle)
{
    uint8 res;
    uint16 size;

    cb_ASSERT(bls.state == cbBLS_S_CONNECTED || bls.state == cbBLS_S_CLOSING);

  switch (bls.txState)
  {
  case cbBLS_S_TX_IN_PROGRESS:
    cb_ASSERT(bls.writeBufCurrentSize != 0);

#ifndef WITHOUT_BLS_WATCHDOGS    
    stopWriteTimeoutWd();
#endif

    bls.writeBufTransmittedSize += bls.writeBufCurrentSize;

    if (bls.writeBufTotalSize == bls.writeBufTransmittedSize)
    {
        size = bls.writeBufTotalSize;
        bls.pWriteBuf = NULL;
        bls.writeBufTotalSize = 0;
        bls.txState = cbBLS_S_TX_IDLE;

        blsCallbackNotifyWriteComplete(cbBLS_PORT_0, size);
    }
    else
    {
        // Write next part of buffer
        uint16 bufSize = bls.writeBufTotalSize - bls.writeBufTransmittedSize;
                
        if (bufSize > cbSPS_FIFO_SIZE)
        {
            bls.writeBufCurrentSize = cbSPS_FIFO_SIZE;
        }
        else
        {
            bls.writeBufCurrentSize = bufSize;
        }

        res = cbSPS_reqData(
            bls.connHandle,
            &bls.pWriteBuf[bls.writeBufTransmittedSize], 
            bls.writeBufCurrentSize);

        if(res == SUCCESS)
        {
#ifndef WITHOUT_BLS_WATCHDOGS    
            startWriteTimeoutWd();
#endif
            bls.txState = cbBLS_S_TX_IN_PROGRESS;
        }
        else
        {
            size = bls.writeBufTransmittedSize;
            
            bls.pWriteBuf = NULL;
            bls.writeBufTotalSize = 0;
            bls.txState = cbBLS_S_TX_IDLE;

            blsCallbackNotifyWriteComplete(cbBLS_PORT_0, size);
        }
    }
    break;

  default:
    cb_ASSERT(FALSE);
    break;
  }
}


/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void spsConnectCallback(uint16 connHandle)
{
  Status_t  status = SUCCESS;
  bool      empty;

  bls.connHandle = connHandle;
  bls.txState = cbBLS_S_TX_IDLE;
  bls.rxState = cbBLS_S_RX_BUF_EMPTY;
  
#ifndef WITHOUT_ESCAPE_SEQUENCE  
  bls.escTimerId = INVALID_TIMER_ID;
  
  if (bls.escEnabled == TRUE)
  {
    // Ignore first pre escape timeout to be able to enter AT over BLE as fast as possible
    // after connection has been established
    bls.escState = cbBLS_S_ESC_PRE_ESCAPE_SEQ_IGNORED;    
  }
  else
  {
    bls.escState = cbBLS_S_ESC_IDLE;
  }
#endif
  
  switch(bls.state)
  {
  case cbBLS_S_WAIT_CONNECT:  
      bls.state = cbBLS_S_CONNECTED;

      osal_CbTimerStop(bls.connTimerId);

      if (bls.pWriteBuf != NULL)
      {
          status = cbBLS_write(cbBLS_PORT_0, bls.pWriteBuf, bls.writeBufTotalSize);
      }
      break;

  case cbBLS_S_IDLE:
      bls.state = cbBLS_S_CONNECTED;
      break;

  default:
      cb_ASSERT2(FALSE, bls.state);
      break;
  }

  empty = cbBUF_isBufferEmpty(bls.bufId);

  if (empty == FALSE)
  {
      bls.rxState = cbBLS_S_RX_DATA_AVAILABLE;
  }    

  updateRemainingBufSize();

#ifndef WITHOUT_BLS_WATCHDOGS    
  startConnectionTimeoutWd();
#endif

  if (status == FAILURE)
  {
      /* Write error */
      bls.pWriteBuf = NULL;
      bls.writeBufTotalSize = 0;

      /* Call WriteComplete in order to throw away the pending buffer and proceed. */
      blsCallbackNotifyWriteComplete(cbBLS_PORT_0, bls.writeBufTotalSize);
  }
}

/*---------------------------------------------------------------------------
* 
* @brief   This handler is registered to the Serial Port Service. It is 
*          called when the remote side writes the credits characteristics 
*          configuration.

*-------------------------------------------------------------------------*/
void spsDisconnectCallback(uint16 connHandle)
{
  disconnect();
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static cbBLS_State entryIdle(void)
{    
  cbBLS_State state = cbBLS_S_IDLE;
  resetLink();
  cbBUF_clear(bls.bufId);
  
  return state;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void updateRemainingBufSize(void)
{
  uint16 remBufSize;
  Status_t status;

  remBufSize = cbBUF_getNoFreeBytes(bls.bufId);
  status = cbSPS_setRemainingBufSize(bls.connHandle, remBufSize);
  cb_ASSERT(status == FALSE);
}


/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void disconnect(void)
{
  bool      writeCnf = FALSE;
  
#ifndef WITHOUT_ESCAPE_SEQUENCE
  // Stop escape timers
  osal_CbTimerStop(bls.escTimerId);
  bls.escTimerId = INVALID_TIMER_ID;
#endif
  
#ifndef WITHOUT_BLS_WATCHDOGS    
  /* If we disconnected because of entering AT mode we are in cbBLS_S_CLOSING, 
  in this case do not reset on disconnect */
  if (bls.wd.disconnectReset == TRUE && bls.state != cbBLS_S_CLOSING)
  {
      cbASSERT_resetHandler();
  }

  stopAllWdTimers();
#endif

  switch(bls.state)
  {
  case cbBLS_S_CONNECTED:

    if(bls.txState == cbBLS_S_TX_IN_PROGRESS)
    {
      writeCnf = TRUE;
    }            

    bls.rxState = cbBLS_S_INVALID;
    bls.txState = cbBLS_S_INVALID;

#ifndef WITHOUT_ESCAPE_SEQUENCE
    bls.escState = cbBLS_S_ESC_INVALID;
#endif
    
    bls.state = entryIdle();

    if(writeCnf == TRUE)
    {
      blsCallbackNotifyWriteComplete(cbBLS_PORT_0, 0);
      bls.writeBufCurrentSize = 0;
    }   
    break;

  case cbBLS_S_CLOSING:

    bls.state = cbBLS_S_CLOSED;
    bls.rxState = cbBLS_S_INVALID;
    bls.txState = cbBLS_S_INVALID;

#ifndef WITHOUT_ESCAPE_SEQUENCE
    bls.escState = cbBLS_S_ESC_INVALID;
#endif
    
    resetLink();
    cbBUF_clear(bls.bufId);

    break;

  default:
    // Ignore
    break;
  }
}


/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void resetLink(void)
{
  bls.pWriteBuf = NULL;
  bls.writeBufTotalSize = 0;
  bls.connHandle = INVALID_CONNHANDLE;
}
#ifndef WITHOUT_ESCAPE_SEQUENCE
/*---------------------------------------------------------------------------
* Check if the data is part of a possible escape sequence.
* If data is part of a possible escape sequence then TRUE is returned 
* and the data shall not be copied into the receive buffer.
*-------------------------------------------------------------------------*/
static bool checkEsc(uint8* pData, int16 nBytes)
{
   bool isEscData = FALSE; 
   uint8 nPrevEscBytes = bls.nEscBytes;

   cb_ASSERT(nBytes > 0);
   cb_ASSERT(pData != NULL);

   switch (bls.escState)
   {
   case cbBLS_S_ESC_IDLE:
       // Ignore
       startPreEscTimer();
       break;

   case cbBLS_S_ESC_PRE_ESCAPE_SEQ:
   case cbBLS_S_ESC_PRE_ESCAPE_SEQ_IGNORED:
        if (nBytes <= cbESC_NUM_ESCAPE_CHARS)
        {
            isEscData = TRUE;
            for (uint8 i = 0; ((i < nBytes) && (isEscData == TRUE)); i++)
            {
                if (pData[i] == bls.escChar)
                {
                    bls.nEscBytes++;
                }
                else
                {
                    isEscData = FALSE; 
                }
            }
            
            if (isEscData == TRUE) 
            {
                if (bls.nEscBytes == cbESC_NUM_ESCAPE_CHARS)
                {
                    bool ignoreEscapeTiming = FALSE;
                    if (bls.escState == cbBLS_S_ESC_PRE_ESCAPE_SEQ_IGNORED)
                    {
                        // When the valid escape sequence is the first data received 
                        // on the connection the escape timing is ignored. Note that
                        // for this to work all three escape characters must be received
                        // in the same packet.
                        ignoreEscapeTiming = TRUE;
                    }

                    startPostEscTimer(ignoreEscapeTiming);
                    
                    bls.escState = cbBLS_S_ESC_POST_ESCAPE_SEQ;
                }
                else
                {
                    startWithinEscTimer();
                    bls.escState = cbBLS_S_ESC_WITHIN_ESCAPE_SEQ;
                }
            }
            else
            {
                startPreEscTimer();         
                bls.escState = cbBLS_S_ESC_IDLE;
            }
        }
        break;

   case cbBLS_S_ESC_WITHIN_ESCAPE_SEQ:
       if (nBytes <= (cbESC_NUM_ESCAPE_CHARS - nPrevEscBytes))
       {
           isEscData = TRUE;
           for (uint8 i = 0; ((i < nBytes) && (isEscData == TRUE)); i++)
           {
               if (pData[i] == bls.escChar)
               {
                   bls.nEscBytes++;
               }
               else
               {
                   isEscData = FALSE; 
               }
           }

           if (bls.nEscBytes == cbESC_NUM_ESCAPE_CHARS)
           {
               startPostEscTimer(FALSE);
               bls.escState = cbBLS_S_ESC_POST_ESCAPE_SEQ;
           }
       }
       
       if (isEscData == FALSE)
       {
           cb_ASSERT(nPrevEscBytes > 0);

           abortEsc(nPrevEscBytes);
           startPreEscTimer();

           bls.escState = cbBLS_S_ESC_IDLE;
       }
       break;
        
   
   case cbBLS_S_ESC_POST_ESCAPE_SEQ:
       // Data received within the post escape timeout. 
       // Escape is invalid and the scape sequence is written to the rx buffer.

       cb_ASSERT(bls.nEscBytes == cbESC_NUM_ESCAPE_CHARS);

       abortEsc(bls.nEscBytes);

       startPreEscTimer();         
       bls.escState = cbBLS_S_ESC_IDLE;
       break;

   default:
    cb_EXIT(bls.escState);
   }

   return isEscData;
}

/*---------------------------------------------------------------------------
* Copy escape data to rx buffer
*-------------------------------------------------------------------------*/
static void abortEsc(uint8 nBytes)
{
    uint8 res;
    
    for (uint8 i = 0; i < nBytes; i ++)
    {
        res = cbBUF_writeByte(bls.bufId, bls.escChar);
        cb_ASSERT(res == cbBUF_OK);
    }

    bls.nEscBytes = 0;
}

/*---------------------------------------------------------------------------
* Stop current escape timer and start the pre escape timer.
*-------------------------------------------------------------------------*/
static void startPreEscTimer(void)
{
    uint8 status;
    uint16 t = cbESC_getPreEscTimout();

    osal_CbTimerStop(bls.escTimerId);

    status = osal_CbTimerStart(preEscTimeout, NULL, t, &(bls.escTimerId));
    cb_ASSERT(status == SUCCESS);
}

/*---------------------------------------------------------------------------
* Stop current escape timer and start the within escape timer.
*-------------------------------------------------------------------------*/
static void startWithinEscTimer(void)
{
    uint8 status;
    uint16 t = cbESC_getWithinEscTimout();
    
    osal_CbTimerStop(bls.escTimerId);

    status = osal_CbTimerStart(withinEscTimeout, NULL, t, &(bls.escTimerId));
    cb_ASSERT(status == SUCCESS);
}

/*---------------------------------------------------------------------------
* Stop current escape timer and start the post escape timer.
*-------------------------------------------------------------------------*/
void startPostEscTimer(bool ignoreTiming)
{
    uint8 status;
    uint16 t;

    osal_CbTimerStop(bls.escTimerId);

    if (ignoreTiming == FALSE)
    {
        t = cbESC_getPostEscTimout();
    }
    else
    {
        t = 1;
    }

    status = osal_CbTimerStart(postEscTimeout, NULL, t, &(bls.escTimerId));
    cb_ASSERT(status == SUCCESS);
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void preEscTimeout(uint8* pData)
{
    if (bls.escTimerId != INVALID_TIMER_ID)
    {
        cb_ASSERT(bls.nEscBytes == 0);
        cb_ASSERT((bls.escState == cbBLS_S_ESC_IDLE) || (bls.escState == cbBLS_S_ESC_INVALID));

        bls.escTimerId = INVALID_TIMER_ID;

        bls.escState = cbBLS_S_ESC_PRE_ESCAPE_SEQ;    
    }
}

/*---------------------------------------------------------------------------
* Copy received part of escape sequence to rx buffer.
*-------------------------------------------------------------------------*/
static void withinEscTimeout(uint8* pData)
{
    cb_ASSERT(bls.escTimerId != INVALID_TIMER_ID)
    cb_ASSERT(bls.escState == cbBLS_S_ESC_WITHIN_ESCAPE_SEQ);

    bls.escTimerId = INVALID_TIMER_ID;

    abortEsc(bls.nEscBytes);
    startPreEscTimer();         
    bls.escState = cbBLS_S_ESC_IDLE;

    if (bls.rxState == cbBLS_S_RX_BUF_EMPTY)
    {
        bls.rxState = cbBLS_S_RX_DATA_AVAILABLE;
        blsCallbackNotifyDataAvailable(cbBLS_PORT_0);
    }
}

/*---------------------------------------------------------------------------
* Valid escape sequence within valid post escape timeout
*-------------------------------------------------------------------------*/
static void postEscTimeout(uint8* pData)
{
    cb_ASSERT(bls.escTimerId != INVALID_TIMER_ID);
    cb_ASSERT(bls.escState == cbBLS_S_ESC_POST_ESCAPE_SEQ);
    cb_ASSERT(bls.nEscBytes == cbESC_NUM_ESCAPE_CHARS);

    bls.escTimerId = INVALID_TIMER_ID;
    
    startPreEscTimer();
    bls.nEscBytes = 0;
    bls.escState = cbBLS_S_ESC_IDLE;

    blsCallbackNotifyEscape(cbBLS_PORT_0);
}
#endif //WITHOUT_ESCAPE_SEQUENCE
/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void copyDataToBuf(int16 bufId, uint8* pData, int16 nBytes)
{
  int16   res;
  uint8*  pBuf;
  uint16  bufSize;
  int16   n = 0;
  bool    done = FALSE;

  while(!done)
  {
    res = cbBUF_getWriteBuf(bufId, &pBuf, &bufSize);
    if(res == cbBUF_OK)
    {
      if(bufSize >= nBytes)
      {
        osal_memcpy(pBuf, &(pData[n]), nBytes);

        res = cbBUF_writeBufProduced(bufId, nBytes);
        cb_ASSERT(res == SUCCESS);

        updateRemainingBufSize();
        done = TRUE;
      }
      else
      {
        osal_memcpy(pBuf, &(pData[n]), bufSize);

        n += bufSize;
        nBytes -= bufSize;

        res = cbBUF_writeBufProduced(bufId, bufSize);
        cb_ASSERT(res == SUCCESS);
        
        updateRemainingBufSize();
      }
    }
    else
    {
      /* Buffer overflow. Data lost!!!!!! */
      done = TRUE; 
    }
  }
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void connTimeout(uint8* idp)
{
    cb_ASSERT(bls.state == cbBLS_S_WAIT_CONNECT);

    bls.state = cbBLS_S_IDLE;
    bls.pWriteBuf = NULL;
    bls.writeBufTotalSize = 0;

    /* Call WriteComplete in order to throw away the pending buffer and proceed. */
    blsCallbackNotifyWriteComplete(cbBLS_PORT_0, bls.writeBufTotalSize);
}

#ifndef WITHOUT_BLS_WATCHDOGS    
/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void kickInactivityTimeoutWd(void)
{
    Status_t status;
    uint16 timeoutInMs = bls.wd.inactivityTimeout * 1000;

    /* This ASSERT happened during connection problem, and after investigation
       it seems that it should not be able to happen. Comment out for now.. */
    //cb_ASSERT(bls.state == cbBLS_S_CONNECTED);

    if (bls.wd.inactivityTimeout != 0)
    {
        if (bls.wdInactivityTimerId == INVALID_TIMER_ID)
        {
            status = osal_CbTimerStart(wdTimeout, &(bls.wdInactivityTimerId), timeoutInMs, &(bls.wdInactivityTimerId));
            cb_ASSERT(status == SUCCESS);    
        }
        else
        {            
            osal_CbTimerUpdate(bls.wdInactivityTimerId, timeoutInMs);
            // Ignore return code, failure can be returned if the timer has expired
        }
    }
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void stopInactivityTimeoutWd(void)
{
    osal_CbTimerStop(bls.wdInactivityTimerId);
    bls.wdInactivityTimerId = INVALID_TIMER_ID;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void startWriteTimeoutWd(void)
{
    uint8 status;

    if (bls.wd.writeTimeout != 0)
    {
        status = osal_CbTimerStart(wdTimeout, &(bls.wdWriteTimerId), bls.wd.writeTimeout * 1000, &(bls.wdWriteTimerId));
        cb_ASSERT(status == SUCCESS);   
    }
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void stopWriteTimeoutWd(void)
{
    osal_CbTimerStop(bls.wdWriteTimerId);
    bls.wdWriteTimerId = INVALID_TIMER_ID;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void startConnectionTimeoutWd(void)
{
    uint8 status;

    if (bls.wd.connectionTimeout != 0)
    {
        status = osal_CbTimerStart(wdTimeout, &(bls.wdConnectTimerId), bls.wd.connectionTimeout * 1000, &(bls.wdConnectTimerId));
        cb_ASSERT(status == SUCCESS);   
    }
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void stopConnectionTimeoutWd(void)
{
    osal_CbTimerStop(bls.wdConnectTimerId);
    bls.wdConnectTimerId = INVALID_TIMER_ID;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void stopAllWdTimers(void)
{
    stopConnectionTimeoutWd();
    stopWriteTimeoutWd();    
    stopInactivityTimeoutWd();
}

/*---------------------------------------------------------------------------
* The same timer callback is used for all watchdogs. The pData points to
* the timerID stored in bls variable. If a watchdog has expired then
* initiate a disconnect.
*-------------------------------------------------------------------------*/
static void wdTimeout(uint8* pData)
{
    *pData = INVALID_TIMER_ID;

     // Disconnect
     // TODO: Is this the correct way to do it?
     cbSPS_disable();
     cbSPS_enable();
}
#endif
/*---------------------------------------------------------------------------
* Notify all registered users
*-------------------------------------------------------------------------*/
static void blsCallbackNotifyDataAvailable(uint8 port)
{
    uint8 i;

    for(i = 0; (i < cbBLS_MAX_CALLBACKS); i++)
    {
        if ((blsCallbacks[i] != NULL) &&  
            (blsCallbacks[i]->dataAvailableCallback != NULL))
        {
            blsCallbacks[i]->dataAvailableCallback(port);
        }
    }
}

static void blsCallbackNotifyWriteComplete(uint8 port, uint16 bufSize)
{
    uint8 i;

    for(i = 0; (i < cbBLS_MAX_CALLBACKS); i++)
    {
        if ((blsCallbacks[i] != NULL) &&  
            (blsCallbacks[i]->writeCompleteCallback != NULL))
        {
            blsCallbacks[i]->writeCompleteCallback(port, bufSize);
        }
    }
}

// This function is temporarily disabled to avoid warning
#if 0
static void blsCallbackNotifyError(uint8 port, uint8 error)
{
    uint8 i;

    for(i = 0; (i < cbBLS_MAX_CALLBACKS); i++)
    {
        if ((blsCallbacks[i] != NULL) &&  
            (blsCallbacks[i]->errorCallback != NULL))
        {
            blsCallbacks[i]->errorCallback(port, error);
        }
    }
}
#endif

static uint8 blsCallbackNotifyRequestConnection(uint8 port)
{
    uint8 i;
    uint8 status = FAILURE;

    for(i = 0; (i < cbBLS_MAX_CALLBACKS) && status == FAILURE; i++)
    {
        if ((blsCallbacks[i] != NULL) &&  
            (blsCallbacks[i]->requestConnectionCallback != NULL))
        {
            status = blsCallbacks[i]->requestConnectionCallback(port);
        }
    }

    return status;
}

#ifndef WITHOUT_ESCAPE_SEQUENCE
static void blsCallbackNotifyEscape(uint8 port)
{
    uint8 i;


    for(i = 0; (i < cbBLS_MAX_CALLBACKS); i++)
    {
        if ((blsCallbacks[i] != NULL) &&  
            (blsCallbacks[i]->escapeCallback != NULL))
        {
            blsCallbacks[i]->escapeCallback(port);
        }
    }
}
#endif


