/*---------------------------------------------------------------------------
* Copyright (c) 2000, 2001 connectBlue AB, Sweden.
* Any reproduction without written permission is prohibited by law.
*
* Component   : Serial Port Service 
* File        : cb_serial_service.c
*
* Description : Implementation of Serial Port Service service.
*-------------------------------------------------------------------------*/
#include "bcomdef.h"
#include "OSAL.h"
#ifndef cbSPS_INDICATIONS
#include "OSAL_Timers.h"
#endif
#include "linkdb.h"
#include "att.h"
#include "gatt.h"
#include "gatt_uuid.h"
#include "gattservapp.h"
#include "gapbondmgr.h"
#include "hal_assert.h"
#include "cb_log.h"

#include "cb_assert.h"
#include "cb_serial_service.h"
#include "peripheral.h"

#ifdef cbSPS_READ_SECURITY_MODE
#include "cb_gap.h"
#include "cb_sec.h"
#endif

/*===========================================================================
* DEFINES
*=========================================================================*/
#define cbSPS_DEBUG

#define ATTRIBUTE128(uuid, pProps, pValue) { {ATT_UUID_SIZE, uuid}, pProps, 0, (uint8*)pValue}
#define ATTRIBUTE16(uuid, pProps, pValue) { {ATT_BT_UUID_SIZE, uuid}, pProps, 0, (uint8*)pValue}

#ifndef cbSPS_MAX_CALLBACKS
#define cbSPS_MAX_CALLBACKS (4)
#endif

#define cbSPS_MAX_LINKS                               (1)
#define cbSPS_INVALID_ID                              (0xFF)

#define cbSPS_POLL_TX_EVENT                           (1 << 0)
#define cbSPS_TX_POLL_TIMEOUT_IN_MS                   (10)      



/*===========================================================================
* TYPES
*=========================================================================*/
typedef enum
{
  SPS_S_NOT_VALID = 0,
  SPS_S_IDLE,
  SPS_S_CONNECTED,

  SPS_S_TX_IDLE,
#ifndef cbSPS_INDICATIONS
  SPS_S_TX_WAIT,
#else
  SPS_S_TX_WAIT_FIFO_WRITE_CNF,
  SPS_S_TX_WAIT_CREDITS_WRITE_CNF,
#endif
  SPS_S_RX_READY

} cbSPS_State;

typedef struct  
{
  uint8         taskId;
  cbSPS_State   state;
  cbSPS_State   txState;
  cbSPS_State   rxState;
  bool          enabled;
  uint8         txCredits; // Number of packets that can be sent
  uint8         rxCredits; // Number of packets that remote side can send
  uint16        connHandle;
  bool          secureConnection; // Encryption required 

  uint16        remainingBufSize;

  uint8         *pPendingTxBuf;
  uint8         pendingTxBufSize;
#ifdef cbSPS_DEBUG
  uint32        dbgTxCount;
  uint32        dbgRxCount;
  uint32        dbgTxCreditsCount;
  uint32        dbgRxCreditsCount;
#endif
} cbSPS_Class;

/*===========================================================================
* DECLARATIONS
*=========================================================================*/
// Operations registered to BLE stack
static void handleConnStatusCB( uint16 connHandle, uint8 changeType );
static uint8 readAttrCB( uint16 connHandle, gattAttribute_t *pAttr, uint8 *pValue, uint8 *pLen, uint16 offset, uint8 maxLen );
static bStatus_t writeAttrCB( uint16 connHandle, gattAttribute_t *pAttr, uint8 *pValue, uint8 len, uint16 offset );
//static bStatus_t authorizeAttrCB( uint16 connHandle, gattAttribute_t *pAttr, uint8 opcode );

// Operations handles incoming write operations
static void creditsReceviceHandler(uint16 connHandle, uint8 credits);
static void fifoReceiveHandler(uint16 connHandle, uint8 *pBuf, uint8 size);

// Operations that sends indications to remote device
static bStatus_t writeFifo(uint16 connHandle, uint8 *pBuf, uint8 size);
static bStatus_t writeCredits(uint16 connHandle, uint8 credits);

// Operations used to call a set of registered callbacks
static void connectEvtCallback(uint16 connHandle);
static void disconnectEvtCallback(uint16 connHandle);
static void dataEvtCallback(uint16 connHandle, uint8 *pBuf, uint8 size);
static void dataCnfCallback(uint16 connHandle);

#ifdef cbSPS_INDICATIONS
static void handleIndConf(uint16 connHandle);
#endif
static void handleCreditsCharConfigChange(uint16 connHandle, bool enabled);
static void pollTx(void);
static void resetLink(void);



/*===========================================================================
* DEFINITIONS
*=========================================================================*/
// Filename used by cb_ASSERT macro
static const char *file = "SPS";

