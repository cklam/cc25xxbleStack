/*---------------------------------------------------------------------------
* Copyright (c) 2000, 2001 connectBlue AB, Sweden.
* Any reproduction without written permission is prohibited by law.
*
* Component   : LED
* File        : cb_led.c
*
* Description : Functionality to enable/disable/flash the red and green
*               LEDs.
*-------------------------------------------------------------------------*/

#include "comdef.h"
#include "hal_types.h"
#include "hal_mcu.h"
#include "osal_cbtimer.h"

#include "cb_led.h"
#include "cb_assert.h"
#include "cb_pio.h"

/*===========================================================================
* DEFINES
*=========================================================================*/

#define cbLED_MAX_NBR_LEDS  (3)


#define cbLED_ON    (cbPIO_LOW)
#define cbLED_OFF   (cbPIO_HIGH)

/*===========================================================================
* TYPES
*=========================================================================*/
typedef struct  
{
    bool    on;

    uint8   count;
    uint16  onTime;
    uint16  offTime;
    bool    savedState;

    uint8  timerId;

} cbLED_Led;

typedef struct  
{
  cbLED_Led led[cbLED_MAX_NBR_LEDS];

}cbLED_Class;

/*===========================================================================
* DECLARATIONS
*=========================================================================*/
static void flashTimeout(uint8* id);

/*===========================================================================
* DEFINITIONS
*=========================================================================*/

// Filename used by cb_ASSERT macro
static const char *file = "led";

// LED pins definitions {Green, Red, Blue}
static const cbPIO_Port port[cbLED_MAX_NBR_LEDS] = {cbPIO_PORT_1, cbPIO_PORT_1, cbPIO_PORT_1};
static const cbPIO_Pin  pin[cbLED_MAX_NBR_LEDS] = {cbPIO_PIN_4, cbPIO_PIN_7, cbPIO_PIN_1};

static cbLED_Class leds;

/*===========================================================================
* FUNCTIONS
*=========================================================================*/

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void cbLED_init(void)
{
    for (int8 i = 0; i < cbLED_MAX_NBR_LEDS; i++)
    {
        leds.led[i].on = FALSE;
        
        leds.led[i].count = 0; 
        leds.led[i].onTime = 0;
        leds.led[i].offTime = 0;
        leds.led[i].savedState = FALSE;

        cbPIO_open(port[i], pin[i], NULL, cbPIO_OUTPUT, cbLED_OFF);
    }
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void cbLED_set(cbLED_Id id, bool on)
{
    cb_ASSERT(id < cbLED_MAX_NBR_LEDS);

    leds.led[id].on = on;

    if(on == TRUE)
    {
        cbPIO_write(port[id], pin[id], cbLED_ON);    
    }
    else
    {
        cbPIO_write(port[id], pin[id], cbLED_OFF);
    }
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void cbLED_flash(cbLED_Id id, uint16 count, uint16 onTime, uint16 offTime)
{
    uint8 res;

    cb_ASSERT(id < cbLED_MAX_NBR_LEDS);
    cb_ASSERT((count == 0) || (onTime > 0));
    cb_ASSERT((count == 0) || (offTime > 0));

    if( (count > 0) && (leds.led[id].count == 0))
    {
        // Start

        leds.led[id].savedState = leds.led[id].on;

        cbLED_set(id, TRUE);

        leds.led[id].count = count; 
        leds.led[id].onTime = onTime;
        leds.led[id].offTime = offTime;

        res = osal_CbTimerStart(flashTimeout, (uint8*)id, onTime, &(leds.led[id].timerId));
        cb_ASSERT(res == SUCCESS);
    }
    else if( (count == 0) && (leds.led[id].count > 0))
    {
        // Stop

        osal_CbTimerStop(leds.led[id].timerId);
        leds.led[id].timerId = INVALID_TIMER_ID;

        leds.led[id].count = 0; 
        leds.led[id].onTime = 0;
        leds.led[id].offTime = 0;
    }
}

/*===========================================================================
* STATIC FUNCTIONS
*=========================================================================*/

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void flashTimeout(uint8* idp)
{
    cbLED_Id    id;
    uint8       res;
    uint16      tmp = (uint16)idp;
    
    id = (cbLED_Id)tmp;

    cb_ASSERT(id < cbLED_MAX_NBR_LEDS);

    if(leds.led[id].count > 0)
    {
        if (leds.led[id].on == TRUE)
        {
            cbLED_set(id, FALSE);

            res = osal_CbTimerStart(flashTimeout, (uint8*)id, leds.led[id].offTime, &(leds.led[id].timerId));
            cb_ASSERT(res == SUCCESS);
        }
        else
        {
            cb_ASSERT(leds.led[id].count > 0);
            leds.led[id].count--;

            if(leds.led[id].count > 0)
            {
                cbLED_set(id, TRUE);

                res = osal_CbTimerStart(flashTimeout, (uint8*)id, leds.led[id].onTime, &(leds.led[id].timerId));
                cb_ASSERT(res == SUCCESS);
            }
            else
            {
                 cbLED_set(id, leds.led[id].savedState);
            }
        }
    }
}