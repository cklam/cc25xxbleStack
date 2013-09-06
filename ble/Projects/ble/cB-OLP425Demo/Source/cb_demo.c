/*---------------------------------------------------------------------------
* Copyright (c) 2000, 2001 connectBlue AB, Sweden.
* Any reproduction without written permission is prohibited by law.
*
* Component   : Demo application for cB-0950 PCB
* File        : cb_demo.c
*
* Description : Application based on the keyfob demo from TI. Functionality
*               have been changed to fit the cB-0950 PCB.
*               Sensors:
*               - LIS3DH Accelerometer
*               - TMP112 Temperature sensor
*               Services:
*               - Device Info (From TI samples)
*               - Battery (From TI samples)
*               - Accelerometer (From TI samples)
*               - Temperature (connectBlue sample service for reading temperature)
*               - Led (connectBlue sample service for controlling LEDs)
*               - Serial Service (connectBlue sample service for serial data)
* 
*-------------------------------------------------------------------------*/
#include "bcomdef.h"
#include "OSAL.h"
#include "OSAL_PwrMgr.h"
#include "OnBoard.h"
#include "hal_adc.h"
#include "hal_led.h"
#include "hal_key.h"
#include "hal_uart.h"

#include "gatt.h"
#include "hci.h"

#include "gapgattserver.h"
#include "gattservapp.h"
#include "linkdb.h"

#if defined ( PLUS_BROADCASTER )
#include "peripheralBroadcaster.h"
#else
#include "peripheral.h"
#endif

#include "cb_demo.h"
#include "cb_assert.h"
#include "cb_assert_handler.h"
#include "cb_swi2c_master.h"
#include "cb_tmp112.h"
#include "cb_lis3dh.h"
#include "cb_led.h"
#include "cb_ble_serial.h"
#include "cb_log.h"

// Services
#include "gapbondmgr.h"
#include "devinfoservice.h"
#include "accelerometer.h"
#include "battservice.h"
#include "cb_temperature_service.h"
#include "cb_led_service.h"
#include "cb_serial_service.h"


// Filename used by cb_ASSERT macro
static const char* file = "cb_demo.c";

/*===========================================================================
* DEFINES
*=========================================================================*/

// Delay between power-up and starting advertising (in ms)
#define STARTDELAY                    500

// How often (in ms) to read the accelerometer
#define ACCEL_READ_PERIOD             100

// Minimum change in accelerometer before sending a notification
#define ACCEL_CHANGE_THRESHOLD        5

//GAP Peripheral Role desired connection parameters

// Whether to enable automatic parameter update request when a connection is formed
#define DEFAULT_ENABLE_UPDATE_REQUEST         FALSE

// Use limited discoverable mode to advertise for 30.72s, and then stop, or 
// use general discoverable mode to advertise indefinitely 
#define DEFAULT_DISCOVERABLE_MODE             GAP_ADTYPE_FLAGS_LIMITED
//#define DEFAULT_DISCOVERABLE_MODE             GAP_ADTYPE_FLAGS_GENERAL

// Minimum connection interval (units of 1.25ms, 80=100ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL     16

// Maximum connection interval (units of 1.25ms, 800=1000ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL     32

// Slave latency to use if automatic parameter update request is enabled
#define DEFAULT_DESIRED_SLAVE_LATENCY         0

// Supervision timeout value (units of 10ms, 1000=10s) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_CONN_TIMEOUT          300


// Company Identifier: Texas Instruments Inc. (13)
#define TI_COMPANY_ID                              0x000D


/*===========================================================================
* TYPES
*=========================================================================*/
typedef struct 
{
  uint8             taskId;
  gaprole_States_t  gapProfileState;
  int8              currentTemperature;
  uint8             batteryLevel;
  bool              waitWrite;
  uint8             txCount;
  bool              tempSensorOk;
  bool              accelerometerOk;
} cbDEMO_Class;

/*===========================================================================
* DECLARATIONS
*=========================================================================*/

