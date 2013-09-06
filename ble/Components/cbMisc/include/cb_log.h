#ifndef _CB_LOG_H_
#define _CB_LOG_H_
/*---------------------------------------------------------------------------
 * Copyright (c) 2009 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : 
 * File        : cb_log.h
 *
 * Description : 
 *-------------------------------------------------------------------------*/

#include "bcomdef.h"
#include "stdio.h"

/*===========================================================================
 * DEFINES
 *=========================================================================*/
#define cbLOG_BUF_SIZE 50
extern char cbLOG_buf[];

#ifdef LOGGING
#define cbLOG_PRINT(...)      {\
  /* snprintf not supported by IAR? */ \
  int sprintf_len = sprintf(cbLOG_buf, __VA_ARGS__); \
  cb_ASSERT(sprintf_len < cbLOG_BUF_SIZE);\
  cbLOG_print(cbLOG_buf);\
}
#else
  #define cbLOG_PRINT(...)
#endif

/*===========================================================================
 * TYPES
 *=========================================================================*/


/*---------------------------------------------------------------------------
* Callback that is called when cbLOG_print is invoked.
*-------------------------------------------------------------------------*/
typedef void (*cbLOG_PrintHandler)(const char* pMsg);


/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/

/*---------------------------------------------------------------------------
 * Initializes the log module
 *-------------------------------------------------------------------------*/
void cbLOG_init();

/*---------------------------------------------------------------------------
* Registers the user defined print handler.
* - printHandler: print handler callback
*-------------------------------------------------------------------------*/
void cbLOG_registerPrintHandler(cbLOG_PrintHandler printHandler);

/*---------------------------------------------------------------------------
* Logs a string according to the user installed print handler.
*-------------------------------------------------------------------------*/
void cbLOG_print(const char* pMsg);

#endif /* _CB_LOG_H_ */

