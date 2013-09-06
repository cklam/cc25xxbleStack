/*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : Temperature Service 
 * File        : cb_temperature_service.c
 *
 * Description : Implementation of Temperature service based on the simple 
 *               GATT service part of the SDK for CC2540.
 *               Note that for simplicity this service uses 16bit UUIDs. A
 *               real application must use 128bit UUIDs for all manufacturer
 *               specific services and characteristics.
 *
 *-------------------------------------------------------------------------*/
#include "bcomdef.h"
#include "OSAL.h"
#include "linkdb.h"
#include "att.h"
#include "gatt.h"
#include "gatt_uuid.h"
#include "gattservapp.h"
#include "gapbondmgr.h"
#include "cb_assert.h"
#include "cb_temperature_service.h"


/*===========================================================================
 * DEFINES
 *=========================================================================*/
#define SERVAPP_NUM_ATTR_SUPPORTED        4
#define UINT16_TO_BYTEARRAY(val) {LO_UINT16(val), HI_UINT16(val)}
#define ATTRIBUTE128(uuid, pProps, pValue) { {ATT_UUID_SIZE, uuid}, pProps, 0, (uint8*)pValue}
#define ATTRIBUTE16(uuid, pProps, pValue)  { {ATT_BT_UUID_SIZE, uuid}, pProps, 0, (uint8*)pValue}


/*===========================================================================
 * TYPES
 *=========================================================================*/


/*===========================================================================
 * DECLARATIONS
 *=========================================================================*/
static uint8 readAttrHandler(uint16 connHandle, gattAttribute_t *pAttr, uint8 *pValue, uint8 *pLen, uint16 offset, uint8 maxLen );
static bStatus_t writeAttrHandler(uint16 connHandle, gattAttribute_t *pAttr, uint8 *pValue, uint8 len, uint16 offset );
static void connStatusHandler(uint16 connHandle, uint8 changeType);


/*===========================================================================
 * DEFINITIONS
 *=========================================================================*/
// Filename used by cb_ASSERT macro
static const char *file = "cb_temperature_service.c";

// UUIDS 
CONST uint8 temperatureServUUID[ATT_BT_UUID_SIZE] = { LO_UINT16(cbTEMP_SERV_UUID), HI_UINT16(cbTEMP_SERV_UUID)};
CONST uint8 temperatureUUID[ATT_BT_UUID_SIZE] = { LO_UINT16(cbTEMP_TEMPERATURE_UUID), HI_UINT16(cbTEMP_TEMPERATURE_UUID)};

// Service attribute
CONST gattAttrType_t temperatureService = { ATT_BT_UUID_SIZE, temperatureServUUID };

// Temperature Characteristic
// Properties, value and configuration
static uint8 temperatureProps = GATT_PROP_READ | GATT_PROP_NOTIFY;
static uint8 temperatureValue[cbTEMP_TEMPERATURE_SIZE] = {0};
static gattCharCfg_t temperatureConfig[GATT_MAX_NUM_CONN];

// Attribute table
static gattAttribute_t tempAttrTbl[SERVAPP_NUM_ATTR_SUPPORTED] = 
{
  // Temperature Service
  ATTRIBUTE16( primaryServiceUUID, GATT_PERMIT_READ, &temperatureService),

  // Temperature Characteristic
  ATTRIBUTE16(characterUUID, GATT_PERMIT_READ, &temperatureProps),
  ATTRIBUTE16(temperatureUUID, GATT_PERMIT_READ , temperatureValue),
  ATTRIBUTE16(clientCharCfgUUID , GATT_PERMIT_READ | GATT_PERMIT_WRITE, &temperatureConfig)
};

// Service callbacks registered to GATT 
CONST gattServiceCBs_t temperatureCBs =
{
  readAttrHandler,  // Read callback
  writeAttrHandler, // Write callback
  NULL              // Authorization
};


/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/

/*---------------------------------------------------------------------------
 * Register temperature service to GATT
 *-------------------------------------------------------------------------*/
void cbTEMP_addService(void)
{
  uint8 status = SUCCESS;

  GATTServApp_InitCharCfg( INVALID_CONNHANDLE, temperatureConfig );
  linkDB_Register( connStatusHandler );  

  // Register GATT attribute list and callbacks with GATT Server App
  status = GATTServApp_RegisterService( tempAttrTbl, GATT_NUM_ATTRS( tempAttrTbl ), &temperatureCBs );
  cb_ASSERT(status == SUCCESS);
}