static void gapApplicationInit(void);
static void gapSetAlwaysAdvertising(void);
static void processOSALMsg( osal_event_hdr_t *pMsg );
static void peripheralStateNotificationCB( gaprole_States_t newState );
static void passcodeCB(uint8 *deviceAddr, uint16 connectionHandle, uint8 uiInputs, uint8 uiOutputs);
static void pairStateCB( uint16 connHandle, uint8 state, uint8 status );
static void accelEnablerChangeCB( void );
static void accelRead( void );
static void updateNameWithAddressInfo(void);
static void checkErrorCode(void);

// Callbacks from services

// LED Service
static void ledSetEvent(uint8 ledId, uint8 value);

// BLE Serial Service
static void blsDataAvailableEvent(uint8 port);
static void blsWriteCompleteEvent(uint8 port, uint16 nBytes);
static void blsErrorEvent(uint8 port, uint8 error);



// Callbacks from drivers

// Accelerometer
static void wakeUpEvent(void);
static void clickEvent(void);

// Temperature sensor
static void temperatureReadEvent(int8 temperature, uint16 raw);


/*===========================================================================
* DEFINITIONS
*=========================================================================*/

static cbDEMO_Class demo;

// GAP - SCAN RSP data (max size = 31 bytes)
static uint8 deviceName[] =
{
  // complete name  
  0x0C,   // length of first data structure 
  0x09,   // AD Type = Complete local name  
  'O',
  'L',
  'P',
  '4',
  '2',
  '5',
  '-',
  '0',    // Will be replaced with number from the device address
  '0',    // Will be replaced with number from the device address
  '0',    // Will be replaced with number from the device address
  '0',    // Will be replaced with number from the device address
};

// GAP - Advertisement data (max size = 31 bytes, though this is
// best kept short to conserve power while advertising)
static uint8 advertData[] = 
{ 
  0x02,   // length of first data structure (2 bytes excluding length byte)
  GAP_ADTYPE_FLAGS,   // AD Type = Flags
  DEFAULT_DISCOVERABLE_MODE | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
};

// GAP GATT Attributes
static uint8 attDeviceName[GAP_DEVICE_NAME_LEN] = "OLP425-0000";


// GAP Role callbacks
static gapRolesCBs_t peripheralRoleCallbacks =
{
  peripheralStateNotificationCB,  // Profile State Change Callbacks
  NULL                            // When a valid RSSI is read from controller
};

// GAP Bond Manager callbacks
static gapBondCBs_t bondMgrCallbacks =
{
  passcodeCB,                     // Passcode callback
  pairStateCB                     // Pairing / Bonding state Callback
};

// Accelerometer Profile callbacks
static accelCBs_t accelServiceCallbacks =
{
  accelEnablerChangeCB,          // Called when Enabler attribute changes
};

// Serial Port Service callbacks
static cbBLS_Callbacks blsCallbacks = {
  blsDataAvailableEvent,
  blsWriteCompleteEvent,
  blsErrorEvent
};

/*===========================================================================
* FUNCTIONS
*=========================================================================*/

/*---------------------------------------------------------------------------
* Initialization function for the Demo application task.
* This is called during initialization and should contain
* any application specific initialization.
* -task_id: The ID assigned by OSAL.  This ID should be
*            used to send messages and set timers.
*-------------------------------------------------------------------------*/
void cbDEMO_init( uint8 taskId )
{   
  demo.taskId = taskId;
  demo.gapProfileState = GAPROLE_INIT;
  demo.currentTemperature  = 0;
  demo.batteryLevel = 100;
  demo.waitWrite = FALSE;
  demo.tempSensorOk = FALSE;
  demo.accelerometerOk = FALSE;
  
  gapApplicationInit();

  // Initialize GATT attributes
  GGS_AddService( GATT_ALL_SERVICES );          // GAP
  GATTServApp_AddService( GATT_ALL_SERVICES );  // GATT attributes
  DevInfo_AddService();                         // Device Information Service
  Batt_AddService();                            // Battery Service 
  Accel_AddService(GATT_ALL_SERVICES);          // Accelerometer Profile
  cbTEMP_addService();                          // Temperature Service   
  cbLEDS_addService(ledSetEvent);               // Led Service  
  cbSPS_addService();                           // Serial Port Service

  // Setup a delayed profile startup
  osal_start_timerEx(demo.taskId, cbDEMO_START_DEVICE_EVT, STARTDELAY);
}


