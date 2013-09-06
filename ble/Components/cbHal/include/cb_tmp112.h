#ifndef _TMP112_SENSOR_H_
#define _TMP112_SENSOR_H_

/*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : tracer
 * File        : tmp112_sensor.h
 *
 * Description : See tmp112_sensor.c
 *-------------------------------------------------------------------------*/
#include "hal_types.h"

/*===========================================================================
 * DEFINES
 *=========================================================================*/

/*===========================================================================
 * TYPES
 *=========================================================================*/
typedef void (*cbTMP122_TemperatureReadEvent)(int8 temperature, uint16 raw);

/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/
void cbTMP112_init(uint8 taskId);
bool cbTMP112_open(void);
uint16 cbTMP112_processEvent(uint8 task_id, uint16 events);
void cbTMP112_startPeriodic(uint16 periodInSeconds, cbTMP122_TemperatureReadEvent callback);
void cbTMP112_stopPeriodic(void);
void cbTMP112_readSingle(cbTMP122_TemperatureReadEvent callback);
bool cbTMP112_selfTest(void);

#endif






