/*---------------------------------------------------------------------------
 * Copyright (c) 2009 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : Assert handling
 * File        : cb_assert_handler.h
 *
 * Description : Definition of assert handler and funcionlity to read out 
 *               stored error codes.
 *-------------------------------------------------------------------------*/
#ifndef _CB_ASSERT_HANDLER_H_
#define _CB_ASSERT_HANDLER_H_

/*===========================================================================
 * DEFINES
 *=========================================================================*/
#define cbASH_FILE_NAME_MAX_LEN             16

/*===========================================================================
 * TYPES
 *=========================================================================*/
typedef struct 
{
  uint32 errorCode;
  uint8  file[cbASH_FILE_NAME_MAX_LEN];
  uint16 line;
} cbASH_ErrorCode;


/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/
extern void cbASSERT_readErrorCode(cbASH_ErrorCode *pError);

#endif