CONST uint8 cbSPS_servUUID[ATT_UUID_SIZE] = {cbSPS_SERIAL_SERVICE_UUID};
CONST uint8 cbSPS_modeUUID[ATT_UUID_SIZE] = {cbSPS_MODE_UUID};
CONST uint8 cbSPS_fifoUUID[ATT_UUID_SIZE] = { cbSPS_FIFO_UUID };
CONST uint8 cbSPS_creditsUUID[ATT_UUID_SIZE] = { cbSPS_CREDITS_UUID };

CONST gattAttrType_t cbSPS_serviceUUID = { ATT_UUID_SIZE, cbSPS_servUUID };

// Characteristic properties
static uint8 modeCharProps = GATT_PROP_READ | GATT_PROP_WRITE_NO_RSP; 
#ifdef cbSPS_INDICATIONS
static uint8 fifoCharProps = GATT_PROP_WRITE_NO_RSP | GATT_PROP_NOTIFY | GATT_PROP_INDICATE; 
static uint8 creditsCharProps = GATT_PROP_WRITE_NO_RSP | GATT_PROP_NOTIFY | GATT_PROP_INDICATE; 
#else
static uint8 fifoCharProps = GATT_PROP_WRITE_NO_RSP | GATT_PROP_NOTIFY /*| GATT_PROP_INDICATE*/; 
static uint8 creditsCharProps = GATT_PROP_WRITE_NO_RSP | GATT_PROP_NOTIFY /*| GATT_PROP_INDICATE*/; 
#endif

// Characteristic configurations
static gattCharCfg_t modeCharConfig; 
static gattCharCfg_t fifoCharConfig; 
static gattCharCfg_t creditsCharConfig;

//Characteristic data
static uint8 mode = 0;
static uint8 fifo[1]; // Note that no data is ever stored here. Size set to 1 to save memory
static uint8 credits;

// Attribute handles that are cached for faster access
static uint16 attrHandleFifo = 0;
static uint16 attrHandleCredits = 0;
static uint16 attrHandleCreditsConfig = 0;

// Attribute table
static gattAttribute_t spsAttrTbl[] = 
{
  // Serial Port Service
  ATTRIBUTE16( primaryServiceUUID, GATT_PERMIT_READ, &cbSPS_serviceUUID),

  // Mode Characteristic
  ATTRIBUTE16(characterUUID     , GATT_PERMIT_READ, &modeCharProps),
  ATTRIBUTE128(cbSPS_modeUUID   , GATT_PERMIT_READ | GATT_PERMIT_WRITE , &mode),
  ATTRIBUTE16(clientCharCfgUUID , GATT_PERMIT_READ | GATT_PERMIT_WRITE , &modeCharConfig),

  // Fifo Characteristic
  ATTRIBUTE16(characterUUID     , GATT_PERMIT_READ, &fifoCharProps),
  ATTRIBUTE128(cbSPS_fifoUUID   , GATT_PERMIT_WRITE , fifo),
  ATTRIBUTE16(clientCharCfgUUID , GATT_PERMIT_READ | GATT_PERMIT_WRITE , &fifoCharConfig),

  // Credits Characteristic
  ATTRIBUTE16(characterUUID     , GATT_PERMIT_READ, &creditsCharProps),
  ATTRIBUTE128(cbSPS_creditsUUID, GATT_PERMIT_WRITE , &credits),
  ATTRIBUTE16(clientCharCfgUUID , GATT_PERMIT_READ | GATT_PERMIT_WRITE , &creditsCharConfig)
};

CONST gattServiceCBs_t serialCBs =
{
  readAttrCB,       // Read callback function pointer
  writeAttrCB,      // Write callback function pointer
  NULL,             // authorizeAttrCB,  Authorization callback function pointer
};

static cbSPS_Callbacks *spsCallbacks[cbSPS_MAX_CALLBACKS] = {NULL, NULL, NULL, NULL};
static cbSPS_Class sps;

/*===========================================================================
* FUNCTIONS
*=========================================================================*/

void cbSPS_init(uint8 taskId)
{
  sps.taskId = taskId;
  sps.state = SPS_S_IDLE;
  sps.txState = SPS_S_NOT_VALID;
  sps.rxState = SPS_S_NOT_VALID;
  sps.enabled = FALSE;
  sps.secureConnection = FALSE;
  resetLink();

#ifdef cbSPS_DEBUG
  sps.dbgTxCount = 0;
  sps.dbgRxCount = 0;
  sps.dbgTxCreditsCount = 0;  
  sps.dbgRxCreditsCount = 0;
#endif
}

