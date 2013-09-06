/*---------------------------------------------------------------------------
 * Copyright (c) 2000 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : Log
 * File        : cb_log.c
 *
 * Description : Implementation of logging functionality
 *-------------------------------------------------------------------------*/

#include "hal_types.h"
#include "cb_assert.h"
#include "cb_log.h"


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
static cbLOG_PrintHandler printHandler = NULL;
static const char* file = "log";

char cbLOG_buf[cbLOG_BUF_SIZE];

/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/
void cbLOG_init()
{
  //Ignore
}

/*---------------------------------------------------------------------------
 * Description of function. Optional verbose description.
 *-------------------------------------------------------------------------*/
void cbLOG_registerPrintHandler(cbLOG_PrintHandler callback)
{
    cb_ASSERT(callback != NULL);
    printHandler = callback;
}

/*---------------------------------------------------------------------------
 * Description of function. Optional verbose description.
 *-------------------------------------------------------------------------*/
void cbLOG_print( const char* pMsg)
{
    if(printHandler != NULL)
    {
        printHandler(pMsg);
    }
}