/*---------------------------------------------------------------------------
* Initialization of GAP properties. 
*-------------------------------------------------------------------------*/
static void gapApplicationInit(void)
{
  // For the CC2540DK-MINI keyfob, device doesn't start advertising until button is pressed
  uint8 initial_advertising_enable = TRUE;
  // By setting this to zero, the device will go into the waiting state after
  // being discoverable for 30.72 second, and will not being advertising again
  // until the enabler is set back to TRUE
  uint16 gapRole_AdvertOffTime = 0;

  uint8 enable_update_request = DEFAULT_ENABLE_UPDATE_REQUEST;
  uint16 desired_min_interval = DEFAULT_DESIRED_MIN_CONN_INTERVAL;
  uint16 desired_max_interval = DEFAULT_DESIRED_MAX_CONN_INTERVAL;
  uint16 desired_slave_latency = DEFAULT_DESIRED_SLAVE_LATENCY;
  uint16 desired_conn_timeout = DEFAULT_DESIRED_CONN_TIMEOUT;

  // Set the GAP Role Parameters
  GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &initial_advertising_enable );
  GAPRole_SetParameter( GAPROLE_ADVERT_OFF_TIME, sizeof( uint16 ), &gapRole_AdvertOffTime );

  GAPRole_SetParameter( GAPROLE_SCAN_RSP_DATA, sizeof ( deviceName ), deviceName );
  GAPRole_SetParameter( GAPROLE_ADVERT_DATA, sizeof( advertData ), advertData );

  GAPRole_SetParameter( GAPROLE_PARAM_UPDATE_ENABLE, sizeof( uint8 ), &enable_update_request );
  GAPRole_SetParameter( GAPROLE_MIN_CONN_INTERVAL, sizeof( uint16 ), &desired_min_interval );
  GAPRole_SetParameter( GAPROLE_MAX_CONN_INTERVAL, sizeof( uint16 ), &desired_max_interval );
  GAPRole_SetParameter( GAPROLE_SLAVE_LATENCY, sizeof( uint16 ), &desired_slave_latency );
  GAPRole_SetParameter( GAPROLE_TIMEOUT_MULTIPLIER, sizeof( uint16 ), &desired_conn_timeout );

  // Set the GAP Attributes
  GGS_SetParameter( GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, attDeviceName );

  // Setup the GAP Bond Manager
  {
    uint8 pairMode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
    uint8 mitm = TRUE;
    uint8 ioCap = GAPBOND_IO_CAP_DISPLAY_ONLY;
    uint8 bonding = TRUE;
    GAPBondMgr_SetParameter( GAPBOND_PAIRING_MODE, sizeof ( uint8 ), &pairMode );
    GAPBondMgr_SetParameter( GAPBOND_MITM_PROTECTION, sizeof ( uint8 ), &mitm );
    GAPBondMgr_SetParameter( GAPBOND_IO_CAPABILITIES, sizeof ( uint8 ), &ioCap );
    GAPBondMgr_SetParameter( GAPBOND_BONDING_ENABLED, sizeof ( uint8 ), &bonding );
  }  
  
  // Set max output power and reciever gain
  HCI_EXT_SetTxPowerCmd(HCI_EXT_TX_POWER_4_DBM);
  HCI_EXT_SetRxGainCmd(HCI_EXT_RX_GAIN_HIGH);
}

