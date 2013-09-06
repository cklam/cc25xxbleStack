/*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : Timer
 * File        : cb_timer.c
 *
 * Description : Implements application timer class.
 *-------------------------------------------------------------------------*/

#include "comdef.h"
#include "osal.h"
#include "hal_board.h"

#include "cb_pio.h"
#include "cb_assert.h"

/*===========================================================================
 * DEFINES
 *=========================================================================*/

#define cbPIO_MAX_CALLBACKS (2)

/*===========================================================================
 * TYPES
 *=========================================================================*/

typedef struct
{
    uint8                   taskId;

    cbPIO_NotifyCallbIrq    callb[cbPIO_MAX_CALLBACKS];
    cbPIO_Port              port[cbPIO_MAX_CALLBACKS];
    cbPIO_Pin               pin[cbPIO_MAX_CALLBACKS];

} cbPIO_Class;

/*===========================================================================
 * DECLARATIONS
 *=========================================================================*/

static void cfgIrq(cbPIO_Port port, cbPIO_Pin pin);

static void setPx(cbPIO_Port port, cbPIO_Pin pin, cbPIO_Value value);

/*===========================================================================
 * DEFINITIONS
 *=========================================================================*/

static const char* file = "pio";

static cbPIO_Class pio;

static volatile uint8* const px[3]       = {(uint8*)0x7080, (uint8*)0x7090, (uint8*)0x70A0}; //{P0, P1, P2};
static volatile uint8* const pxSel[3]    = {(uint8*)0x70F3, (uint8*)0x70F4, (uint8*)0x70F5}; //{P0SEL, P1SEL, P2SEL};
static volatile uint8* const pxDir[3]    = {(uint8*)0x70FD, (uint8*)0x70FE, (uint8*)0x70FF}; //{P0DIR, P1DIR, P2DIR};
static volatile uint8* const pxInp[3]    = {(uint8*)0x708F, (uint8*)0x70F6, (uint8*)0x70F7}; //{P0INP, P1INP, P2INP};
static volatile uint8* const pxIfg[3]    = {(uint8*)0x7089, (uint8*)0x708A, (uint8*)0x708B}; //{P0IFG, P1IFG, P2IFG};
static volatile uint8* const pxIen[3]    = {(uint8*)0x70AB, (uint8*)0x708D, (uint8*)0x70AC}; //{P0IEN, P1IEN, P2IEN};

/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/
 
void cbPIO_init(void)
{
    uint8 i;

    for(i = 0; i < cbPIO_MAX_CALLBACKS; i++)
    {
        pio.callb[i] = NULL;
        pio.port[i] = cbPIO_PORT_INVALID;
        pio.pin[i] = 0xFF;
    }
}