/*---------------------------------------------------------------------------
* Register service to GATT
*-------------------------------------------------------------------------*/
void cbSPS_addService(void)
{
  uint8 status;
  gattAttribute_t *pAttr;

  linkDB_Register( handleConnStatusCB );    
  GATTServApp_InitCharCfg( INVALID_CONNHANDLE, &modeCharConfig );
  GATTServApp_InitCharCfg( INVALID_CONNHANDLE, &fifoCharConfig );
  GATTServApp_InitCharCfg( INVALID_CONNHANDLE, &creditsCharConfig );  

  status = GATTServApp_RegisterService( spsAttrTbl, GATT_NUM_ATTRS( spsAttrTbl ), &serialCBs );
  cb_ASSERT(status == SUCCESS);

  //Init attribute handles needed for indications and notifications

  pAttr = GATTServApp_FindAttr(spsAttrTbl, GATT_NUM_ATTRS( spsAttrTbl ), fifo );
  cb_ASSERT(pAttr != NULL);
  attrHandleFifo = pAttr->handle;

  pAttr = GATTServApp_FindAttr(spsAttrTbl, GATT_NUM_ATTRS( spsAttrTbl ), (uint8*)&credits );
  cb_ASSERT(pAttr != NULL);
  attrHandleCredits = pAttr->handle;

  pAttr = GATTServApp_FindAttr(spsAttrTbl, GATT_NUM_ATTRS( spsAttrTbl ), (uint8*)&creditsCharConfig );
  cb_ASSERT(pAttr != NULL);
  attrHandleCreditsConfig = pAttr->handle;

#ifdef cbSPS_READ_SECURITY_MODE
  {
      cbSEC_SecurityMode securityMode;
      status = cbSEC_getSecurityMode(&securityMode);
      cb_ASSERT(status == SUCCESS);

      if (securityMode == cbSEC_SEC_MODE_AUTOACCEPT)
      {
          cbSPS_setSecurity(FALSE, FALSE);
      }
      else if (securityMode == cbSEC_SEC_MODE_JUST_WORKS)
      {
          // Encryption required
          cbSPS_setSecurity(TRUE, FALSE);
      }
      else
      {
          cbSPS_setSecurity(TRUE, TRUE);
      }
  }
#endif
}

/*---------------------------------------------------------------------------
 * Security settings
 * - encryption, set to to TRUE to enforce that the link is encrypted. This 
 *              will trig a an unauthenticated bonding. (Just Works)
 * - authentication, set to TRUE to enforce that an authenticated and encrypted
 *              link is used. This will trig an authentication bonding.
 *-------------------------------------------------------------------------*/
void cbSPS_setSecurity(bool encryption, bool authentication)
{
  gattAttribute_t *pAttr;

  // List of attributes for which security config applies. Some of the attributes are always readable.
  uint8 *attrValuePointer[6] =  {&mode, fifo, &credits, (uint8*)&modeCharConfig, (uint8*)&fifoCharConfig, (uint8*)&creditsCharConfig};

  sps.secureConnection = encryption;

  for(uint8 i = 0; i < 6; i++)
  {
    pAttr = GATTServApp_FindAttr(spsAttrTbl, GATT_NUM_ATTRS( spsAttrTbl ), attrValuePointer[i] );
    cb_ASSERT(pAttr != NULL);
    
    if(authentication == TRUE)
    {
      // Only authenticated read / writes allowed
      if((pAttr->permissions & (GATT_PERMIT_READ | GATT_PERMIT_AUTHEN_READ)) != 0)
      {
        pAttr->permissions |= (GATT_PERMIT_AUTHEN_READ);
      }

      if((pAttr->permissions & (GATT_PERMIT_WRITE | GATT_PERMIT_AUTHEN_WRITE)) != 0)
      {

        pAttr->permissions |= (GATT_PERMIT_AUTHEN_WRITE);
      }
    }
    else
    {
      // Authenticated and unauthenticated read / writes allowed
      if((pAttr->permissions & (GATT_PERMIT_READ | GATT_PERMIT_AUTHEN_READ)) != 0)
      {
        pAttr->permissions &= ~GATT_PERMIT_AUTHEN_READ;        
      }

      if((pAttr->permissions & (GATT_PERMIT_WRITE | GATT_PERMIT_AUTHEN_WRITE)) != 0)
      {

        pAttr->permissions &= ~(GATT_PERMIT_AUTHEN_WRITE);
      }
    }    
  }
}

/*---------------------------------------------------------------------------
* Register callback functions
*-------------------------------------------------------------------------*/
extern void cbSPS_register(cbSPS_Callbacks *pCallbacks)
{
  uint8 i;
  bool  found = FALSE;

  cb_ASSERT(pCallbacks != NULL);

  for (i = 0; ((i < cbSPS_MAX_CALLBACKS) && (found == FALSE)); i++)
  {
    if (spsCallbacks[i] == NULL)
    {
      spsCallbacks[i] = pCallbacks;
      found = TRUE;
    }
  }
  cb_ASSERT(found == TRUE);
}