/*---------------------------------------------------------------------------
* Configure the device to be always advertising.
* Enable general advertising, one second advertising interval.
*-------------------------------------------------------------------------*/
void gapSetAlwaysAdvertising(void)
{
  uint8 advertising_enable = TRUE;
  uint16 desired_min_advertising_interval = 1600;
  uint16 desired_max_advertising_interval = 2000;    
  
  uint8 advertData[] = 
  { 
    0x02,   // length of first data structure (2 bytes excluding length byte)
    GAP_ADTYPE_FLAGS,   // AD Type = Flags
    GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED
  };

  GAP_SetParamValue( TGAP_GEN_DISC_ADV_INT_MIN, desired_min_advertising_interval);
  GAP_SetParamValue( TGAP_GEN_DISC_ADV_INT_MAX, desired_max_advertising_interval);
  
  GAPRole_SetParameter( GAPROLE_ADVERT_DATA, sizeof( advertData ), advertData );
  
  GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &advertising_enable );
}

#ifdef LOGGING
/*---------------------------------------------------------------------------
* Uart callback function. Since only write is used in for the logging 
* functionality only the tx empty event is handled. 
* -task_id: The ID assigned by OSAL.  This ID should be
*            used to send messages and set timers.
*-------------------------------------------------------------------------*/
void halUartCallback(uint8 port, uint8 event)
{
  switch (event)
  {
  case HAL_UART_RX_FULL:
  case HAL_UART_RX_ABOUT_FULL:
  case HAL_UART_RX_TIMEOUT:
  case HAL_UART_TX_FULL:
    break;

  case HAL_UART_TX_EMPTY:
    osal_pwrmgr_task_state(demo.taskId, PWRMGR_CONSERVE );
    break;

  default:
    break;    
  }
}

/*---------------------------------------------------------------------------
* Description 
* -parameter: 
*-------------------------------------------------------------------------*/
void printHandler(const char *pMsg)
{
  HalUARTWrite(HAL_UART_PORT_0, (uint8*)pMsg, osal_strlen((char*)pMsg));

  // Do not empty power save until write is complete
  osal_pwrmgr_task_state(demo.taskId, PWRMGR_HOLD );  
}

/*---------------------------------------------------------------------------
* Description 
* -parameter: 
*-------------------------------------------------------------------------*/
void initLogging(void)
{
  int8 res;
  halUARTCfg_t uartConfig;

  uartConfig.callBackFunc = halUartCallback;
  uartConfig.baudRate = HAL_UART_BR_57600;
  uartConfig.flowControl = FALSE;
  uartConfig.configured = TRUE;
  uartConfig.flowControlThreshold = 48;
  uartConfig.idleTimeout = 6;
  uartConfig.intEnable = TRUE;
  //uartConfig.rx = NULL;
  uartConfig.rxChRvdTime = 6;
  //uartConfig.tx = NULL;     

  res = HalUARTOpen(HAL_UART_PORT_0, &uartConfig);
  cb_ASSERT(res == HAL_UART_SUCCESS);

  cbLOG_registerPrintHandler(printHandler);   
}
#endif

/*---------------------------------------------------------------------------
* Passcode callback, will be called during pairing. 
* The passcode is set to "0".
*-------------------------------------------------------------------------*/
static void passcodeCB( uint8 *deviceAddr, uint16 connectionHandle, uint8 uiInputs, uint8 uiOutputs )
{
  cbLOG_PRINT("Passcode response\r\n");      
  GAPBondMgr_PasscodeRsp( connectionHandle, SUCCESS, 0 );
}

