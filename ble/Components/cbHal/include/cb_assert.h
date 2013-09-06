/*---------------------------------------------------------------------------
 * Copyright (c) 2009 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : Assert
 * File        : cb_assert.h
 *
 * Description : ASSERT macro variations.
 *-------------------------------------------------------------------------*/
#ifndef _cb_ASSERT_H_
#define _cb_ASSERT_H_

/*===========================================================================
 * DEFINES
 *=========================================================================*/

/*
 * Internal platform function declarations.
 * Shall never be called directly.
 */
extern void cbASSERT_handler(int32 errorCode, const char* file, int32 line); 
extern void cbASSERT_resetHandler(void);

#ifndef NASSERT

/*
 * If the condition (C) evaluates to FALSE, the registered error handler in cbOS
 * is called with file and line info before the system is reset.
 */
#define cb_ASSERT(C)     if(!(C)){cbASSERT_handler(-1, (const char*)file , __LINE__);}
#define cb_ASSERTC(C)    if(!(C)){cbASSERT_handler(-1, (const char*)file , __LINE__);}
#define cb_ASSERT2(C, E) if(!(C)){cbASSERT_handler(E, (const char*)file , __LINE__);}

/*
 * The registered error handler is called with the file and line info before asystem reset.
 */
#define cb_EXIT(E) cbASSERT_handler(((int32)(E)), (const char*)file, __LINE__)

#else

#define cb_ASSERT(C)
#define cb_ASSERTC(C) if(!(C)){cbASSERT_resetHandler();} // Critical assert is never removed.
#define cb_ASSERT2(C, E)
#define cb_EXIT(E) cbASSERT_resetHandler()

#endif

#endif