/*---------------------------------------------------------------------------
* Unregister callback functions
*-------------------------------------------------------------------------*/
extern void cbSPS_unregister(cbSPS_Callbacks *pCallbacks)
{
  uint8 i;
  bool  found = FALSE;

  cb_ASSERT(pCallbacks != NULL);

  for (i = 0; ((i < cbSPS_MAX_CALLBACKS) && (found == FALSE)); i++)
  {
    if (spsCallbacks[i] == pCallbacks)
    {
      spsCallbacks[i] = NULL;
      found = TRUE;
    }
  }

  cb_ASSERT(found == TRUE);
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
uint16 cbSPS_processEvent(uint8 taskId, uint16 events)
{
#ifdef cbSPS_INDICATIONS
  if ((events & SYS_EVENT_MSG) != 0)
  {    
    uint8* pMsg = osal_msg_receive(sps.taskId);

    if ( pMsg != NULL )
    {
      switch (((osal_event_hdr_t*)pMsg)->event )
      {
      case GATT_MSG_EVENT:
        if (((gattMsgEvent_t*)pMsg)->method == ATT_HANDLE_VALUE_CFM)
        {
          handleIndConf(((gattMsgEvent_t*)pMsg)->connHandle);
        }
        break;

      default:
        break;
      }

      osal_msg_deallocate(pMsg);
    }

    return (events ^ SYS_EVENT_MSG);
  }
#endif

  if ((events & cbSPS_POLL_TX_EVENT) != 0)
  {
    // Tx pending fifo data or send new credits to remote side
    pollTx();
    return (events ^ cbSPS_POLL_TX_EVENT);
  }

  return 0;
}



/*---------------------------------------------------------------------------
* Write fifo data. If notifications or indications have been enabled
* then fifo data will be sent to remote device.
*-------------------------------------------------------------------------*/
uint8 cbSPS_reqData(uint16 connHandle, uint8 *pBuf, uint8 size)
{
  bStatus_t status = FAILURE;

  cb_ASSERT(size != 0);
  cb_ASSERT(pBuf != NULL);
  cb_ASSERT(sps.pPendingTxBuf == NULL);
  cb_ASSERT(sps.pendingTxBufSize == 0);

  if (sps.state == SPS_S_CONNECTED)
  {
#ifdef cbSPS_INDICATIONS
    switch (sps.txState)
    {
    case SPS_S_TX_IDLE:
      if (sps.txCredits > 0)
      {
        status = writeFifo(connHandle, pBuf, size);
        if (status == SUCCESS)
        {
          sps.txState = SPS_S_TX_WAIT_FIFO_WRITE_CNF;
          sps.txCredits--;
        }
        else
        {
          // TBD if the write fails - maybe it shall be stored as a pending write?
          cb_ASSERT(FALSE);         
        }
      }
      else
      {
        sps.pPendingTxBuf = pBuf;
        sps.pendingTxBufSize = size;
        status = SUCCESS;
      }      
      break;

    case SPS_S_TX_WAIT_CREDITS_WRITE_CNF:
      /* Write in progress, store the */
      sps.pPendingTxBuf = pBuf;
      sps.pendingTxBufSize = size;
      status = SUCCESS;
      break;

    case SPS_S_TX_WAIT_FIFO_WRITE_CNF:
    default:
      cb_EXIT(sps.state);
      break;
    }    
#else
    switch (sps.txState)
    {
    case SPS_S_TX_IDLE:
    case SPS_S_TX_WAIT:
      sps.pPendingTxBuf = pBuf;
      sps.pendingTxBufSize = size;
      status = SUCCESS;
      osal_set_event(sps.taskId, cbSPS_POLL_TX_EVENT); 
      break;
    
    default:
      cb_EXIT(sps.state);
      break;
    }    
#endif
  }

  return status;
}

/*---------------------------------------------------------------------------
* Write credits. If notifications or indications have been enabled
* then credits will be sent to remote device.
*-------------------------------------------------------------------------*/
uint8 cbSPS_setRemainingBufSize(uint16 connHandle, uint16 size)
{
  bStatus_t status = FAILURE;

  if (sps.state == SPS_S_CONNECTED)
  {
    sps.remainingBufSize = size;
    osal_set_event(sps.taskId, cbSPS_POLL_TX_EVENT); 
    status = SUCCESS;
  }

  return status;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void cbSPS_enable(void)
{
    sps.enabled = TRUE;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void cbSPS_disable(void)
{
    sps.enabled = FALSE;

    if (sps.state == SPS_S_CONNECTED)
    {
        // Disconnect
        // TBD cbGAP not part of demo application
        GAPRole_TerminateConnection();
    }
}

/*===========================================================================
* STATIC FUNCTIONS
*=========================================================================*/

/*---------------------------------------------------------------------------
* Read callback
* Read is only allowed on the Mode characteristic
*-------------------------------------------------------------------------*/
static bStatus_t readAttrCB( uint16 connHandle, gattAttribute_t *pAttr, uint8 *pValue, uint8 *pLen, uint16 offset, uint8 maxLen )
{
  bStatus_t status = SUCCESS;
  *pLen = 0; 
  
  // Make sure it's not a blob operation (no attributes in the profile are long)
  if ( offset > 0 )
  {
    return ( ATT_ERR_ATTR_NOT_LONG );
  }

  if ((sps.secureConnection == TRUE) &&
      (linkDB_Encrypted(connHandle) == FALSE))
  {
      return ATT_ERR_INSUFFICIENT_AUTHEN;
      //return ATT_ERR_INSUFFICIENT_ENCRYPT; // Does not trig a bonding
  }    
  
  if ( pAttr->type.len == ATT_UUID_SIZE )
  {
    if (osal_memcmp(pAttr->type.uuid, cbSPS_modeUUID, ATT_UUID_SIZE) == TRUE)
    {
      *pLen = 1;      
      pValue[0] = pAttr->pValue[0];
      status = SUCCESS;
    }
    else
    {
      // Should never get here!
      *pLen = 0;
      status = ATT_ERR_ATTR_NOT_FOUND;
    }
  }
  else
  {
    // 16-bit BT UUID
    status = ATT_ERR_INVALID_HANDLE;
  }

  return ( status );
}

/*---------------------------------------------------------------------------
* Write callback
*-------------------------------------------------------------------------*/
static bStatus_t writeAttrCB( uint16 connHandle, gattAttribute_t *pAttr, uint8 *pValue, uint8 len, uint16 offset )
{
  bStatus_t status = SUCCESS;
 
  if ((sps.secureConnection == TRUE) &&
      (linkDB_Encrypted(connHandle) == FALSE))
  {
      return ATT_ERR_INSUFFICIENT_AUTHEN;
      //return ATT_ERR_INSUFFICIENT_ENCRYPT; // Does not trig a bonding
  }
  
  if ( pAttr->type.len == ATT_UUID_SIZE )
  {   
    // Make sure it is not a blob operation
    if ( offset != 0 )
    {
      status = ATT_ERR_ATTR_NOT_LONG;
    }
    else if (osal_memcmp(pAttr->type.uuid, cbSPS_modeUUID, ATT_UUID_SIZE) == TRUE)
    {
      if (len == 1)
      {
        pAttr->pValue[0] = pValue[0];
        cb_ASSERT(pAttr->pValue[0] == mode);
        //TODO Mode changes are not implemented
        //spsCallbacks->pfModeChangeEvent(connHandle, mode);
      }
      else
      {
        status = ATT_ERR_INVALID_VALUE_SIZE;
      }
    }
    else if (osal_memcmp(pAttr->type.uuid, cbSPS_fifoUUID, ATT_UUID_SIZE) == TRUE)
    {
      fifoReceiveHandler(connHandle, pValue, len);
    }
    else if (osal_memcmp(pAttr->type.uuid, cbSPS_creditsUUID, ATT_UUID_SIZE) == TRUE)
    {
      if (len == 1)
      {
        creditsReceviceHandler(connHandle, pValue[0]);
      }
      else
      {
        status = ATT_ERR_INVALID_VALUE_SIZE;
      }    
    }
    else
    {
      status = ATT_ERR_ATTR_NOT_FOUND;
    }
  }
  else if ( pAttr->type.len == ATT_BT_UUID_SIZE )
  {
    uint16 uuid = BUILD_UINT16( pAttr->type.uuid[0], pAttr->type.uuid[1]);
    
    switch (uuid)
    {
    case GATT_CLIENT_CHAR_CFG_UUID:

      if (sps.enabled == TRUE)         
      {        
        status = GATTServApp_ProcessCCCWriteReq(connHandle, pAttr, pValue, len, offset, GATT_CLIENT_CFG_NOTIFY | GATT_CLIENT_CFG_INDICATE);
                
        // Changing the characteristic configuration for the Credits characteristic
        // triggers connection and disconnection events.
        // During connection setup indications shall have been enabled for the fifo characteristic
        // before indications are enabled on the credits characteristic. 
        if((status == SUCCESS) &&            
           (pAttr->handle == attrHandleCreditsConfig) &&
           ((fifoCharConfig.value & (GATT_CLIENT_CFG_NOTIFY | GATT_CLIENT_CFG_INDICATE)) != 0))
        {
          if((creditsCharConfig.value & (GATT_CLIENT_CFG_NOTIFY | GATT_CLIENT_CFG_INDICATE)) != 0)             
          {
            handleCreditsCharConfigChange(connHandle, TRUE);
          }
          else
          {
            handleCreditsCharConfigChange(connHandle, FALSE);
          }
        }
      }
      else
      {
        status = ATT_ERR_WRITE_NOT_PERMITTED;
      }
      break;

    default:
      // Should never get here!
      status = ATT_ERR_ATTR_NOT_FOUND;
      break;
    }     
  }  

  return ( status );
}


/*---------------------------------------------------------------------------
* Connection status callback
*-------------------------------------------------------------------------*/
static void handleConnStatusCB( uint16 connHandle, uint8 changeType )
{ 
  // Make sure this is not loopback connection
  if(connHandle != LOOPBACK_CONNHANDLE)
  {
    // Reset Client Char Config if connection has dropped
    if((changeType == LINKDB_STATUS_UPDATE_REMOVED) ||
       ((changeType == LINKDB_STATUS_UPDATE_STATEFLAGS) && (!linkDB_Up(connHandle))))
    { 
      GATTServApp_InitCharCfg( connHandle, &modeCharConfig );
      GATTServApp_InitCharCfg( connHandle, &fifoCharConfig );
      GATTServApp_InitCharCfg( connHandle, &creditsCharConfig );        

      switch (sps.state)
      {
      case SPS_S_IDLE:  
        //ignore
        break;

      case SPS_S_CONNECTED:
        sps.state = SPS_S_IDLE;
        sps.txState = SPS_S_NOT_VALID;
        sps.rxState = SPS_S_NOT_VALID;    
        resetLink();

        disconnectEvtCallback(connHandle);
        break;

      default:
        cb_ASSERT(FALSE);
        break;
      }

    }
  }
}

/*---------------------------------------------------------------------------
* This operation sends credits or data. If fifo data is successfully
* written to a lower layer then the data cnf callback is called immidiately
* allowing a higher layers to start a new write. If data can not be written
* to lower layer then a new poll is trigged after a timeout.
*-------------------------------------------------------------------------*/
static void pollTx(void)
{
  bStatus_t status;
  uint8 newCredits;

  if (sps.state == SPS_S_CONNECTED)
  {
    switch (sps.txState)
    {
    case SPS_S_TX_IDLE:
#ifndef cbSPS_INDICATIONS
    case SPS_S_TX_WAIT:
#endif
      {
        if ((sps.rxCredits == 0) &&
            (sps.remainingBufSize > cbSPS_FIFO_SIZE))
        {
          newCredits = (sps.remainingBufSize / cbSPS_FIFO_SIZE) - sps.rxCredits;          

          status = writeCredits(sps.connHandle, newCredits);

          if (status == SUCCESS)
          {
            sps.remainingBufSize = 0;
            sps.rxCredits += newCredits;
             
#ifdef cbSPS_DEBUG
            sps.dbgRxCreditsCount += newCredits;       
#endif
            
#ifdef cbSPS_INDICATIONS
            sps.txState = SPS_S_TX_WAIT_CREDITS_WRITE_CNF;
#else
            // Trig another poll to send pending fifo data as well
            if (sps.pPendingTxBuf != NULL)
            {
              osal_set_event(sps.taskId, cbSPS_POLL_TX_EVENT);        
            }
#endif
          }
          else
          {
#ifdef cbSPS_INDICATIONS
            // TBD Remove after debugging
            // How shall this case be handled?
            cb_ASSERT(FALSE);
#else
            sps.txState = SPS_S_TX_WAIT;
            osal_start_timerEx(sps.taskId, cbSPS_POLL_TX_EVENT, cbSPS_TX_POLL_TIMEOUT_IN_MS);
            //osal_set_event(sps.taskId, cbSPS_POLL_TX_EVENT);
#endif
          }
        }
        else if ((sps.pPendingTxBuf != NULL) &&
                 (sps.txCredits > 0))
        {
          status = writeFifo(sps.connHandle, sps.pPendingTxBuf, sps.pendingTxBufSize);

          if (status == SUCCESS)
          {

#ifdef cbSPS_DEBUG
            sps.dbgTxCount += sps.pendingTxBufSize;
#endif
            sps.txCredits--;
            sps.pPendingTxBuf = NULL;
            sps.pendingTxBufSize = 0;
#ifdef cbSPS_INDICATIONS
            sps.txState = SPS_S_TX_WAIT_FIFO_WRITE_CNF;
#else
            sps.txState = SPS_S_TX_IDLE;

            dataCnfCallback(sps.connHandle);
#endif
          }
          else
          {
#ifdef cbSPS_INDICATIONS
            // TBD Remove after debugging
            // How shall this case be handled?
            cb_ASSERT(FALSE);
#else
             sps.txState = SPS_S_TX_WAIT;
             //osal_start_timerEx(sps.taskId, cbSPS_POLL_TX_EVENT, cbSPS_TX_POLL_TIMEOUT_IN_MS);
             osal_set_event(sps.taskId, cbSPS_POLL_TX_EVENT);
#endif
          }
        }
      }
      break;

#ifdef cbSPS_INDICATIONS
    case SPS_S_TX_WAIT_CREDITS_WRITE_CNF:
    case SPS_S_TX_WAIT_FIFO_WRITE_CNF:
      break;
#endif

    default:
      cb_ASSERT(FALSE);
      break;
    }
  }
}

/*---------------------------------------------------------------------------
* Reset link vartiables
*-------------------------------------------------------------------------*/
static void resetLink(void)
{
  sps.txCredits = 0;
  sps.rxCredits = 0;
  sps.connHandle = INVALID_CONNHANDLE;
  sps.remainingBufSize = 0;
  sps.pPendingTxBuf = NULL;
  sps.pendingTxBufSize = 0;
}

/*---------------------------------------------------------------------------
* Handle received credits
*-------------------------------------------------------------------------*/
static void creditsReceviceHandler(uint16 connHandle, uint8 credits)
{
  cb_ASSERT(credits != 0);

  switch(sps.state)
  {
  case SPS_S_IDLE:
    break;

  case SPS_S_CONNECTED:
    {
      switch (sps.txState)
      {
      case SPS_S_TX_IDLE:
#ifdef cbSPS_INDICATIONS
      case SPS_S_TX_WAIT_CREDITS_WRITE_CNF:
      case SPS_S_TX_WAIT_FIFO_WRITE_CNF:
#else
      case SPS_S_TX_WAIT:
#endif        
        sps.txCredits += credits;

#ifdef cbSPS_DEBUG
        sps.dbgTxCreditsCount += credits;       
#endif        
        osal_set_event(sps.taskId, cbSPS_POLL_TX_EVENT); 
        break;

      default:
        cb_ASSERT(FALSE);
        break;
      }
    }
    break;

  default:
    cb_EXIT(sps.state);
    break;
  }
}

/*---------------------------------------------------------------------------
* Handle incoming fifo data
*-------------------------------------------------------------------------*/
static void fifoReceiveHandler(uint16 connHandle, uint8 *pBuf, uint8 size)
{
  // During connect/disconnect test with data the assert below happened. 
  // Try to ignore this event if we are not connected (it seems we get a write
  // callback from the stack after we get the handleConnStatusCB)
  //cb_ASSERT(sps.state == SPS_S_CONNECTED); 

  if (sps.state == SPS_S_CONNECTED)
  {      
    switch (sps.rxState)
    {
    case SPS_S_RX_READY:
      sps.rxCredits--;
#ifdef cbSPS_DEBUG
      sps.dbgRxCount += size;
#endif
      dataEvtCallback(connHandle, pBuf, size);
      break;
  
    default:
      cb_ASSERT(FALSE);
      break;
    }
  }
}

/*---------------------------------------------------------------------------
* Handle credits characteristic configuration events. The state of 
* the credits characteristic defines if the connection state.
*-------------------------------------------------------------------------*/
void handleCreditsCharConfigChange(uint16 connHandle, bool enabled)
{
  cb_ASSERT(sps.enabled == TRUE);

  switch (sps.state)
  {
  case SPS_S_IDLE:    
    if (enabled == TRUE)
    {
	    sps.connHandle = connHandle;
	    sps.state = SPS_S_CONNECTED;
	    sps.txState = SPS_S_TX_IDLE;
	    sps.rxState = SPS_S_RX_READY;
	
	    connectEvtCallback(connHandle);
    }
    break;

  case SPS_S_CONNECTED:
    if (enabled == FALSE)
    {
	    sps.state = SPS_S_IDLE;
	    sps.txState = SPS_S_NOT_VALID;
	    sps.rxState = SPS_S_NOT_VALID;    
	    resetLink();	   

	    disconnectEvtCallback(connHandle);
    }
    break;

  default:
    cb_ASSERT(FALSE);
    break;
  }
}

#ifdef cbSPS_INDICATIONS
static void handleIndConf(uint16 connHandle)
{
  switch(sps.state)
  {
  case SPS_S_CONNECTED:
    {
      switch (sps.txState)
      {
      case SPS_S_TX_WAIT_FIFO_WRITE_CNF:
        sps.txState = SPS_S_TX_IDLE;
        dataCnfCallback(connHandle);
        osal_set_event(sps.taskId, cbSPS_POLL_TX_EVENT); 
        break;

      case SPS_S_TX_WAIT_CREDITS_WRITE_CNF:
        sps.txState = SPS_S_TX_IDLE;
        osal_set_event(sps.taskId, cbSPS_POLL_TX_EVENT); 
        break;

      default:
        cb_ASSERT(FALSE);
        break;
      }
    }
    break;

  default:
    //Ignore, probably disconnect in progress.
    break;    
  }
}
#endif

/*---------------------------------------------------------------------------
* Send indication with fifo attribute data to remote side
*-------------------------------------------------------------------------*/
#if 0
static uint8 previousByte = 0;
static uint8 previousStart = 0;
#endif

static bStatus_t writeFifo(uint16 connHandle, uint8 *pBuf, uint8 size)
{
  bStatus_t status = FAILURE;

#ifdef cbSPS_INDICATIONS
  attHandleValueInd_t attribute;
#else
  attHandleValueNoti_t attribute;
#endif

  cb_ASSERT(size <= cbSPS_FIFO_SIZE);


  if (((fifoCharConfig.value & (GATT_CLIENT_CFG_INDICATE | GATT_CLIENT_CFG_NOTIFY)) != 0) &&
    (fifoCharConfig.connHandle != INVALID_CONNHANDLE) &&
    (fifoCharConfig.connHandle == connHandle))
  {    
    cb_ASSERT(attrHandleFifo != 0);

#if 0
    // DBG
    {
        int     i;
        bool    err = FALSE;

        for(i = 0; (i < size) && (err == FALSE); i++)
        {
            if( ((previousByte + 1) != pBuf[i]) &&
                (pBuf[i] != 0) &&
                ((i != 0) || (previousStart != pBuf[0])))
            {
                err = TRUE;
            }
            previousByte = pBuf[i];
        }
        previousByte = pBuf[size - 1];
        previousStart = pBuf[0];
        if(err == TRUE)
        {
            osal_memset(pBuf, 0xFF, size);
        }
    }
    // End DBG
#endif
            
    attribute.handle = attrHandleFifo;
    attribute.len = size;
    osal_memcpy(attribute.value, pBuf, size);

#ifdef cbSPS_DEBUG
    sps.dbgTxCount += size;
#endif

#ifdef cbSPS_INDICATIONS
    status = GATT_Indication(fifoCharConfig.connHandle, &attribute, FALSE, sps.taskId);
#else
    status = GATT_Notification(fifoCharConfig.connHandle, &attribute, FALSE);
#endif
  }

  return status;
}

/*---------------------------------------------------------------------------
* Send indication with credits attribute data to remote side
*-------------------------------------------------------------------------*/
static bStatus_t writeCredits(uint16 connHandle, uint8 credits)
{
  bStatus_t status = FAILURE;

#ifdef cbSPS_INDICATIONS
  attHandleValueInd_t attribute;
#else
  attHandleValueNoti_t attribute;
#endif   

  if(((creditsCharConfig.value & (GATT_CLIENT_CFG_INDICATE | GATT_CLIENT_CFG_NOTIFY)) != 0) &&
    (creditsCharConfig.connHandle != INVALID_CONNHANDLE) &&
    (creditsCharConfig.connHandle == connHandle))
  {
    cb_ASSERT(attrHandleCredits != 0);
         
    attribute.handle = attrHandleCredits;
    attribute.len = 1;
    attribute.value[0] = credits;

#ifdef cbSPS_INDICATIONS
    status = GATT_Indication(creditsCharConfig.connHandle , &attribute, FALSE, sps.taskId);
#else
    status = GATT_Notification(creditsCharConfig.connHandle , &attribute, FALSE);
#endif
  }

  return status;
}


/*---------------------------------------------------------------------------
* Notify all registered users
*-------------------------------------------------------------------------*/
static void connectEvtCallback(uint16 connHandle)
{
  uint8 i;
  for(i = 0; (i < cbSPS_MAX_CALLBACKS); i++)
  {
    if ((spsCallbacks[i] != NULL) &&  
      (spsCallbacks[i]->connectEventCallback != NULL))
    {
      spsCallbacks[i]->connectEventCallback(connHandle);
    }
  }
}

/*---------------------------------------------------------------------------
* Notify all registered users
*-------------------------------------------------------------------------*/
static void disconnectEvtCallback(uint16 connHandle)
{
  uint8 i;
  for(i = 0; (i < cbSPS_MAX_CALLBACKS); i++)
  {
    if((spsCallbacks[i] != NULL) && 
      (spsCallbacks[i]->disconnectEventCallback != NULL))
    {
      spsCallbacks[i]->disconnectEventCallback(connHandle);
    }
  }
}

/*---------------------------------------------------------------------------
* Notify all registered users
*-------------------------------------------------------------------------*/
static void dataEvtCallback(uint16 connHandle, uint8 *pBuf, uint8 size)
{
  uint8 i;
  for(i = 0; (i < cbSPS_MAX_CALLBACKS); i++)
  {
    if((spsCallbacks[i] != NULL) &&  
       (spsCallbacks[i]->dataEventCallback != NULL))
    {
      spsCallbacks[i]->dataEventCallback(connHandle, pBuf, size);
    }
  }
}

/*---------------------------------------------------------------------------
* Notify all registered users
*-------------------------------------------------------------------------*/
static void dataCnfCallback(uint16 connHandle)
{
  uint8 i;
  for(i = 0; (i < cbSPS_MAX_CALLBACKS); i++)
  {
    if((spsCallbacks[i] != NULL) && 
      (spsCallbacks[i]->dataCnfCallback != NULL))
    {
      spsCallbacks[i]->dataCnfCallback(connHandle);
    }
  }
}


/*********************************************************************
*********************************************************************/
