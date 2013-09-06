/*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : Assert
 * File        : cb_assert_handler.c
 *
 * Description : Implementation of assert handler and functionality
 *               to read stored error codes.
 *
 *-------------------------------------------------------------------------*/
#include "hal_types.h"
#include "OSAL.h"
#include "HAL_MCU.h"
#include "hal_assert.h"
#include "cb_assert.h"
#include "cb_assert_handler.h"
#include "osal_snv.h"
#include "cb_assert.h"
#include "cb_snv_ids.h"

/*===========================================================================
 * DEFINES
 *=========================================================================*/

/*===========================================================================
 * TYPES
 *=========================================================================*/

/*===========================================================================
 * DECLARATIONS
 *=========================================================================*/

/*===========================================================================
 * DEFINITIONS
 *=========================================================================*/

/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/
 /*---------------------------------------------------------------------------
 * Handler called by cb_ASSERT macro. Store error code to NVDS.
 *-------------------------------------------------------------------------*/
void cbASSERT_handler(int32 errorCode, const char* file, int32 line)
{
  cbASH_ErrorCode error;

  error.errorCode = errorCode;
  error.line = line;
  osal_memcpy(error.file, file, MIN((osal_strlen((char*)file) + 1), cbASH_FILE_NAME_MAX_LEN));
  
  osal_snv_write( cbNVI_ERROR_CODE_ID, sizeof(error), (void*)&error);

  HAL_SYSTEM_RESET();
}

/*---------------------------------------------------------------------------
 * Reset device
 *-------------------------------------------------------------------------*/
void cbASSERT_resetHandler(void)
{
  HAL_SYSTEM_RESET();
}

/*---------------------------------------------------------------------------
 * Description of function. Optional verbose description.
 *-------------------------------------------------------------------------*/
void cbASSERT_readErrorCode(cbASH_ErrorCode *pError)
{ 
  uint8 res;
  
  HAL_ASSERT(pError != NULL);

  res = osal_snv_read(cbNVI_ERROR_CODE_ID, sizeof(cbASH_ErrorCode), pError);  
  if (res != SUCCESS)
  {
    osal_memset(&pError, 0, sizeof(pError));
  }
}

/*---------------------------------------------------------------------------
 * Description of function. Optional verbose description.
 *-------------------------------------------------------------------------*/
void halAssertHandler(void)
{    
  HAL_SYSTEM_RESET();
}



