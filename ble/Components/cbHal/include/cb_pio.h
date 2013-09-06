/*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : PIO
 * File        : cb_pio.h
 *
 * Description : Contains functionality for PIO. 
 *-------------------------------------------------------------------------*/
#ifndef _CB_PIO_H_
#define _CB_PIO_H_

#include "comdef.h"

/*===========================================================================
 * DEFINES
 *=========================================================================*/

// Pins
#define cbPIO_PIN_0             (0x01)
#define cbPIO_PIN_1             (0x02)
#define cbPIO_PIN_2             (0x04)
#define cbPIO_PIN_3             (0x08)
#define cbPIO_PIN_4             (0x10)
#define cbPIO_PIN_5             (0x20)
#define cbPIO_PIN_6             (0x40)
#define cbPIO_PIN_7             (0x80)
#define cbPIO_PIN_INVALID       (0xFF)

/*===========================================================================
 * TYPES
 *=========================================================================*/

typedef uint8 cbPIO_Pin;

typedef enum
{
    cbPIO_PORT_0 = 0,
    cbPIO_PORT_1,
    cbPIO_PORT_2,

    cbPIO_PORT_INVALID

} cbPIO_Port;

typedef enum
{
    cbPIO_LOW,
    cbPIO_HIGH

} cbPIO_Value;

typedef enum
{
    cbPIO_INPUT_PU,         // Input pull-up (default)
    cbPIO_INPUT_PD,         // Input pull-down
    cbPIO_INPUT_FLOATING,   // Floating  
    cbPIO_OUTPUT,
    cbPIO_ALTERNATE         // Used by other peripheral and not as GPIO.

} cbPIO_Mode;

typedef void (*cbPIO_NotifyCallbIrq)(
    cbPIO_Port  port,
    cbPIO_Pin   pin,
    cbPIO_Value value);

/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/

/*---------------------------------------------------------------------------
 * Initialises PIO. Must be called before any other PIO function.
 *-------------------------------------------------------------------------*/
void cbPIO_init(void);

/*---------------------------------------------------------------------------
 * Open a port for use as PIO. Assert if the port is already
 * in use.
 * - port: Identifies what port to use.
 * - pin: Identifies what pin of the port to use.
 * - notifyCallbFcn: For input pins, the registered function is called when
 *                   the value changes. Optional
 * - mode: Operational mode of the IO to open.
 * - value: If the mode is output, the value is set when the IO is opened.
 *-------------------------------------------------------------------------*/
void cbPIO_open(
    cbPIO_Port              port,
    cbPIO_Pin               pin,
    cbPIO_NotifyCallbIrq    notifyCallbIrq,
    cbPIO_Mode              mode,
    cbPIO_Value             value);

/*---------------------------------------------------------------------------
 * Close a port.
 * - port: Identifies what port to use.
 * - pin: Identifies what pin of the port to use.
 *-------------------------------------------------------------------------*/
void cbPIO_close(
    cbPIO_Port port,
    cbPIO_Pin  pin);

/*---------------------------------------------------------------------------
 * Write to a port with output mode. Assert if the port is in
 * input mode.
 * - port: Identifies what port to use.
 * - pin: Identifies what pin of the port to use.
 * - value: Value to write.
 *-------------------------------------------------------------------------*/
void cbPIO_write(
    cbPIO_Port      port,
    cbPIO_Pin       pin,
    cbPIO_Value     value);

/*---------------------------------------------------------------------------
 * Read from a port It works both with input and output mode.
 * - port: Identifies what port to use.
 * - pin: Identifies what pin of the port to use.
 * The read value is returned.
 *-------------------------------------------------------------------------*/
cbPIO_Value cbPIO_read(
    cbPIO_Port port,
    cbPIO_Pin  pin);

#endif /* _CB_PIO_H_ */

