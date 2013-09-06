#ifndef _CB_DEMO_H_
#define _CB_DEMO_H_

/*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : Demo application
 * File        : cb_demo.h
 *
 * Description : See cb_demo.c
 *-------------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C"
{
#endif

/*===========================================================================
 * DEFINES
 *=========================================================================*/
// Application Events handled in task function
#define cbDEMO_START_DEVICE_EVT                              0x0001
#define cbDEMO_ACCEL_PERIODIC_EVT                            0x0002
#define cbDEMO_ADV_IN_CONNECTION_EVT                         0x0004
#define cbDEMO_ACCEL_CHECK_EVT                               0x0008
#define cbDEMO_SPS_CONNECT_EVT                               0x0010

/*===========================================================================
 * TYPES
 *=========================================================================*/


/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/
extern void cbDEMO_init(uint8 taskId);

extern uint16 cbDEMO_processEvent(uint8 taskId, uint16 events);

#ifdef __cplusplus
}
#endif

#endif