/*---------------------------------------------------------------------------
* Callback indicating the current state of pairing.
*-------------------------------------------------------------------------*/
static void pairStateCB( uint16 connHandle, uint8 state, uint8 status )
{
  if ( state == GAPBOND_PAIRING_STATE_STARTED )
  {    
    cbLOG_PRINT("Pairing state: Pairing started\r\n");      
  }
  else if ( state == GAPBOND_PAIRING_STATE_COMPLETE )
  {
    if ( status == SUCCESS )
    {
      linkDBItem_t  *pItem;

      if ( (pItem = linkDB_Find(connHandle )) != NULL )
      {
        if (( (pItem->stateFlags & LINK_BOUND) == LINK_BOUND ))
        {
          cbLED_flash(cbLED_GREEN, 3, 250, 500);        
          cbLOG_PRINT("Pairing state: Pairing success\r\n");      
        }
      }      
    }
    else
    {
      cbLOG_PRINT("Pairing state: Pairing error\r\n");      
    }
  }
  else if ( state == GAPBOND_PAIRING_STATE_BONDED )
  {
    cbLED_flash(cbLED_GREEN, 5, 250, 500);         
    cbLOG_PRINT("Pairing state: Bonding complete\r\n");      
  }
}

/*---------------------------------------------------------------------------
* Application event processor.  This function
* is called to process all events for the task. Events
* include timers, messages and any other user defined events.
* - task_id: The OSAL assigned task ID.
* - events: Events to process.  This is a bit map and can contain more 
*           than one event.
*-------------------------------------------------------------------------*/
uint16 cbDEMO_processEvent( uint8 taskId, uint16 events )
{
  if ( events & SYS_EVENT_MSG )
  {
    uint8 *pMsg;

    if ( (pMsg = osal_msg_receive( demo.taskId )) != NULL )
    {
      processOSALMsg( (osal_event_hdr_t *)pMsg );

      VOID osal_msg_deallocate( pMsg );
    }

    return (events ^ SYS_EVENT_MSG);
  }

  if ( events & cbDEMO_START_DEVICE_EVT )
  {
    uint16 accelRange;

    // Start the Device
    VOID GAPRole_StartDevice( &peripheralRoleCallbacks );

    // Start Bond Manager
    VOID GAPBondMgr_Register( &bondMgrCallbacks );       

    // Start the Accelerometer Profile
    VOID Accel_RegisterAppCBs( &accelServiceCallbacks );

    accelRange = 2;
    Accel_SetParameter(ACCEL_RANGE, 2, &accelRange );

    // Init sensors
    cbLED_init();
    cbSWI2C_init();   
    
    demo.accelerometerOk = cbLIS_open();
    demo.tempSensorOk = cbTMP112_open();    

    if (demo.accelerometerOk == TRUE)
    {
      cbLIS_register(wakeUpEvent, clickEvent);        
    }
    else
    {
      gapSetAlwaysAdvertising();
    }

    cbBLS_init();
    cbBLS_registerCallbacks(&blsCallbacks);
    cbBLS_open(cbBLS_PORT_0, NULL);    
    
#ifdef LOGGING
    initLogging(); 
#endif

    cbLOG_PRINT("\r\n\r\n\r\nDemo application started\r\n");    

    checkErrorCode();

    // Flash red LED three times
    cbLED_flash(cbLED_RED, 3, 250, 500);

    return ( events ^ cbDEMO_START_DEVICE_EVT );    
  }

  if ( events & cbDEMO_ACCEL_PERIODIC_EVT )
  {
    if (demo.gapProfileState == GAPROLE_CONNECTED)
    {
      // Read accelerometer and update profile data if the values have changed
      accelRead();
    }
    else
    {
      osal_stop_timerEx(demo.taskId, cbDEMO_ACCEL_PERIODIC_EVT);
    }

    return (events ^ cbDEMO_ACCEL_PERIODIC_EVT);
  } 

  if ( events & cbDEMO_ACCEL_CHECK_EVT )
  {
    // Accelerometer watchdog 
    // Check that the accelorometer is still configured as expected.
    // If the configuration is lost or changed reset. This never seems to happed.
    bool restore = cbLIS_checkAndRestoreConfiguration();
    cb_ASSERT(restore == FALSE);

    return (events ^ cbDEMO_ACCEL_CHECK_EVT);
  } 

  if ( events & cbDEMO_SPS_CONNECT_EVT )
  {
    // Ignore
    return (events ^ cbDEMO_SPS_CONNECT_EVT);
  }

  // Discard unknown events
  return 0;
}

