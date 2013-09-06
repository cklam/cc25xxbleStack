 /*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : tracer
 * File        : cb_lis3dh.c
 *
 * Description : Driver for the LIS3DH accelerometer. The sensor
 *               interface is I2C.
 *               
 *               I2C Address: 0x19
 *
 *               This file implements configuration of the LIS3DH with 
 *               the following characterstics:
 *               Sample rate 10Hz
 *               Low power mode enabled
 *               Generate interrupt when device is moved.
 *               Wakeup and Click interrupt on P1.3
 *               
 *-------------------------------------------------------------------------*/

#include "hal_types.h"
#include "hal_board.h"
#include "hal_defs.h"
#include "osal.h"
#include "cb_swi2c_master.h"
#include "cb_lis3dh.h"
#include "cb_assert.h"
#include "cb_pio.h"


/*===========================================================================
 * DEFINES
 *=========================================================================*/
#define LIS3DH_ADDRESS          (0x19)

// Registers
#define LIS3DH_STATUS_REG_AUX   (0x07)
#define LIS3DH_OUT_ADC1_L       (0x08)
#define LIS3DH_OUT_ADC1_H       (0x09)
#define LIS3DH_OUT_ADC2_L       (0x0A)
#define LIS3DH_OUT_ADC2_H       (0x0B)
#define LIS3DH_OUT_ADC3_L       (0x0C)
#define LIS3DH_OUT_ADC3_H       (0x0D)
#define LIS3DH_INT_COUNTER_REG  (0x0E)
#define LIS3DH_WHO_AM_I         (0x0F)
#define LIS3DH_TEMP_CONF_REG    (0x1F)
#define LIS3DH_CTRL_REG1        (0x20)
#define LIS3DH_CTRL_REG2        (0x21)
#define LIS3DH_CTRL_REG3        (0x22)
#define LIS3DH_CTRL_REG4        (0x23)
#define LIS3DH_CTRL_REG5        (0x24)
#define LIS3DH_CTRL_REG6        (0x25)
#define LIS3DH_REFERENCE        (0x26)
#define LIS3DH_STATUS_REG       (0x27)
#define LIS3DH_OUT_X_L          (0x28)
#define LIS3DH_OUT_X_H          (0x29)
#define LIS3DH_OUT_Y_L          (0x2A)
#define LIS3DH_OUT_Y_H          (0x2B)
#define LIS3DH_OUT_Z_L          (0x2C)
#define LIS3DH_OUT_Z_H          (0x2D)
#define LIS3DH_FIFO_CTRL_REG    (0x2E)
#define LIS3DH_FIFO_SRC_REG     (0x2F)
#define LIS3DH_INT1_CFG         (0x30)
#define LIS3DH_INT1_SRC         (0x31)
#define LIS3DH_INT1_THS         (0x32)
#define LIS3DH_INT1_DURATION    (0x33)
#define LIS3DH_CLICK_CFG        (0x38)
#define LIS3DH_CLICK_SRC        (0x39)
#define LIS3DH_CLICK_THS        (0x3A)
#define LIS3DH_TIME_LIMIT       (0x3B)
#define LIS3DH_TIME_LATENCY     (0x3C)
#define LIS3DH_TIME_WINDOW      (0x3D)
#define LIS3DH_ACT_THS          (0x3E)
#define LIS3DH_INACT_DUR        (0x3F)

// Interrupt line is connected to P1.3
#define LIS3DH_INT1_PORT   P1
#define LIS3DH_INT1_BIT    BV(3)
#define LIS3DH_INT1_SEL    P1SEL
#define LIS3DH_INT1_DIR    P1DIR


#define LIS3DH_INT1_INTERRUPT_ENABLE        IEN2
#define LIS3DH_INT1_PORT_INTERRUPT_MASK     P1IEN
#define LIS3DH_INT1_INTERRUPT_CONTROL       P1CTL
#define LIS3DH_INT1_INTERRUPT_STATUS_FLAG   P1IFG

