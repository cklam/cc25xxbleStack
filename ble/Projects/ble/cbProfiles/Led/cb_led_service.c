/*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : LED Service 
 * File        : cb_led_service.c
 *
 * Description : Implementation of LED service based on the simple GATT
 *               service part of the SDK for CC2540.
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
#include "cb_led_service.h"

/*===========================================================================
 * DEFINES
 *=========================================================================*/
#define SERVAPP_NUM_ATTR_SUPPORTED        5

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
static bStatus_t writeAttrHandler(uint16 connHandle, gattAttribute_t *pAttr,uint8 *pValue, uint8 len, uint16 offset );


/*===========================================================================
 * DEFINITIONS
 *=========================================================================*/
CONST uint8 cbDEMO_servUUID[ATT_BT_UUID_SIZE] = { LO_UINT16(cbLEDS_SERV_UUID), HI_UINT16(cbLEDS_SERV_UUID)};
CONST uint8 cbDEMO_redLedUUID[ATT_BT_UUID_SIZE] = { LO_UINT16(cbLEDS_RED_LED_UUID), HI_UINT16(cbLEDS_RED_LED_UUID)};
CONST uint8 cbDEMO_greenLedUUID[ATT_BT_UUID_SIZE] = { LO_UINT16(cbLEDS_GREEN_LED_UUID), HI_UINT16(cbLEDS_GREEN_LED_UUID)};

// Filename used by cb_ASSERT macro
static const char *file = "cb_led_service.c";

static cbLEDS_LedSetEvent ledSetEventCallbacks = NULL;

// Service attribute
static CONST gattAttrType_t ledService = { ATT_BT_UUID_SIZE, cbDEMO_servUUID };

// Red LED Characteristic 
// Properties and value
static uint8 redLedProps = GATT_PROP_READ | GATT_PROP_WRITE;
static uint8 redLedValue[cbLEDS_RED_LED_SIZE];

// Green LED Characteristic 
// Properties and value
static uint8 greenLedProps = GATT_PROP_READ | GATT_PROP_WRITE;
static uint8 greenLedValue[cbLEDS_GREEN_LED_SIZE];

// Attribute table 
static gattAttribute_t tempAttrTbl[SERVAPP_NUM_ATTR_SUPPORTED] = 
{
  // LED Service Primary Service UUID
  ATTRIBUTE16( primaryServiceUUID, GATT_PERMIT_READ, &ledService),

  // Red LED
  ATTRIBUTE16(characterUUID, GATT_PERMIT_READ, &redLedProps),
  ATTRIBUTE16(cbDEMO_redLedUUID, GATT_PERMIT_READ | GATT_PERMIT_WRITE, redLedValue),

  // Green LED
  ATTRIBUTE16(characterUUID, GATT_PERMIT_READ, &greenLedProps),
  ATTRIBUTE16(cbDEMO_greenLedUUID, GATT_PERMIT_READ | GATT_PERMIT_WRITE, greenLedValue),
};

// Service callbacks registered to GATT 
CONST gattServiceCBs_t ledCBs =
{
  readAttrHandler,  // Read callback
  writeAttrHandler, // Write callback
  NULL              // Authorization callback
};


/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/

/*---------------------------------------------------------------------------
 * Register LED service to GATT
 * -callback: Callback used to notify write operations to to the 
 *            characteristics.
 *-------------------------------------------------------------------------*/
void cbLEDS_addService(cbLEDS_LedSetEvent callback)
{
  uint8 status = SUCCESS;

  cb_ASSERT(callback != NULL);
  ledSetEventCallbacks = callback;    

  // Register GATT attribute list and callbacks with GATT Server App
  status = GATTServApp_RegisterService( tempAttrTbl, GATT_NUM_ATTRS( tempAttrTbl ), &ledCBs );
  cb_ASSERT(status == SUCCESS);
}

/*---------------------------------------------------------------------------
 * Set the status of a LED. No notifications or indications are supported. 
 *-------------------------------------------------------------------------*/
extern void cbLEDS_setStatus(uint8 ledId, uint8 value)
{
  switch (ledId)
  {
  case cbLEDS_RED_LED_ID:
    redLedValue[0] = value;
    break;

  case cbLEDS_GREEN_LED_ID:
    greenLedValue[0] = value;
    break;

  default:
    cb_EXIT(ledId);
    break;
  }
}

/*===========================================================================
 * STATIC FUNCTIONS
 *=========================================================================*/

/*---------------------------------------------------------------------------
 * Read callback
 *-------------------------------------------------------------------------*/
static uint8 readAttrHandler( uint16 connHandle, gattAttribute_t *pAttr, uint8 *pValue, uint8 *pLen, uint16 offset, uint8 maxLen )
{
  bStatus_t status = SUCCESS;
  uint16 uuid = BUILD_UINT16( pAttr->type.uuid[0], pAttr->type.uuid[1]);

  *pLen = 0;

  cb_ASSERT(ledSetEventCallbacks != NULL);

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
    case cbLEDS_RED_LED_UUID:
      *pLen = cbLEDS_RED_LED_SIZE;
      cb_ASSERT( *pLen <= maxLen);
      osal_memcpy(pValue, pAttr->pValue, *pLen);      
      break;

    case cbLEDS_GREEN_LED_UUID:
      *pLen = cbLEDS_GREEN_LED_SIZE;
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
 * Write callback
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
    case cbLEDS_RED_LED_UUID:
      if (len == cbLEDS_RED_LED_SIZE)
      { 
        uint8 value = pValue[0];
        pAttr->pValue[0] = value;

        if (ledSetEventCallbacks != NULL)
        {
          ledSetEventCallbacks(cbLEDS_RED_LED_ID, value);
        }          
      }
      break;

    case cbLEDS_GREEN_LED_UUID:
      if (len == cbLEDS_GREEN_LED_SIZE)
      { 
        uint8 value = pValue[0];
        pAttr->pValue[0] = value;

        if (ledSetEventCallbacks != NULL)
        {
          ledSetEventCallbacks(cbLEDS_GREEN_LED_ID, value);
        }          
      }
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