/*---------------------------------------------------------------------------
* Process an incoming task message.
* - pMsg: Message to process
*-------------------------------------------------------------------------*/
static void processOSALMsg( osal_event_hdr_t *pMsg )
{
  switch ( pMsg->event )
  {
  default:
    break;
  }
}

/*---------------------------------------------------------------------------
* Peripheral role of a state change handler.
* - newState: new state
*-------------------------------------------------------------------------*/
static void peripheralStateNotificationCB( gaprole_States_t newState )
{
  uint16 connHandle = INVALID_CONNHANDLE;

  cb_ASSERT(newState != GAPROLE_ERROR);

  if ( demo.gapProfileState != newState )
  {
    switch( newState )
    {
    case GAPROLE_STARTED:
      {
        // Set the system ID from the bd addr
        uint8 systemId[DEVINFO_SYSTEM_ID_LEN];
        GAPRole_GetParameter(GAPROLE_BD_ADDR, systemId);

        // shift three bytes up
        systemId[7] = systemId[5];
        systemId[6] = systemId[4];
        systemId[5] = systemId[3];

        // set middle bytes to zero
        systemId[4] = 0;
        systemId[3] = 0;

        DevInfo_SetParameter(DEVINFO_SYSTEM_ID, DEVINFO_SYSTEM_ID_LEN, systemId);          

        updateNameWithAddressInfo();        

        cbLOG_PRINT("GAP State: Started\r\n");            
      }
      break;      

    case GAPROLE_ADVERTISING:       
      cbLOG_PRINT("GAP State: Advertising\r\n");               
      break;

    case GAPROLE_CONNECTED:
      cbLOG_PRINT("GAP State: Connected\r\n");     
      
      // Start periodic timer for accelerometer readings
      if (demo.accelerometerOk == TRUE)
      {
        osal_start_reload_timer(demo.taskId, cbDEMO_ACCEL_PERIODIC_EVT, ACCEL_READ_PERIOD);
      }            
      
      // Start peridoc temperature readings
      if (demo.tempSensorOk == TRUE)
      {
        cbTMP112_startPeriodic(5, temperatureReadEvent);
      }         
      
      GAPRole_GetParameter( GAPROLE_CONNHANDLE, &connHandle );     

#if defined ( PLUS_BROADCASTER )
      osal_start_timerEx( demo.taskId, cbDEMO_ADV_IN_CONNECTION_EVT, ADV_IN_CONN_WAIT );
#endif
      break;

    case GAPROLE_WAITING:
      cbLOG_PRINT("GAP State: Waiting\r\n");      
      cbTMP112_stopPeriodic();
      break;

    case GAPROLE_WAITING_AFTER_TIMEOUT:
      cbLOG_PRINT("GAP State: Waiting after timeout\r\n");            
      cbTMP112_stopPeriodic();
      break;

    default:
      // do nothing
      break;      
    }
  }

  demo.gapProfileState = newState;
}

/*---------------------------------------------------------------------------
* Called by the Accelerometer Profile when the Enabler Attribute
* is changed. This is not used in this application.
*-------------------------------------------------------------------------*/
static void accelEnablerChangeCB( void )
{
  // Iqnore
}

