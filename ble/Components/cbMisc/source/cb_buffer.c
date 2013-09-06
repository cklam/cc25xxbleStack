/*---------------------------------------------------------------------------
* Copyright (c) 2000, 2001 connectBlue AB, Sweden.
* Any reproduction without written permission is prohibited by law.
*
* Component   : Circular Buffer
* File        : cb_buffer.c
*
* Description : Implementation of circular buffer class.
*-------------------------------------------------------------------------*/

#include "comdef.h"
#include "hal_types.h"
#include "OSAL.h"

#include "cb_assert.h"
#include "cb_buffer.h"

/*===========================================================================
* DEFINES
*=========================================================================*/

/* Total number and size of all buffers */
#ifndef cbBUF_MAX_BUFFERS
#define cbBUF_MAX_BUFFERS   ((uint16)2)
#endif

#ifndef cbBUF_SIZE
#define cbBUF_SIZE (128 + 200)
#endif

/*===========================================================================
* TYPES
*=========================================================================*/
typedef enum
{
    cbBUF_S_CLOSED,
    cbBUF_S_IDLE,
    cbBUF_S_USER_STORING,
    cbBUF_S_USER_READING,
    cbBUF_S_USER_READING_AND_STORING
} cbBUF_State;

typedef struct
{
    cbBUF_State  state;

    uint8        *data; /* Will be used as an array except in cbBUF_Open */
    uint16       bufSize;

    uint16       readIndex;
    uint16       writeIndex;
    uint16       currentSize;
    uint16       endOfDataIndex; /* defines current wrap-around */

    uint16       minReturnSize;
    uint16       reservedSize;

} cbBUF_Adm;

typedef struct
{
    cbBUF_Adm   buf[cbBUF_MAX_BUFFERS]; 
    uint16      usedSize;
    uint8       nbrOfOpenedBuffers;
} cbBUF_Class;

/*===========================================================================
* DECLARATIONS
*=========================================================================*/

/*===========================================================================
* DEFINITIONS
*=========================================================================*/
// Filename used by cb_ASSERT macro
static const char *file = "cb_serial.c";
static uint8 circBuffer[cbBUF_SIZE];
static cbBUF_Class cBuf;

/*===========================================================================
* FUNCTIONS
*=========================================================================*/

uint8 cbBUF_init(void)
{
    uint8 i;

    for (i = 0; i < cbBUF_MAX_BUFFERS;i++)
    {
        cBuf.buf[i].state = cbBUF_S_CLOSED;
    }

    cBuf.usedSize            = 0;
    cBuf.nbrOfOpenedBuffers  = 0;

    return cbBUF_OK;
}


uint8 cbBUF_open(
    uint16 size, 
    uint16 minReturnSize,
    uint16 reservedSize,
    uint8 *pBufferId)
{
    uint8 id = cBuf.nbrOfOpenedBuffers;

    *pBufferId = id;

    cb_ASSERT(id < cbBUF_MAX_BUFFERS);
    cb_ASSERT((size + cBuf.usedSize) <= cbBUF_SIZE);

    cBuf.buf[id].data = &(circBuffer[cBuf.usedSize]);

    cBuf.buf[id].bufSize        = size;
    cBuf.buf[id].readIndex      = 0;
    cBuf.buf[id].writeIndex     = 0;
    cBuf.buf[id].currentSize    = 0; 
    cBuf.buf[id].endOfDataIndex = size - 1;
    cBuf.buf[id].minReturnSize  = minReturnSize;
    cBuf.buf[id].reservedSize   = reservedSize;
    cBuf.buf[id].state          = cbBUF_S_IDLE; 

    cBuf.usedSize += size;
    cBuf.nbrOfOpenedBuffers++;

    return cbBUF_OK;
}

