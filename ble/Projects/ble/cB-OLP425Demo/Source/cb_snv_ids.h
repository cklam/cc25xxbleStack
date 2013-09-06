#ifndef _CB_SNV_IDS_H_
#define _CB_SNV_IDS_H_
/*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : -
 * File        : cb_snv_ids.h
 *
 * Description : Definition of svn ids.
 *               Note that the stack used ids below 0xA0. This could change.
 *-------------------------------------------------------------------------*/
#include "osal_snv.h"

/*===========================================================================
 * DEFINES
 *=========================================================================*/
#ifndef OSAL_SNV_UINT16_ID
  #define cbNVI_START                   (0x100)
#else
  #define cbNVI_START                   (0xA0)
#endif

#define cbNVI_ERROR_CODE_ID             (osalSnvId_t)(cbNVI_START + 1)
#define cbNVI_WATCHDOG_ID               (osalSnvId_t)(cbNVI_START + 2)
#define cbNVI_SERVER_PROFILE_ID         (osalSnvId_t)(cbNVI_START + 3)

#endif 