/*---------------------------------------------------------------------------
* Called by the application to read accelerometer data
* and write values to accelerometer service.
*-------------------------------------------------------------------------*/
static void accelRead( void )
{
  static int8 x, y, z;
  int8 new_x, new_y, new_z;

  // Read data for each axis of the accelerometer
  new_x = cbLIS_readX();  
  new_y = cbLIS_readY();
  new_z = cbLIS_readZ();

  // Check if x-axis value has changed by more than the threshold value and
  // set profile parameter if it has (this will send a notification if enabled)
  if( (x < (new_x-ACCEL_CHANGE_THRESHOLD)) || (x > (new_x+ACCEL_CHANGE_THRESHOLD)) )
  {
    x = new_x;
    Accel_SetParameter(ACCEL_X_ATTR, sizeof ( int8 ), &x);
  }

  // Check if y-axis value has changed by more than the threshold value and
  // set profile parameter if it has (this will send a notification if enabled)
  if( (y < (new_y-ACCEL_CHANGE_THRESHOLD)) || (y > (new_y+ACCEL_CHANGE_THRESHOLD)) )
  {
    y = new_y;
    Accel_SetParameter(ACCEL_Y_ATTR, sizeof ( int8 ), &y);
  }

  // Check if z-axis value has changed by more than the threshold value and
  // set profile parameter if it has (this will send a notification if enabled)
  if( (z < (new_z-ACCEL_CHANGE_THRESHOLD)) || (z > (new_z+ACCEL_CHANGE_THRESHOLD)) )
  {
    z = new_z;  
    Accel_SetParameter(ACCEL_Z_ATTR, sizeof ( int8 ), &z);
  }  
}

/*---------------------------------------------------------------------------
* Callback registered to accelerometer and called when the accelerometer
* detects that the device is moved. Enable advertising.
*-------------------------------------------------------------------------*/
void wakeUpEvent(void)
{
  uint8 advertEnabled = TRUE;

  // Updated the battery level in the battery service
  // Do not update when radio is active
  if ((demo.gapProfileState == GAPROLE_WAITING) || 
      (demo.gapProfileState == GAPROLE_WAITING_AFTER_TIMEOUT))
  {
    Batt_MeasLevel();
    //cbLED_flash(cbLED_GREEN, 1, 100, 0);
  }
  
  cbLOG_PRINT("Wake up event\r\n");      

  GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &advertEnabled );      
}

/*---------------------------------------------------------------------------
* Callback registered to accelerometer and called when the accelerometer
* detects a click. Click detection is currently not enabled.
*-------------------------------------------------------------------------*/
void clickEvent(void)
{
  // Ignore
}

/*---------------------------------------------------------------------------
* Callback for the serial port service. 
*-------------------------------------------------------------------------*/
void blsWriteCompleteEvent(uint8 port, uint16 nBytes)
{
  int8 res;
  uint8 *pBuf;

  cb_ASSERT(demo.waitWrite == TRUE);

  res = cbBLS_readBufConsumed(port, nBytes);
  cb_ASSERT(res == SUCCESS);

  res = cbBLS_getReadBuf(port,&pBuf, &nBytes);
  if (res == SUCCESS)
  {
    cb_ASSERT((pBuf != NULL) && (nBytes > 0));

    res = cbBLS_write(port, pBuf, MIN(nBytes, cbSPS_FIFO_SIZE));
    if (res == SUCCESS)
    {
      demo.waitWrite = TRUE;
    }
    else
    {
      demo.waitWrite = FALSE;
    }
  }
  else
  {
    demo.waitWrite = FALSE;
  }
}

/*---------------------------------------------------------------------------
* Callback for the serial port service.
*-------------------------------------------------------------------------*/
void blsDataAvailableEvent(uint8 port)
{
  int8 res;
  uint8 *pBuf;
  uint16 nBytes;

  cbLOG_PRINT(".");

  if (demo.waitWrite == FALSE)
  {
    res = cbBLS_getReadBuf(port, &pBuf, &nBytes);
    cb_ASSERT((res == SUCCESS) && (pBuf != NULL) && (nBytes > 0));

#if 0
    // Flash LEDs when echoing data
    cbLED_flash(cbLED_GREEN, 1, 30, 10);
#endif

    res = cbBLS_write(port, pBuf, MIN(nBytes, cbSPS_FIFO_SIZE));
    if (res == SUCCESS)
    {
      demo.waitWrite = TRUE;
    }
  }
}

void blsErrorEvent(uint8 port, uint8 error)
{
}