void cbPIO_open(
    cbPIO_Port              port,
    cbPIO_Pin               pin,
    cbPIO_NotifyCallbIrq    notifyCallbFcn,
    cbPIO_Mode              mode,
    cbPIO_Value             value)
{
    uint8   i;
    bool    done = FALSE;
    uint8   selBit = pin;
    uint8   inpBit = pin;

    cb_ASSERT(port < cbPIO_PORT_INVALID);
    cb_ASSERT((port != cbPIO_PORT_2) || (pin < cbPIO_PIN_5));
    
    if(port == cbPIO_PORT_2)
    {
      switch(pin)
      {
      case cbPIO_PIN_0:
        selBit = 0x01;
        break;
        
      case cbPIO_PIN_3:
        selBit = 0x02;
        break;
        
      case cbPIO_PIN_4:
        selBit = 0x04;
        break;
        
      default:
        selBit = 0x00;
        break;
      }
    }
    else if(port == cbPIO_PORT_1)
    {
      inpBit &= 0xFC;
    }

    switch(mode)
    {
    case cbPIO_INPUT_PU:
        *(pxSel[port]) &= ~selBit;
        *(pxDir[port]) &= ~pin;
        *(pxInp[port]) &= ~inpBit;

        if(port == cbPIO_PORT_0)
        {
            *(pxInp[2]) &= ~(0x20);
        }
        else if(port == cbPIO_PORT_1)
        {
            *(pxInp[2]) &= ~(0x40);
        }
        else
        {
            *(pxInp[2]) &= ~(0x80);
        }
        break;

    case cbPIO_INPUT_PD:
        *(pxSel[port]) &= ~selBit;
        *(pxDir[port]) &= ~pin;
        *(pxInp[port]) &= ~inpBit;

        if(port == cbPIO_PORT_0)
        {
            *(pxInp[2]) |= 0x20;
        }
        else if(port == cbPIO_PORT_1)
        {
            *(pxInp[2]) |= 0x40;
        }
        else
        {
            *(pxInp[2]) |= 0x80;
        }
        break;

    case cbPIO_INPUT_FLOATING:
        *(pxSel[port]) &= ~selBit;
        *(pxDir[port]) &= ~pin;
        break;

    case cbPIO_OUTPUT:
        *(pxSel[port]) &= ~selBit;
        *(pxDir[port]) |= pin;
        
#if 0
        if(value == cbPIO_HIGH)
        {
          *(px[port]) |= pin;
        }
        else
        {
          *(px[port]) &= ~pin;
        }
#else   
        setPx(port, pin, value);
#endif
        break;

    case cbPIO_ALTERNATE:
        *(pxSel[port]) |= selBit;
        break;

    default:
        cb_EXIT(mode);
    }

    if( (notifyCallbFcn != NULL) &&
        ((mode == cbPIO_INPUT_FLOATING) ||
         (mode == cbPIO_INPUT_PU) ||
         (mode == cbPIO_INPUT_PD)))
    {
        for(i = 0; (i < cbPIO_MAX_CALLBACKS) && (done == FALSE); i++)
        {
            if(pio.callb[i] == NULL)
            {
                pio.callb[i] = notifyCallbFcn;
                pio.port[i] = port;
                pio.pin[i] = pin;

                done = TRUE;

                cfgIrq(port, pin);
            }
        }
    }
}

void cbPIO_close(
    cbPIO_Port port,
    cbPIO_Pin  pin)
{
    uint8   i;

    cb_ASSERT(port <= cbPIO_PORT_2);
    cb_ASSERT((port != cbPIO_PORT_2) || (pin < cbPIO_PIN_5));

    for(i = 0; i < cbPIO_MAX_CALLBACKS; i++)
    {
        if((port == pio.port[i]) && (pin == pio.pin[i]))
        {
            pio.callb[i] = NULL;
            pio.port[i] = cbPIO_PORT_INVALID;
            pio.pin[i] = cbPIO_PIN_INVALID;

            *(pxIen[port]) &= ~pin; //TBD
            *(pxIfg[port]) = ~pin;  //TBD
        }
    }

    //TBD default IO settings
}

void cbPIO_write(
    cbPIO_Port      port,
    cbPIO_Pin       pin,
    cbPIO_Value     value)
{
    cb_ASSERT(port <= cbPIO_PORT_2);
    cb_ASSERT((port != cbPIO_PORT_2) || (pin < cbPIO_PIN_5));

    setPx(port, pin, value);
}

cbPIO_Value cbPIO_read(
    cbPIO_Port port,
    cbPIO_Pin  pin)
{
#if 1
    cbPIO_Value val = cbPIO_LOW;

    cb_ASSERT(port <= cbPIO_PORT_2);
    cb_ASSERT((port != cbPIO_PORT_2) || (pin < cbPIO_PIN_5));

    if( (*(px[port]) & pin) != 0)
    {
        val = cbPIO_HIGH;
    }
#else
    cbPIO_Value val;
    cbPIO_Value val2;
    
    cb_ASSERT(port <= cbPIO_PORT_2);
    cb_ASSERT((port != cbPIO_PORT_2) || (pin < cbPIO_PIN_5));

    do
    {
        if( (*(px[port]) & pin) != 0)
        {
            val = cbPIO_HIGH;
        }
        else
        {
            val = cbPIO_LOW;
        }

        if( (*(px[port]) & pin) != 0)
        {
            val2 = cbPIO_HIGH;
        }
        else
        {
            val2 = cbPIO_LOW;
        }

    } while(val != val2);

#endif

    return val;
}