#define LIS3DH_INT1_IENBIT   BV(3) 
#define LIS3DH_INT1_ICTLBIT  BV(1) 
#define LIS3DH_PORT1_INT_IENBIT   BV(4) 

#define LIS3DH_INTERRUPT_EVT            (1 << 0)
//#define LIS3DH_WAKEUP_TIMEOUT_EVT       (1 << 1)

#define LIS3DH_CONFIG_REGISTER             (0)
#define LIS3DH_CONFIG_VALUE                (1)
#define LIS3DH_CONFIG_SIZE                 (sizeof(config)/2)      

#define CTRL_REG1_1HZ_LOW_POWER_ENABLED_XYZ_ENABLED (0x1F)
#define CTRL_REG1_10HZ_LOW_POWER_ENABLED_XYZ_ENABLED (0x2F)

#ifndef LIS3DH_DEFAULT_LIS3DH_CTRL_REG1
  #define LIS3DH_DEFAULT_LIS3DH_CTRL_REG1 CTRL_REG1_10HZ_LOW_POWER_ENABLED_XYZ_ENABLED
#endif

#ifndef LIS3DH_DEFAULT_LIS3DH_WAKEUP_THRESHOLD
  #define LIS3DH_DEFAULT_LIS3DH_WAKEUP_THRESHOLD (0x10)
#endif

/*===========================================================================
 * TYPES
 *=========================================================================*/


/*===========================================================================
 * DECLARATIONS
 *=========================================================================*/
static void _NOP(void){asm("NOP");}
static void write( uint8 reg, uint8 val);
static uint8 read(uint8 reg);
static void pioNotifyCallb(cbPIO_Port  port, cbPIO_Pin pin, cbPIO_Value value);

const uint8 config[][2] =
{
  {LIS3DH_CTRL_REG1, LIS3DH_DEFAULT_LIS3DH_CTRL_REG1},
  {LIS3DH_CTRL_REG2, 0xC1},     // High pass, auto reset on A01
  {LIS3DH_CTRL_REG3, 0xC0},     // AOI1 and Click Interrupt on INT1   
  {LIS3DH_CTRL_REG4, 0x00},     // Full Scale = 2g
  {LIS3DH_CTRL_REG5, 0x08},     // Interrupt latched 
  {LIS3DH_CTRL_REG6, 0x02},     // Interrupt active low   
  {LIS3DH_INT1_DURATION, 0x00}, // Duration = 0
  {LIS3DH_INT1_CFG, 0x2A},      // Enable X and Y and Z    
  {LIS3DH_INT1_THS,  LIS3DH_DEFAULT_LIS3DH_WAKEUP_THRESHOLD}      // Threshold
};

// Filename used by cb_ASSERT macro
static const char *file = "cb_lis3dh.c";

/*===========================================================================
 * DEFINITIONS
 *=========================================================================*/
typedef struct
{
  uint8 taskId;
  cbLIS_WakeUpEvent wakeUpHandler;
  cbLIS_ClickEvent clickHandler;
  bool             wakeupEnabled;
  uint8            wakeupThreshold;

} LIS3DH_class;

LIS3DH_class lis3dh;