/*---------------------------------------------------------------------------
* Callback for the serial port service.
*-------------------------------------------------------------------------*/
void blsConnectEvent(void)
{
  // Ignore
}

/*---------------------------------------------------------------------------
* Callback for the serial port service.
*-------------------------------------------------------------------------*/
void blsDisconnectEvent(void)
{
  // Ignore
}

/*---------------------------------------------------------------------------
* Registered to tmp112. Called periodically when the temperature 
* is read by TMP112. 
* - temperature: temperature without decimals
* - raw: temperature in raw format as read from TMP112 sensor
*-------------------------------------------------------------------------*/
static void temperatureReadEvent(int8 temperature, uint16 raw)
{
  if (demo.currentTemperature != temperature)
  {
    demo.currentTemperature = temperature;
    cbTEMP_setTemperature(temperature);
  }
}

/*---------------------------------------------------------------------------
* Callback registered to led service. Called when the led characteristics are
* written.
*-------------------------------------------------------------------------*/
static void ledSetEvent(uint8 ledId, uint8 value)
{
  switch (ledId)
  {
  case cbLEDS_RED_LED_ID:
    if (value == cbLEDS_LED_OFF)
    {
      cbLED_set(cbLED_RED, FALSE);
    }
    else
    {
      cbLED_set(cbLED_RED, TRUE);
    }
    break;

  case cbLEDS_GREEN_LED_ID:
    if (value == cbLEDS_LED_OFF)
    {
      cbLED_set(cbLED_GREEN, FALSE);
    }
    else
    {
      cbLED_set(cbLED_GREEN, TRUE);
    }
    break;

  default:
    HAL_ASSERT_FORCED();
    break;
  }
}

/*---------------------------------------------------------------------------
* Get character representing a hexadecimal digit.
*-------------------------------------------------------------------------*/
uint8 getAscii(uint8 value)
{
  char hex[] = "0123456789ABCDEF";
  cb_ASSERT(value <= 0x0F);    
  return hex[value];
}

/*---------------------------------------------------------------------------
* Add the set the last part of hte device name to the final bytes of the 
* device address. Useful when mane demo devices are located in the same room.
*-------------------------------------------------------------------------*/
void updateNameWithAddressInfo(void)
{
  uint8 status;
  uint8 numberString[4];
  uint8 address[6];
  uint8 value;

  status = GAPRole_GetParameter(GAPROLE_BD_ADDR, address);
  cb_ASSERT(status == SUCCESS);

  value = (address[1] & 0xF0) >> 4;
  numberString[0] = getAscii(value);

  value = address[1] & 0x0F;
  numberString[1] = getAscii(value);     

  value = (address[0] & 0xF0) >> 4;
  numberString[2] = getAscii(value);

  value = address[0] & 0x0F;
  numberString[3] = getAscii(value);     

  // Replace "0000" part of "OLP425-0000"
  osal_memcpy(&attDeviceName[7], numberString, 4);
  osal_memcpy(&deviceName[9], numberString, 4);

  status = GAPRole_SetParameter( GAPROLE_SCAN_RSP_DATA, sizeof ( deviceName ), deviceName );
  cb_ASSERT(status == SUCCESS);

  status = GGS_SetParameter( GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN - 1, attDeviceName );  
  cb_ASSERT(status == SUCCESS);  
}

/*---------------------------------------------------------------------------
* Check for stored error codes and if found, log them using the cbLOG 
* functionality
*-------------------------------------------------------------------------*/
void checkErrorCode(void)
{ 
  static cbASH_ErrorCode error;  

  cbASSERT_readErrorCode(&error);

  if (error.line != 0)
  {
    cbLOG_PRINT("Stored error code found\r\n");  
    cbLOG_PRINT("File: %s\r\n", error.file);
    cbLOG_PRINT("Line: %d\r\n", (int)error.line);
    cbLOG_PRINT("Code: %d\r\n", (int)error.errorCode);
  }
}