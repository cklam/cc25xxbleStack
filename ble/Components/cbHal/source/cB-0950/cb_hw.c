/*---------------------------------------------------------------------------
 * Copyright (c) 2000, 2001 connectBlue AB, Sweden.
 * Any reproduction without written permission is prohibited by law.
 *
 * Component   : HW Init
 * File        : cb_hw_init.c
 *
 * Description : Initialization of all IO pins.
 *-------------------------------------------------------------------------*/
#include "hal_mcu.h"
#include "cb_hw.h"

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
void cbHW_init(void)
{
  P0SEL = 0; // Configure Port 0 as GPIO
  P1SEL = 0; // Configure Port 1 as GPIO
  P2SEL = 0; // Configure Port 2 as GPIO

  // P0
  // P0.0 ADC0 (IN TRI-STATE)
  // P0.1 ADC1 (IN TRI-STATE)
  // P0.2 UART_RxD* / SPI_MISO* / SPI_SS** (IN TRI-STATE)
  // P0.3 UART_TxD* / SPI_MOSI* / SPI_CLK** (IN TRI-STATE)
  // P0.4 UART_CTS* / SPI_SS* / SPI_MOSI** (IN TRI-STATE)
  // P0.5 UART_RTS* / SPI_CLK* / SPI_MISO* (IN TRI-STATE)
  // P0.6 ADC_IN2 (IN TRI-STATE)
  // P0.7 ADC_IN3 / Interrupt 2 (TMP112 interrupt) (IN TRI-STATE)

  P0DIR = 0x00;   // All inputs
  //P0DIR = 0xFF;     // All outputs
  //P0INP = 0xFF; // Tristate
  P0INP = 0x00;   // PU/PD
  P2INP &= ~0x20; // Port 0 PU
  //P2INP = 0x20; // Port 0 PD
  P0 = 0x00; 

  // P1
  // 11110011
  // P1.0 VCC_Peripheral  (OUT - LOW)
  // P1.1 Blue LED (OUT - HIGH)
  // P1.2 Switch-0 (IN)
  // P1.3 Interrupt 1 (IN) 
  // P1.4 Green/Switch-1 (OUT)
  // P1.5 I2C_CLK (IN - PU)
  // P1.6 I2C_SDA (IN - PU)
  // P1.7 Red (OUT)
  P1DIR =  ((1<<7) | (1<<4) | (1<<1)); 
  P1INP =  0x00; // All pins PU/PD
  P1    =  (1<<7) | (1<<4) | (1<<1);
  P2INP |= 0x40;// Port 1 PU
  //P2INP &= ~0x40;// Port 1 PD

  // P2
  // P2.0 ExtInt (IN)
  // P2.1 Debug_Data / UART_DTR (IN)
  // P2.2 Debug_Clk / UART_DSR (IN)
  P2DIR = 0x00; 
  //P2    = 0x00; 
  //P2INP |= 0x1F; // Port 2 Tri state                

  ///test 
  P2DIR = 0x1F; // All port 1 pins (P2.0-P2.4) as output
  P2 = 0;
}

void cbHW_refClkOnP1_2(void)
{
  /* Timer test */
  CLKCONCMD &= ~CLKSPD(0); // 32 MHz
  CLKCONCMD &= ~(OSC); // 32 MHz crystal
  CLKCONCMD |= TICKSPD(3); // 4 MHz

  PERCFG |= (1 << 6); // T1CFG Timer 1 I/O location alternative 2 location
  P1DIR |= 0x04; // P1.2 = Output
  P1SEL |= 0x04; // P1.2 = Peripheral function
  P2SEL &= ~(1 << 4); // PRI1P1   Timer 1 has priority vs Timer 4
  P2SEL |= (1 << 3); // PRI0P1   Timer 1 has priority vs UART 1

  T1CC0L = 0x01;
  T1CC0H = 0x00;

  T1CTL = 0x02; // Tick frequency/1, Modulo mode

  T1CCTL0 = 0x38; // magic init?
  T1CCTL0 = 0x14;  

  while(1);
}


/*===========================================================================
 * STATIC FUNCTIONS
 *=========================================================================*/

/*---------------------------------------------------------------------------
 * Description of function. Optional verbose description.
 *-------------------------------------------------------------------------*/