uint8 cbBUF_clear(uint8 id)
{
    cBuf.buf[id].readIndex      = 0;
    cBuf.buf[id].writeIndex     = 0;
    cBuf.buf[id].currentSize    = 0; 
    cBuf.buf[id].endOfDataIndex = cBuf.buf[id].bufSize - 1;
    cBuf.buf[id].state          = cbBUF_S_IDLE; 

    return cbBUF_OK;
}

uint8 cbBUF_getWriteBuf(uint8 id, uint8 **pBuf, uint16 *bufSize)
{
    uint8    result = cbBUF_OK;
    uint16      readIndex;
    uint16      writeIndex;

    cb_ASSERT(id < cbBUF_MAX_BUFFERS);
    cb_ASSERT((cBuf.buf[id].state == cbBUF_S_IDLE) || (cBuf.buf[id].state == cbBUF_S_USER_READING));

    writeIndex = cBuf.buf[id].writeIndex;
    readIndex =  cBuf.buf[id].readIndex;

    /* Buffer is not full */
    if((cBuf.buf[id].currentSize == 0) ||
        (writeIndex > readIndex) ||
        ((writeIndex < readIndex) && ((readIndex - writeIndex) >= cBuf.buf[id].minReturnSize)))
    {
        /* Get write buffer */
        *pBuf = &(cBuf.buf[id].data[writeIndex]);
        if (writeIndex >= readIndex)
        {   
            *bufSize = cBuf.buf[id].bufSize - writeIndex;
        }
        else
        {
            *bufSize = readIndex - writeIndex;          
        }

        switch (cBuf.buf[id].state)
        {      
        case cbBUF_S_IDLE:
            cBuf.buf[id].state = cbBUF_S_USER_STORING;
            break;

        case cbBUF_S_USER_READING:
            cBuf.buf[id].state = cbBUF_S_USER_READING_AND_STORING;
            break;

        default:
            cb_EXIT(cBuf.buf[id].state);
            break;
        }
    }
    else
    {
        result         = cbBUF_FULL;    
        *pBuf          = NULL;
        *bufSize = 0;
    }

    return result;
}

uint8 cbBUF_getAvailableWriteBuf(uint8 id, uint8 **pBuf, uint16 *bufSize)
{
    uint8    result = cbBUF_OK;
    uint16      readIndex;
    uint16      writeIndex;

    cb_ASSERT(id < cbBUF_MAX_BUFFERS);
    cb_ASSERT((cBuf.buf[id].state == cbBUF_S_IDLE) || (cBuf.buf[id].state == cbBUF_S_USER_READING));

    writeIndex = cBuf.buf[id].writeIndex;
    readIndex =  cBuf.buf[id].readIndex;

    /* Buffer is not full */
    if((cBuf.buf[id].currentSize == 0) ||
        (writeIndex > readIndex) ||
        ((writeIndex < readIndex) && ((readIndex - writeIndex) >= 1)))
    {
        /* Get write buffer */
        *pBuf = &(cBuf.buf[id].data[writeIndex]);
        if (writeIndex >= readIndex)
        {   
            *bufSize = cBuf.buf[id].bufSize - writeIndex;
        }
        else
        {
            *bufSize = readIndex - writeIndex;          
        }

        switch (cBuf.buf[id].state)
        {      
        case cbBUF_S_IDLE:
            cBuf.buf[id].state = cbBUF_S_USER_STORING;
            break;

        case cbBUF_S_USER_READING:
            cBuf.buf[id].state = cbBUF_S_USER_READING_AND_STORING;
            break;

        default:
            cb_EXIT(cBuf.buf[id].state);
            break;
        }
    }
    else
    {
        result         = cbBUF_FULL;  
        *pBuf          = NULL;
        *bufSize = 0;
    }

    return result;
}

