#ifndef _LIS3DH_SENSOR_H_
#define _LIS3DH_SENSOR_H_

/*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : tracer
 * File        : lis3dh_sensor.h
 *
 * Description : See lis3dh_sensor.c
 *-------------------------------------------------------------------------*/
#include "hal_types.h"

/*===========================================================================
 * DEFINES
 *=========================================================================*/

/*===========================================================================
 * TYPES
 *=========================================================================*/
typedef void (*cbLIS_WakeUpEvent) (void);
typedef void (*cbLIS_ClickEvent) (void);
/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/
void cbLIS_init(uint8 taskId);
void cbLIS_register(cbLIS_WakeUpEvent wukeUpHandler, cbLIS_ClickEvent clickHandler);
bool cbLIS_selfTest(void);
bool cbLIS_open(void);

void cbLIS_setTemperatureOffset(int8 temperatureOffset);
uint8 cbLIS_getTemperatureOffset(void);
int8 cbLIS_readTemperature(void);
bool cbLIS_checkAndRestoreConfiguration(void);
void cbLIS_setWakeupTreshold(uint8 treshold);
void cbLIS_enableWakeupInterrupt(void);
void cbLIS_disableWakeupInterrupt(void);

int8 cbLIS_readX(void);
int8 cbLIS_readY(void);
int8 cbLIS_readZ(void);

uint16 cbLIS_processEvent(uint8 taskId, uint16 events);
#endif






