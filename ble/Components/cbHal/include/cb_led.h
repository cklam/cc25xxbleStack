#ifndef _CB_LED_H_
#define _CB_LED_H_

/*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : Red, Green and Blue LED
 * File        : cb_led.h
 *
 * Description : LED functionalty for TL1
 *-------------------------------------------------------------------------*/

#include "comdef.h"

/*===========================================================================
 * DEFINES
 *=========================================================================*/

/*===========================================================================
 * TYPES
 *=========================================================================*/

typedef enum
{
    cbLED_GREEN = 0,
    cbLED_RED,
    cbLED_BLUE

} cbLED_Id;

/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/

/*---------------------------------------------------------------------------
 * Initialises LED. Must be called before any other LED function.
 *-------------------------------------------------------------------------*/
void cbLED_init(void);

/*---------------------------------------------------------------------------
 * Description
 *-------------------------------------------------------------------------*/
void cbLED_set(cbLED_Id led, bool on);

/*---------------------------------------------------------------------------
 * Description
 *-------------------------------------------------------------------------*/
void cbLED_flash(cbLED_Id led, uint16 count, uint16 onTime, uint16 offTime);

#endif 