/*---------------------------------------------------------------------------
 * Set the current temperature.
 *-------------------------------------------------------------------------*/
void cbTEMP_setTemperature(int8 temperature)
{
  temperatureValue[0] = temperature;

  GATTServApp_ProcessCharCfg( temperatureConfig, (uint8 *)&temperatureValue,
    FALSE, tempAttrTbl, GATT_NUM_ATTRS( tempAttrTbl ),
    INVALID_TASK_ID );
}

/*===========================================================================
 * STATIC FUNCTIONS
 *=========================================================================*/

/*---------------------------------------------------------------------------
 * Read callback
 *-------------------------------------------------------------------------*/
static uint8 readAttrHandler( uint16 connHandle, gattAttribute_t *pAttr, 
                                    uint8 *pValue, uint8 *pLen, uint16 offset, uint8 maxLen )
{
  bStatus_t status = SUCCESS;
  uint16 uuid = BUILD_UINT16( pAttr->type.uuid[0], pAttr->type.uuid[1]);

  *pLen = 0;

  // If attribute permissions require authorization to read, return error
  if ( gattPermitAuthorRead( pAttr->permissions ) )
  {
    // Insufficient authorization
    return ( ATT_ERR_INSUFFICIENT_AUTHOR );
  }
  
  // Blob operations not allowed
  if (( offset > 0 ))
  {
    return ( ATT_ERR_ATTR_NOT_LONG );
  }
 
  if ( pAttr->type.len == ATT_BT_UUID_SIZE )
  {
    // 16-bit UUID    
    switch ( uuid )
    {
    case cbTEMP_TEMPERATURE_UUID:
      *pLen = cbTEMP_TEMPERATURE_SIZE;
      cb_ASSERT( *pLen <= maxLen);
      osal_memcpy(pValue, pAttr->pValue, *pLen);
      break;

    default:
      // Should never get here!
      status = ATT_ERR_ATTR_NOT_FOUND;
      break;
    }
  }
  else
  {
    // 128-bit UUID
    status = ATT_ERR_INVALID_HANDLE;
  }

  return ( status );
}

/*---------------------------------------------------------------------------
 * Read callback
 *-------------------------------------------------------------------------*/
static bStatus_t writeAttrHandler( uint16 connHandle, gattAttribute_t *pAttr,
                                 uint8 *pValue, uint8 len, uint16 offset )
{
  bStatus_t status = SUCCESS;
  uint16 uuid = BUILD_UINT16( pAttr->type.uuid[0], pAttr->type.uuid[1]);
  
  // If attribute permissions require authorization to write, return error
  if ( gattPermitAuthorWrite( pAttr->permissions ) )
  {
    // Insufficient authorization
    return ( ATT_ERR_INSUFFICIENT_AUTHOR );
  }

  // Blob operations not allowed
  if (( offset > 0 ))
  {
    return ( ATT_ERR_ATTR_NOT_LONG );
  }
  
  if ( pAttr->type.len == ATT_BT_UUID_SIZE )
  {
    switch (uuid)
    {
    case GATT_CLIENT_CHAR_CFG_UUID:
      status = GATTServApp_ProcessCCCWriteReq( connHandle, pAttr, pValue, len, offset, GATT_CLIENT_CFG_NOTIFY );
      break;   

    default:
      // Should never get here!
      status = ATT_ERR_ATTR_NOT_FOUND;
      break;
    }     
  }
  else
  {
    // 128-bit UUID
    status = ATT_ERR_INVALID_HANDLE;
  }

  return ( status );
}

/*---------------------------------------------------------------------------
 * Connection status callback
 *-------------------------------------------------------------------------*/
static void connStatusHandler( uint16 connHandle, uint8 changeType )
{ 
  // Make sure this is not loopback connection
  if ( connHandle != LOOPBACK_CONNHANDLE )
  {
    // Reset Client Char Config if connection has dropped
    if ( ( changeType == LINKDB_STATUS_UPDATE_REMOVED )      ||
         ( ( changeType == LINKDB_STATUS_UPDATE_STATEFLAGS ) && 
           ( !linkDB_Up( connHandle ) ) ) )
    { 
      GATTServApp_InitCharCfg( connHandle, temperatureConfig );
    }
  }
}