uint8 cbBUF_writeBufProduced(uint8 id, uint16 nBytes)
{
    cb_ASSERT(id < cbBUF_MAX_BUFFERS);

    cBuf.buf[id].currentSize += nBytes;
    cb_ASSERT( cBuf.buf[id].currentSize <= cBuf.buf[id].bufSize);

    /* No wrap-around */
    if (cBuf.buf[id].writeIndex >=  cBuf.buf[id].readIndex)
    {
        cBuf.buf[id].writeIndex += nBytes;

        cb_ASSERT(cBuf.buf[id].writeIndex <= cBuf.buf[id].bufSize);

        /* Perform wrap-around */
        if (cBuf.buf[id].writeIndex >= (cBuf.buf[id].bufSize - cBuf.buf[id].reservedSize))
        {
            cBuf.buf[id].endOfDataIndex  = cBuf.buf[id].writeIndex - 1;

            cBuf.buf[id].writeIndex = 0;
        }    
    }
    /* Wrap-around */
    else
    {
        cBuf.buf[id].writeIndex += nBytes;

        cb_ASSERT(cBuf.buf[id].writeIndex <=  cBuf.buf[id].readIndex);

        if( cBuf.buf[id].writeIndex == cBuf.buf[id].readIndex)
        {
            cb_ASSERT(cBuf.buf[id].currentSize == (cBuf.buf[id].endOfDataIndex + 1));
        }  
    }

    switch (cBuf.buf[id].state)
    {
    case cbBUF_S_USER_STORING:
        cBuf.buf[id].state = cbBUF_S_IDLE;
        break;

    case cbBUF_S_USER_READING_AND_STORING:      
        cBuf.buf[id].state = cbBUF_S_USER_READING;
        break;

    default:
        cb_EXIT(cBuf.buf[id].state);
        break;
    }

    return cbBUF_OK;
}

uint8 cbBUF_getReadBuf(uint8 id, uint8** pBuf, uint16 *bufSize)
{
    uint8 result = cbBUF_OK;

    cb_ASSERT(id < cbBUF_MAX_BUFFERS);
    cb_ASSERT((cBuf.buf[id].state == cbBUF_S_IDLE) || (cBuf.buf[id].state == cbBUF_S_USER_STORING));

    /* Buffer is empty */
    if (cBuf.buf[id].currentSize == 0)
    {
        result = cbBUF_NO_DATA;

        *pBuf          = NULL;
        *bufSize = 0;    
    }
    /* Buffer not empty */
    else
    {
        *pBuf = &(cBuf.buf[id].data[cBuf.buf[id].readIndex]);

        /* No wrap-around */
        if (cBuf.buf[id].writeIndex > cBuf.buf[id].readIndex)
        {
            *bufSize = cBuf.buf[id].writeIndex - cBuf.buf[id].readIndex;
        }
        /* Wrap-around */
        else
        {
            *bufSize = cBuf.buf[id].endOfDataIndex - cBuf.buf[id].readIndex + 1;
        }

        switch (cBuf.buf[id].state)
        {      
        case cbBUF_S_IDLE: 
            cBuf.buf[id].state = cbBUF_S_USER_READING;
            break;

        case cbBUF_S_USER_STORING:
            cBuf.buf[id].state = cbBUF_S_USER_READING_AND_STORING;
            break;     

        default:
            cb_EXIT(cBuf.buf[id].state);
            break;
        }    
		
        result = cbBUF_OK;    
    }

    return result;
}


uint8 cbBUF_readBufConsumed(uint8 id, uint16 nBytes)
{
    cb_ASSERT(id < cbBUF_MAX_BUFFERS);

    cBuf.buf[id].currentSize -= nBytes;
    cb_ASSERT( cBuf.buf[id].currentSize <= cBuf.buf[id].bufSize);

    /* No wrap-around */
    if (cBuf.buf[id].writeIndex > cBuf.buf[id].readIndex)
    {
        cBuf.buf[id].readIndex += nBytes;
        cb_ASSERT(cBuf.buf[id].readIndex <= cBuf.buf[id].writeIndex);        
    }
    /* Wrap-around */
    else
    {
        cBuf.buf[id].readIndex += nBytes;
        cb_ASSERT(cBuf.buf[id].readIndex <= (cBuf.buf[id].endOfDataIndex + 1));

        if (cBuf.buf[id].readIndex > cBuf.buf[id].endOfDataIndex)
        {
            cBuf.buf[id].readIndex = 0;
            cBuf.buf[id].endOfDataIndex = cBuf.buf[id].bufSize - 1;
        } 
    }        

    switch (cBuf.buf[id].state)
    {            
    case cbBUF_S_USER_READING:
        cBuf.buf[id].state = cbBUF_S_IDLE;
        break;

    case cbBUF_S_USER_READING_AND_STORING:
        cBuf.buf[id].state = cbBUF_S_USER_STORING;    
        break;

    default:
        cb_EXIT(cBuf.buf[id].state);
        break;
    }

    return cbBUF_OK;
}


