 /*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : tracer
 * File        : cb_tmp112.c
 *
 * Description : Driver for the TMP112 temperature sensor. The sensor
 *               interface is I2C.
 *               
 *               I2C Address: 0x49
 *               Temperature in Celsius
 *               Temperature format: int8
 *
 *               The sensor is operating in shutdown mode. Oneshot 
 *               measurements are use to read the temperature.
 *-------------------------------------------------------------------------*/
#include "hal_types.h"
#include "cb_assert.h"
#include "hal_assert.h"
#include "osal.h"
#include "cb_tmp112.h"
#include "cb_swi2c_master.h"

/*===========================================================================
 * DEFINES
 *=========================================================================*/
#define cbTMP112_ADDRESS                          (0x49)
#define cbTMP112_CONVERSION_DELAY_IN_MS           (50)

#define cbTMP112_PERIODIC_TIMEOUT                 (1 << 4)
#define cbTMP112_CONVERSION_TIMEOUT               (1 << 5)


/*===========================================================================
 * TYPES
 *=========================================================================*/
typedef enum
{
  TMP112_S_NOT_INITIALIZED = 0,
  TMP112_S_IDLE,
  TMP112_S_WAIT,
  TMP112_S_WAIT_CONVERSION,

} cbTMP112_State;

typedef struct  
{
  uint8              taskId;
  cbTMP112_State     state;
  uint32             periodInMs;

  bool               notifySingle;
  bool               notifyPeriodic;

  cbTMP122_TemperatureReadEvent singleConvCallback;
  cbTMP122_TemperatureReadEvent periodicConvCallback;

} cbTMP112_Class;

/*===========================================================================
 * DECLARATIONS
 *=========================================================================*/
static void _NOP(void){asm("NOP");}
static void write(uint8 reg, uint8 byte1, uint8 byte2);
static void read(uint8 reg, uint8 *pByte1, uint8 *pByte2);

static void startOneShotConversion(void);
static bool isOneShotConversionComplete(void);
static void readTemperature(int8 *pTemperature, uint16 *pTemperatureRaw);

/*===========================================================================
 * DEFINITIONS
 *=========================================================================*/
#define cbTMP112_TEMPERATURE_REGISTER     0x00
#define cbTMP112_CONFIGURATION_REGISTER   0x01

cbTMP112_Class tmp112;

// Filename used by cb_ASSERT macro
static const char *file = "cb_tmp112.c";

