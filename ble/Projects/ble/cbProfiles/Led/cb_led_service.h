#ifndef CB_LED_SERVICE_H
#define CB_LED_SERVICE_H
/*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : LED Service
 * File        : cb_led_service.h
 *
 * Description : Declaration of LED service.
 *-------------------------------------------------------------------------*/
#include "hal_types.h"
#include "bcomdef.h"  

/*===========================================================================
 * DEFINES
 *=========================================================================*/
// Service UUID
#define cbLEDS_SERV_UUID                        (0xFFD0)

// Characteristics UUIDs
#define cbLEDS_RED_LED_UUID                     (0xFFD1)
#define cbLEDS_GREEN_LED_UUID                   (0xFFD2)

#define cbLEDS_RED_LED_SIZE                     (1)
#define cbLEDS_GREEN_LED_SIZE                   (1)

#define cbLEDS_RED_LED_ID                       (1)
#define cbLEDS_GREEN_LED_ID                     (2)

#define cbLEDS_LED_OFF                          (0)
#define cbLEDS_LED_ON                           (1)

/*===========================================================================
 * TYPES
 *=========================================================================*/
typedef void (*cbLEDS_LedSetEvent) (uint8 ledId, uint8 value);


/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/
extern void cbLEDS_addService(cbLEDS_LedSetEvent callback);
extern void cbLEDS_setStatus(uint8 ledId, uint8 value);

#endif