static void setPx(cbPIO_Port port, cbPIO_Pin pin, cbPIO_Value value)
{
    if(value == cbPIO_HIGH)
    {
        switch(port)
        {
        case cbPIO_PORT_0:
            P0 |= pin;
            break;

        case cbPIO_PORT_1:
            P1 |= pin;
            break;

        case cbPIO_PORT_2:
            P2 |= pin;
            break;
        }
    }
    else
    {
        switch(port)
        {
        case cbPIO_PORT_0:
            P0 &= ~pin;
            break;

        case cbPIO_PORT_1:
            P1 &= ~pin;
            break;

        case cbPIO_PORT_2:
            P2 &= ~pin;
            break;
        }
    } 
}

static void cfgEdgeTrig(cbPIO_Port port, cbPIO_Pin pin)
{
    uint8 val1;
    uint8 val2;
    
    uint8 bit = 0;

    switch(port)
    {
    case cbPIO_PORT_0:
        bit = 0x01;
        break;

    case cbPIO_PORT_1:
        if(pin < cbPIO_PIN_4)
        {
            bit = 0x02;
        }
        else
        {
            bit = 0x04;
        }
        break;

    case cbPIO_PORT_2:
        bit = 0x08;
        break;

    default:
        cb_EXIT(port);
        break;
    }
       
    do
    {
        val1 = (*(px[port])) & pin;
    
        if(val1 == 0)
        {
            PICTL &= ~bit;
        }
        else
        {
            PICTL |= bit;
        }
        
        val2 = (*(px[port])) & pin;
        
    } while(val1 != val2);
}

static void cfgIrq(cbPIO_Port port, cbPIO_Pin pin)
{
    *(pxIfg[port]) = ~pin; // Clear Interrupt
    *(pxIen[port]) |= pin; // Enable Interrupt in SFR register

    cfgEdgeTrig(port, pin);

    switch(port)
    {
    case cbPIO_PORT_0:
        IEN1 |= 0x20;
        break;

    case cbPIO_PORT_1:
        IEN2 |= 0x10;
        break;

    case cbPIO_PORT_2:
         IEN2 |= 0x02;
        break;

    default:
        cb_EXIT(port);
        break;
    }
    
    IEN0 |= 0x80;
}

static void handlePxIrq(cbPIO_Port port)
{
    uint8 i;
    uint8 ifg = 0;
    uint8 io;

    for(i = 0; i < cbPIO_MAX_CALLBACKS; i++)
    {
        ifg = *(pxIfg[port]);
        
        if( (pio.callb[i] != NULL) &&
            (pio.port[i] == port) &&
            ((pio.pin[i] & ifg) != 0))
        {
            io = *(px[port]);

            if((io & pio.pin[i]) != 0)
            {
                pio.callb[i](port, pio.pin[i], cbPIO_HIGH);
            }
            else
            {
                pio.callb[i](port, pio.pin[i], cbPIO_LOW);
            }
            
            *(pxIfg[port]) = ~(pio.pin[i]); // Clear interrupt
            
            cfgEdgeTrig(port, pio.pin[i]);
        }
    }
}

HAL_ISR_FUNCTION( pioP0Isr, P0INT_VECTOR )
{
    handlePxIrq(cbPIO_PORT_0);

    //IRCON = ~0x20;
    P0IF = 0;
}

HAL_ISR_FUNCTION( pioP1Isr, P1INT_VECTOR )
{
    handlePxIrq(cbPIO_PORT_1);
    
    IRCON2 &= ~0x08;
}

HAL_ISR_FUNCTION( pioP2Isr, P2INT_VECTOR )
{
    handlePxIrq(cbPIO_PORT_2);
    
    IRCON2 &= ~0x01;
}