/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void cbTMP112_init(uint8 taskId)
{      
  tmp112.taskId = taskId;
  tmp112.state = TMP112_S_IDLE;
  tmp112.periodInMs = 0;
  tmp112.notifySingle = FALSE;
  tmp112.notifyPeriodic = FALSE;

  tmp112.periodicConvCallback = NULL;
  tmp112.singleConvCallback = NULL;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
bool cbTMP112_open(void)
{
  uint8 byte1, byte2;
  bool sensorOk = TRUE;
    
  // Write configuration register
  // Byte1: 0110 0001 Low power mode enabled, interrupt active low
  // Byte2: 0000 0000 change conversion rate to 0,25Hz instead of 4(default)    
  write(cbTMP112_CONFIGURATION_REGISTER, 0x61, 0x00);
  read(cbTMP112_CONFIGURATION_REGISTER, &byte1, &byte2);  
  
  if ((byte1 & 0x7F) == 0x61)
  {
    sensorOk = TRUE;
  }
  else
  {
    sensorOk = FALSE; 
  }
  
  return sensorOk;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
uint16 cbTMP112_processEvent(uint8 task_id, uint16 events)
{
  bool    convComplete      = FALSE;
  uint16  rawTemperature    = 0;
  int8    temperature       = 0;
  if ( events & SYS_EVENT_MSG )
  {
    uint8 *pMsg;

    if ( (pMsg = osal_msg_receive(tmp112.taskId)) != NULL )
    {
      // Release the OSAL message
      VOID osal_msg_deallocate( pMsg );
    }

    return (events ^ SYS_EVENT_MSG);
  }

  if ( events & cbTMP112_PERIODIC_TIMEOUT)
  {
    switch (tmp112.state)
    {
    case TMP112_S_WAIT:
      tmp112.notifyPeriodic = TRUE;
      tmp112.state = TMP112_S_WAIT_CONVERSION;
      startOneShotConversion();
      break;

    case TMP112_S_WAIT_CONVERSION:
      tmp112.notifyPeriodic = TRUE;
      break;

    default:
      HAL_ASSERT_FORCED();
      break;
    }
    return ( events ^ cbTMP112_PERIODIC_TIMEOUT );
  }

  if ( events & cbTMP112_CONVERSION_TIMEOUT)
  {
    switch (tmp112.state)
    {
    case TMP112_S_WAIT_CONVERSION:
      
      convComplete = isOneShotConversionComplete();        
      cb_ASSERT(convComplete == TRUE);

      readTemperature(&temperature, &rawTemperature);

      if (tmp112.notifyPeriodic == TRUE)
      {
        cb_ASSERT(tmp112.periodicConvCallback != NULL);
        tmp112.periodicConvCallback(temperature, rawTemperature);
        tmp112.notifyPeriodic = FALSE;
      }

      if (tmp112.notifySingle== TRUE)
      {
        cb_ASSERT(tmp112.singleConvCallback != NULL);
        tmp112.singleConvCallback(temperature, rawTemperature);
        tmp112.notifySingle = FALSE;
      }
      
      if (tmp112.periodInMs == 0)
      {
        tmp112.state = TMP112_S_IDLE;
      }
      else
      {
        tmp112.state = TMP112_S_WAIT;
      }      
      break;

    default:
      HAL_ASSERT_FORCED();
      break;
    }
    return ( events ^ cbTMP112_CONVERSION_TIMEOUT );
  }

  return 0;
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void cbTMP112_startPeriodic(uint16 periodInSeconds, cbTMP122_TemperatureReadEvent callback)
{
  uint8 res;  

  cb_ASSERT(periodInSeconds != 0);
  cb_ASSERT(callback != NULL);

  tmp112.periodicConvCallback = callback;
  tmp112.periodInMs = (uint32)periodInSeconds * 1000;
  tmp112.notifyPeriodic = TRUE;

  // Start periodic timer
  res = osal_start_reload_timer(tmp112.taskId, cbTMP112_PERIODIC_TIMEOUT, tmp112.periodInMs);
  cb_ASSERT(res == SUCCESS); 

  switch (tmp112.state)
  {
  case TMP112_S_IDLE:
  case TMP112_S_WAIT:
    tmp112.state = TMP112_S_WAIT_CONVERSION;
    startOneShotConversion();
    break;

  case TMP112_S_WAIT_CONVERSION:
    // Ignore - notify periodic flag has been set, periodic timer will be restarted
    break;

  default:
    HAL_ASSERT_FORCED();
    break;
  }
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void cbTMP112_stopPeriodic(void)
{
  tmp112.periodInMs = 0;
  tmp112.notifyPeriodic = FALSE;

  switch (tmp112.state)
  {
  case TMP112_S_WAIT:
    osal_stop_timerEx(tmp112.taskId, cbTMP112_PERIODIC_TIMEOUT);
    break;

  case TMP112_S_IDLE:
  case TMP112_S_WAIT_CONVERSION:    
    //Ignore
    break;

  default:
    cb_EXIT(0);
    break;
  }
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
void cbTMP112_readSingle(cbTMP122_TemperatureReadEvent callback)
{
  cb_ASSERT(callback != NULL);

  tmp112.singleConvCallback = callback;
  tmp112.notifySingle = TRUE;

  switch (tmp112.state)
  {
  case TMP112_S_IDLE:
  case TMP112_S_WAIT:
    tmp112.state = TMP112_S_WAIT_CONVERSION;
    startOneShotConversion();
    break;

  case TMP112_S_WAIT_CONVERSION:
    // Ignore - notify single flag has been set
    break;

  default:
    HAL_ASSERT_FORCED();
    break;
  }
}

/*---------------------------------------------------------------------------
* Write and readback configuration register. Returns true if the read
* returns the same written value. Do not call this when the temperatures
* are actually sampled.
*-------------------------------------------------------------------------*/
bool cbTMP112_selfTest(void)
{
  uint8 byte1 = 0x5A;
  uint8 byte2 = 0xA5;
  
  write(cbTMP112_CONFIGURATION_REGISTER, 0x61, 0x00);  
  read(cbTMP112_CONFIGURATION_REGISTER, &byte1, &byte2);
  
  // For byte 1 ignore Oneshot / Conversion Ready bit
  // For byte 2 ignore AL bit
  if (((byte1 & 0x7F) == 0x61) && ((byte2 & 0xDF) == 0))
  {
    return TRUE;    
  }
  else
  {
    return FALSE;
  }
}


/*===========================================================================
 * STATIC FUNCTIONS
 *=========================================================================*/

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void startOneShotConversion(void)
{
  uint8 res;

  // Write configuration register
  // Byte1: 1110 0001 Low power mode enable, start one shot conversion
  // Byte2: 0000 0000 change conversion rate to 0,25Hz instead of 4(default)    
  write(cbTMP112_CONFIGURATION_REGISTER, 0xE1, 0x00);

  // Start conversion delay timer
  res = osal_start_timerEx(tmp112.taskId, cbTMP112_CONVERSION_TIMEOUT, cbTMP112_CONVERSION_DELAY_IN_MS);
  cb_ASSERT(res == SUCCESS);
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static bool isOneShotConversionComplete(void)
{
  uint8 byte1, byte2;
  read(cbTMP112_CONFIGURATION_REGISTER, &byte1, &byte2);

  if ((byte1 & 0x80) == 0x80)
  {
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

/*---------------------------------------------------------------------------
* Description of function. Optional verbose description.
*-------------------------------------------------------------------------*/
static void readTemperature(int8 *pTemperature, uint16 *pTemperatureRaw)
{
  uint8 byte1, byte2;

  read(cbTMP112_TEMPERATURE_REGISTER, &byte1, &byte2);
  
  // TESTCODE:
  // -25.0
//  byte1 = 0xE7;
//  byte2 = 0x00;

  // -0,25
//  byte1 = 0xFF;
//  byte2 = 0xC0;

  *pTemperatureRaw = (uint16)((uint16)byte1 << 8 ) + (uint16)byte2;
  *pTemperature = byte1;
}


/*---------------------------------------------------------------------------
 * Description of function. Optional verbose description.
 *-------------------------------------------------------------------------*/
static void write(uint8 reg, uint8 byte1, uint8 byte2)
{
  cbSWI2C_start();                        

  cbSWI2C_txByte((cbTMP112_ADDRESS << 1)  & ~0x01); // [ADDR] + R/W bit = 0 Write operation

  _NOP();                         
  _NOP();                          

  cbSWI2C_txByte(reg);                           

  _NOP();                         
  _NOP();                          
  
  cbSWI2C_txByte(byte1);
  
  _NOP();                         
  _NOP();                          

  cbSWI2C_txByte(byte2);

  cbSWI2C_stop(); 
}

/*---------------------------------------------------------------------------
 * Description of function. Optional verbose description.
 *-------------------------------------------------------------------------*/
static void read(uint8 reg, uint8 *pByte1, uint8 *pByte2)
{
  cbSWI2C_start();                        
  cbSWI2C_txByte((cbTMP112_ADDRESS << 1)  & ~0x01); // [ADDR] + R/W bit = 0 Write operation

  // Write pointer register in TMP112
  cbSWI2C_txByte(reg);                           
  cbSWI2C_stop();

  _NOP();                         
  _NOP();  

  cbSWI2C_start();
  cbSWI2C_txByte((cbTMP112_ADDRESS << 1) | 0x01); // [ADDR] + R/W bit = 1 Read operation

  // Read temperature register (2 bytes)
  *pByte1 = SWI2C_rxByte(TRUE);
  *pByte2 = SWI2C_rxByte(TRUE);

  cbSWI2C_stop();  
}

