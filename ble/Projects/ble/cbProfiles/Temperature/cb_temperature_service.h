#ifndef CB_TEMPERATURE_SERVICE_H
#define CB_TEMPERATURE_SERVICE_H
/*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : Temperature service
 * File        : cb_temperature_service.h
 *
 * Description : Declaration of temperature service functionality.
 *-------------------------------------------------------------------------*/
#include "hal_types.h"
#include "bcomdef.h"  

/*===========================================================================
 * DEFINES
 *=========================================================================*/
// Service UUID
#define cbTEMP_SERV_UUID                        (0xFFE0)

// Characteristics UUIDs
#define cbTEMP_TEMPERATURE_UUID                 (0xFFE1) 
#define cbTEMP_TEMPERATURE_SIZE                 (1)

/*===========================================================================
 * TYPES
 *=========================================================================*/


/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/
extern void cbTEMP_addService(void);
extern void cbTEMP_setTemperature(int8 temperature);

#endif