uint8 cbBUF_readByte(uint8 id, uint8* pByte)
{
    uint8 result = cbBUF_OK;

    cb_ASSERT(id < cbBUF_MAX_BUFFERS); 
    cb_ASSERT((cBuf.buf[id].state == cbBUF_S_IDLE) || (cBuf.buf[id].state == cbBUF_S_USER_STORING));
    cb_ASSERT(cBuf.buf[id].readIndex <= cBuf.buf[id].endOfDataIndex);

    if (cBuf.buf[id].currentSize == 0)
    {
        result = cbBUF_NO_DATA;
    }
    else
    {
        *pByte = cBuf.buf[id].data[cBuf.buf[id].readIndex];

        cBuf.buf[id].readIndex++;
        cBuf.buf[id].currentSize--;

        if (cBuf.buf[id].readIndex > cBuf.buf[id].endOfDataIndex)
        {
            cBuf.buf[id].readIndex = 0;
            cBuf.buf[id].endOfDataIndex = cBuf.buf[id].bufSize - 1;
        } 
    }

    return result;
}


uint8 cbBUF_writeByte(uint8 id, uint8 data)
{
    uint8 result = cbBUF_OK;

    cb_ASSERT(id < cbBUF_MAX_BUFFERS); 
    cb_ASSERT((cBuf.buf[id].state == cbBUF_S_IDLE) || (cBuf.buf[id].state == cbBUF_S_USER_READING));

    /* Buffer full */
    if ((cBuf.buf[id].writeIndex == cBuf.buf[id].readIndex ) && (cBuf.buf[id].currentSize != 0))
    {
        cb_ASSERT(cBuf.buf[id].currentSize ==  (cBuf.buf[id].endOfDataIndex + 1));

        result = cbBUF_FULL;
    }
    /* Buffer not full */
    else
    {
        cBuf.buf[id].data[cBuf.buf[id].writeIndex] = data;

        cBuf.buf[id].writeIndex++;
        cBuf.buf[id].currentSize++;

        /* Perform wrap-around */
        if (cBuf.buf[id].writeIndex >= (cBuf.buf[id].bufSize - cBuf.buf[id].reservedSize))
        {
            cBuf.buf[id].endOfDataIndex = cBuf.buf[id].writeIndex - 1;

            cBuf.buf[id].writeIndex = 0;
        } 
    }

    return result;
}


bool cbBUF_isBufferEmpty(uint8 id)
{
    cb_ASSERT(id < cbBUF_MAX_BUFFERS);

    if (cBuf.buf[id].currentSize == 0)
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

uint16 cbBUF_getNoBytes(uint8 id)
{
    cb_ASSERT(id < cbBUF_MAX_BUFFERS);

    return cBuf.buf[id].currentSize;
}

uint16 cbBUF_getNoFreeBytes(uint8 id)
{
    cb_ASSERT(id < cbBUF_MAX_BUFFERS);

    return (cBuf.buf[id].bufSize - cBuf.buf[id].currentSize);
}

uint16 cbBUF_getReservedSize(uint8 id)
{
    cb_ASSERT(id < cbBUF_MAX_BUFFERS);

    return cBuf.buf[id].reservedSize;
}