/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void cbLIS_init(uint8 taskId)
{
  lis3dh.taskId = taskId;
  lis3dh.wakeupEnabled = FALSE;
  lis3dh.wakeupThreshold = LIS3DH_DEFAULT_LIS3DH_WAKEUP_THRESHOLD;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void cbLIS_register(cbLIS_WakeUpEvent wakeUpHandler,
                     cbLIS_ClickEvent clickHandler)
{
  lis3dh.wakeUpHandler = wakeUpHandler;
  lis3dh.clickHandler = clickHandler;
}

/*---------------------------------------------------------------------------
* Configure the the LIS3DH and enable the wakeup interrupt.
*-------------------------------------------------------------------------*/
bool cbLIS_open(void)
{
  bool sensorOk = TRUE;
  // Write all configuration registers, 
  for (uint8 i = 0; (i < LIS3DH_CONFIG_SIZE) && (sensorOk == TRUE); i++)
  {
    uint8 registerAddress = config[i][LIS3DH_CONFIG_REGISTER];
    uint8 value = config[i][LIS3DH_CONFIG_VALUE];
    uint8 readValue = 0;
        
    // Read registers to verify that correct value has been written
    write(registerAddress, value);
    readValue = read(registerAddress);
    if (value != readValue)
    {
      sensorOk = FALSE;
    }
  }

  if (sensorOk == TRUE)
  {      
    read(LIS3DH_INT1_SRC);
    read(LIS3DH_CLICK_SRC);  
  
    lis3dh.wakeupEnabled = TRUE;

    cbPIO_open(cbPIO_PORT_1, cbPIO_PIN_3, pioNotifyCallb, cbPIO_INPUT_PU, cbPIO_HIGH);
  }
  
  return sensorOk;  
}

/*---------------------------------------------------------------------------
* Watchdog operation that checks that the configuration of the 
* LIS3DH has not changed. This do not seems to happen but this
* operation was introduced just tp verify this.
*-------------------------------------------------------------------------*/
bool cbLIS_checkAndRestoreConfiguration(void)
{
  bool restorePerformed = FALSE;
  
  for (uint8 i = 0; i < LIS3DH_CONFIG_SIZE; i++)
  {
    uint8 registerAddress = config[i][LIS3DH_CONFIG_REGISTER];
    uint8 configuredValue = config[i][LIS3DH_CONFIG_VALUE];
    uint8 readValue = 0;

    readValue = read(registerAddress);

    if (registerAddress != LIS3DH_INT1_THS)
    {
      // Verify that the value written is still stored in LIS3DH
      // If not restore configuration
      if (readValue != configuredValue)
      {
        restorePerformed = true;

        write(registerAddress, configuredValue);
        readValue = read(registerAddress);
        cb_ASSERT(configuredValue == readValue);  
      }
    }
    else
    {
      // Verify that the value written is still stored in LIS3DH
      // If not restore configuration
      if (readValue != lis3dh.wakeupThreshold)
      {
        restorePerformed = true;

        write(registerAddress, lis3dh.wakeupThreshold);
        readValue = read(registerAddress);
        cb_ASSERT(lis3dh.wakeupThreshold == readValue);  
      }
    }      
  }

  read(LIS3DH_INT1_SRC);
  read(LIS3DH_CLICK_SRC);    
  
  lis3dh.wakeupEnabled = TRUE;

  // Clear any pending interrupts
  P1IFG = 0;
  P1IF = 0;

  // Enable interrupt on P1.3
  PICTL |= BV(1);                 // P1ICONL: Falling edge ints on pins 0-3.
  P1IEN |= BV(3);                 // Enable specific P1 bits for ints by bit mask.
  IEN2  |= BV(4);                 // Enable general P1 interrupts.  

  return restorePerformed;
}

/*---------------------------------------------------------------------------
* Configure the threshold for generation of wakeup interrupt
*-------------------------------------------------------------------------*/
void cbLIS_setWakeupTreshold(uint8 threshold)
{
  uint8 value;

  write(LIS3DH_INT1_THS,  threshold); // Threshold
  value = read(LIS3DH_INT1_THS);
  cb_ASSERT(value == threshold);
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void cbLIS_enableWakeupInterrupt(void)
{
  uint8 value;

  write(LIS3DH_INT1_CFG, 0x2A);  // Enable X and Y and Z 
  value = read(LIS3DH_INT1_CFG);
  cb_ASSERT(value == 0x2A);

  lis3dh.wakeupEnabled = TRUE;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void cbLIS_disableWakeupInterrupt(void)
{
  uint8 value;

  lis3dh.wakeupEnabled = FALSE;

  write(LIS3DH_INT1_CFG, 0x00);  // Disable X and Y and  Z  
  value = read(LIS3DH_INT1_CFG);
  cb_ASSERT(value == 0x00);
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
int8 cbLIS_readX(void)
{
  uint8 highByte;
  //uint8 lowByte;
 
  highByte = read(LIS3DH_OUT_X_H);
  //lowByte = read(LIS3DH_OUT_X_L);
  
  //return (int16)((uint16)lowByte + (((uint16)(highByte)) << 8));  
  return (int8)highByte;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
int8 cbLIS_readY(void)
{
  uint8 highByte;
  //uint8 lowByte;
 
  highByte = read(LIS3DH_OUT_Y_H);
  //lowByte = read(LIS3DH_OUT_Y_L);
  
  //return (int16)((uint16)lowByte + (((uint16)(highByte)) << 8));  
  return (int8)highByte;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
int8 cbLIS_readZ(void)
{
  uint8 highByte;
  //uint8 lowByte;
 
  highByte = read(LIS3DH_OUT_Z_H);
  //lowByte = read(LIS3DH_OUT_Z_L);
  
  //return (int16)((uint16)lowByte + (((uint16)(highByte)) << 8));  
  return (int8)highByte;
}

/*---------------------------------------------------------------------------
* Test function used for selftest
*-------------------------------------------------------------------------*/
bool cbLIS_selfTest(void)
{
  bool ok = FALSE;
  uint8 readValue;
    
  readValue = read(LIS3DH_WHO_AM_I);
  
  if (readValue == 0x33)
  {
    ok = TRUE;
  }  
  
  return ok;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
uint16 cbLIS_processEvent( uint8 taskId, uint16 events )
{
  VOID taskId; // OSAL required parameter that isn't used in this function

  if ( events & SYS_EVENT_MSG )
  {
    uint8 *pMsg;

    if ( (pMsg = osal_msg_receive(lis3dh.taskId)) != NULL )
    {
      VOID osal_msg_deallocate( pMsg );
    }
    
    return (events ^ SYS_EVENT_MSG);
  }

  if (events & LIS3DH_INTERRUPT_EVT)
  {     
    uint8 wakupSource = read(LIS3DH_INT1_SRC);
    uint8 clickSource = read(LIS3DH_CLICK_SRC);  
    
    if (clickSource != 0)
    {
      if (lis3dh.clickHandler != NULL)
      {
        lis3dh.clickHandler();
      }
    }
    
    if (wakupSource != 0)
    {
      if (lis3dh.wakeUpHandler != NULL)
      {
        lis3dh.wakeUpHandler();
      }      
    }
 
    return ( events ^ LIS3DH_INTERRUPT_EVT );
  }

  return 0;
}

/*---------------------------------------------------------------------------
* Write register in LIS3DH
*-------------------------------------------------------------------------*/
static void write( uint8 reg, uint8 val)
{
  cbSWI2C_start();                        
  cbSWI2C_txByte((LIS3DH_ADDRESS << 1)  & ~0x01); // [ADDR] + R/W bit = 0 Write operation

  cbSWI2C_txByte(reg);                     
  cbSWI2C_txByte(val);
  cbSWI2C_stop();
}

/*---------------------------------------------------------------------------
* Read register in LIS3DH
*-------------------------------------------------------------------------*/
static uint8 read(uint8 reg)
{
  uint8 val = 0;

  cbSWI2C_start();                        
  cbSWI2C_txByte((LIS3DH_ADDRESS << 1)  & ~0x01); // [ADDR] + R/W bit = 0 Write operation

  _NOP();                         
  _NOP();                          

  cbSWI2C_txByte(reg); // Setup register to read

  _NOP();                         
  _NOP();  

  cbSWI2C_start();     
  cbSWI2C_txByte((LIS3DH_ADDRESS << 1) | 0x01); // [ADDR] + R/W bit = 1 Read operation

  val = SWI2C_rxByte(FALSE);

  cbSWI2C_stop();  

  return val;    
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void pioNotifyCallb(
    cbPIO_Port  port,
    cbPIO_Pin   pin,
    cbPIO_Value value)
{
  cb_ASSERT((port == cbPIO_PORT_1) && (pin == cbPIO_PIN_3)); 
  
  osal_set_event(lis3dh.taskId, LIS3DH_INTERRUPT_EVT); 
}

