#ifndef _CBUF_H_
#define _CBUF_H_
/*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : Buffer
 * File        : cb_buffer.h
 *
 * Description : Declaration of types and functions for 
 *               the circular buffer class.
 *-------------------------------------------------------------------------*/

#include "comdef.h"
#include "hal_types.h"

/*===========================================================================
 * DEFINES
 *=========================================================================*/

/* Return Codes */
#define cbBUF_OK        (0x00)
#define cbBUF_ERROR     (0x01)
#define cbBUF_FULL      (0x02)
#define cbBUF_NO_DATA   (0x03)

/*===========================================================================
 * TYPES
 *=========================================================================*/

/*===========================================================================
 * FUNCTIONS
 *=========================================================================*/

/*---------------------------------------------------------------------------
 * Initializes the circular buffer component.
 *-------------------------------------------------------------------------*/
uint8 cbBUF_init(void);

/*---------------------------------------------------------------------------
 * Opens a circular buffer.
 * - size: Number of bytes in buffer.
 * - minReturnSize: No buffer is returned unless minimum size is available.
 * - reservedSize: Wrap-around if within reserved size of buffer end.
 * - pBufferId: Pointer to returned buffer identifier.
 *-------------------------------------------------------------------------*/
uint8 cbBUF_open(
    uint16 size, 
    uint16 minReturnSize,
    uint16 reservedSize,
    uint8 *pBufferId);

/*---------------------------------------------------------------------------
 * Empties and re-initialises the buffer.
 * - bufId: Buffer identifier received when buffer was opened.
 *-------------------------------------------------------------------------*/
uint8 cbBUF_clear(uint8 bufId);

/*---------------------------------------------------------------------------
 * Gets a pointer to buffer for writing.
 * Buffer must be equal or bigger than reserved size.
 * For every cbBUF_GetWriteBuf there must be one cbBUF_WriteBufProduced.
 * - bufId: Buffer identifier received when buffer was opened.
 * - ppBuf: Returned buffer pointer for writing.
 * - pBufSize: Returned size of buffer for writing.
 *-------------------------------------------------------------------------*/
uint8 cbBUF_getWriteBuf(
    uint8  bufId, 
    uint8  **ppBuf, 
    uint16 *pBufSize);

/*---------------------------------------------------------------------------
 * Gets a pointer to buffer for writing.
 * Buffer does not need to be equal or bigger than reserved size.
 * For every cbBUF_GetWriteBuf there must be one cbBUF_WriteBufProduced.
 * - bufId: Buffer identifier received when buffer was opened.
 * - ppBuf: Returned buffer pointer for writing.
 * - pBufSize: Returned size of buffer for writing.
 *-------------------------------------------------------------------------*/
uint8 cbBUF_getAvailableWriteBuf(
    uint8  bufId, 
    uint8  **ppBuf, 
    uint16 *pBufSize);

/*---------------------------------------------------------------------------
 * Returns a previously allocated write buffer.
 * For every cbBUF_GetWriteBuf there must be one cbBUF_WriteBufProduced.
 * - bufId: Buffer identifier received when buffer was opened.
 * - nBytes: Number of bytes that has been written in write buffer.
 *-------------------------------------------------------------------------*/
uint8 cbBUF_writeBufProduced(
    uint8  bufId, 
    uint16 nBytes);
                                
/*---------------------------------------------------------------------------
 * Returns part of a previously allocated write buffer.
 * The buffer can still be used for writing.
 * For every cbBUF_GetWriteBuf there must be one cbBUF_WriteBufProduced.
 * - bufId: Buffer identifier received when buffer was opened.
 * - nBytes: Number of bytes that has been written in write buffer.
 *-------------------------------------------------------------------------*/                                
uint8 cbBUF_writeBufProducedCont(
    uint8  bufId,
    uint16 nBytes);

/*---------------------------------------------------------------------------
 * Gets a pointer to a buffer for reading.
 * For every cbBUF_GetReadBuf there must be one cbBUF_ReadBufConsumed.
 * - bufId: Buffer identifier received when buffer was opened.
 * - ppBuf: Returned buffer pointer for reading data.
 * - pBufSize: Returned size of buffer for reading.
 *-------------------------------------------------------------------------*/
uint8 cbBUF_getReadBuf(
    uint8  bufId, 
    uint8  **ppBuf, 
    uint16 *pBufSize);

/*---------------------------------------------------------------------------
 * Returns a previously allocated read buffer.
 * For every cbBUF_GetReadBuf there must be one cbBUF_ReadBufConsumed.
 * - bufId: Buffer identifier received when buffer was opened.
 * - nBytes: Number of bytes that has been read from read buffer.
 *-------------------------------------------------------------------------*/
uint8 cbBUF_readBufConsumed(
    uint8  bufId, 
    uint16 nBytes);

/*---------------------------------------------------------------------------
 * Reads one byte from buffer.
 * - bufId: Buffer identifier received when buffer was opened.
 * - pByte: Pointer to the read byte.
 *-------------------------------------------------------------------------*/
uint8 cbBUF_readByte(
    uint8  bufId, 
    uint8  *pByte);

/*---------------------------------------------------------------------------
 * Writes one byte to buffer.
 * - bufId: Buffer identifier received when buffer was opened.
 * - byte: Byte to write.
 *-------------------------------------------------------------------------*/
uint8 cbBUF_writeByte(
    uint8  bufId, 
    uint8  byte);

/*---------------------------------------------------------------------------
 * Checks if buffer is empty.
 * Returns TRUE if empty and FALSE otherwise.
 * - bufId: Buffer identifier received when buffer was opened.
 *-------------------------------------------------------------------------*/
bool cbBUF_isBufferEmpty(uint8 bufId);

/*---------------------------------------------------------------------------
 * Gets number of bytes available for reading from the buffer.
 * - bufId: Buffer identifier received when buffer was opened.
 *-------------------------------------------------------------------------*/
uint16 cbBUF_getNoBytes(uint8 bufId);

/*---------------------------------------------------------------------------
 * Gets number of bytes available for writing to the buffer.
 * - bufId: Buffer identifier received when buffer was opened.
 *-------------------------------------------------------------------------*/
uint16 cbBUF_getNoFreeBytes(uint8 bufId);

/*---------------------------------------------------------------------------
 * Gets number of bytes that was reserved in cbBUF_open.
 * - bufId: Buffer identifier received when buffer was opened.
 *-------------------------------------------------------------------------*/
uint16 cbBUF_getReservedSize(uint8 bufId);

#endif


