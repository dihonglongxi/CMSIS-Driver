/* -----------------------------------------------------------------------------
 * Copyright (c) 2019 Arm Limited (or its affiliates). All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *
 * $Date:        12. November 2019
 * $Revision:    V1.0
 *
 * Project:      Simple serial buffer
 * -------------------------------------------------------------------------- */

#include <string.h>

#include "ESP8266_Serial.h"
#include "Driver_USART.h"
#include "cmsis_compiler.h"

#include "WiFi_ESP8266_Config.h"

/* Serial buffer sizes */
#ifndef SERIAL_TXBUF_SZ
#define SERIAL_TXBUF_SZ   512
#endif

#ifndef SERIAL_RXBUF_SZ
#define SERIAL_RXBUF_SZ   512
#endif

/* Expansion macro used to create CMSIS Driver references */
#define EXPAND_SYMBOL(name, port) name##port
#define CREATE_SYMBOL(name, port) EXPAND_SYMBOL(name, port)

/* CMSIS-Driver USART reference (Driver_USART#) */
#define CMSIS_USART_DRIVER        CREATE_SYMBOL(Driver_USART, WIFI_ESP8266_SERIAL_DRIVER)

/* Extern CMSIS-Driver USART */
extern ARM_DRIVER_USART           CMSIS_USART_DRIVER;
#define pDrvUART                 &CMSIS_USART_DRIVER

/* Static functions */
static void UART_Callback (uint32_t event);

typedef struct {
  ARM_DRIVER_USART *drv;  /* UART driver */
  uint32_t rxc;           /* Rx buffer count */
  uint32_t rxi;           /* Rx buffer index */
  uint32_t txi;           /* Tx buffer index */
  uint8_t  txb;           /* Tx busy flag    */
  uint8_t  r[3];          /* Reserved        */
} SERIAL_COM;

static uint8_t RxBuf[SERIAL_RXBUF_SZ];
static uint8_t TxBuf[SERIAL_TXBUF_SZ];

static SERIAL_COM Com;

/**
  Initialize serial interface.

  \return 0:ok, 1:error
*/
int32_t Serial_Initialize (void) {
  int32_t err;

  err = 1;

  /* Initialize serial control structure */
  Com.drv = pDrvUART;
  Com.rxc = 0U;
  Com.rxi = 0U;
  Com.txi = 0U;
  Com.txb = 0U;

  /* Initialize serial driver */
  if (Com.drv->Initialize (&UART_Callback) == ARM_DRIVER_OK) {

    Com.drv->PowerControl (ARM_POWER_FULL);

    /* Configure UART mode: 8 bits, no parity, 1 stop bit, no flow control, 9600 bps */
    Com.drv->Control (ARM_USART_MODE_ASYNCHRONOUS |
                      ARM_USART_DATA_BITS_8       |
                      ARM_USART_PARITY_NONE       |
                      ARM_USART_STOP_BITS_1       |
                      ARM_USART_FLOW_CONTROL_NONE, 9600);

    /* Enable TX output */
    Com.drv->Control(ARM_USART_CONTROL_TX, 1);

    /* Enable RX output */
    Com.drv->Control(ARM_USART_CONTROL_RX, 1);

    /* Start serial receive */
    if (Com.drv->Receive (&RxBuf[0], SERIAL_RXBUF_SZ) == ARM_DRIVER_OK) {
      err = 0U;
    }
  }

  return (err);
}


/**
  Uninitialize serial interface.

  \return 0:ok
*/
int32_t Serial_Uninitialize (void) {

  Com.drv->PowerControl (ARM_POWER_OFF);
  Com.drv->Uninitialize ();

  memset (RxBuf, 0x00, SERIAL_RXBUF_SZ);
  memset (TxBuf, 0x00, SERIAL_TXBUF_SZ);

  return (0);
}

/**
  Set serial interface baudrate.
  
  \param[in]  baudrate    desired baudrate
  \return 0:ok, 1:error
*/
int32_t Serial_SetBaudrate (uint32_t baudrate) {
  int32_t err, status;

  status = Com.drv->Control (ARM_USART_ABORT_RECEIVE, NULL);

  Com.rxc = 0U;
  Com.rxi = 0U;
  Com.txi = 0U;
  Com.txb = 0U;

  if (status == ARM_DRIVER_OK) {
    /* Configure UART mode: 8 bits, no parity, 1 stop bit, no flow control, 9600 bps */
    status = Com.drv->Control (ARM_USART_MODE_ASYNCHRONOUS |
                               ARM_USART_DATA_BITS_8       |
                               ARM_USART_PARITY_NONE       |
                               ARM_USART_STOP_BITS_1       |
                               ARM_USART_FLOW_CONTROL_NONE, baudrate);

    if (status == ARM_DRIVER_OK) {
      /* Enable TX output */
      Com.drv->Control(ARM_USART_CONTROL_TX, 1);

      /* Enable RX output */
      Com.drv->Control(ARM_USART_CONTROL_RX, 1);

      /* Start serial receive */
      status = Com.drv->Receive (&RxBuf[0], SERIAL_RXBUF_SZ);
    }
  }

  if (status == ARM_DRIVER_OK) {
    err = 0;
  } else {
    err = 1;
  }

  return (err);
}

/**
  Get number of bytes free in transmit buffer.

  \return number of bytes
*/
uint32_t Serial_GetTxFree (void) {
  uint32_t n;

  if (Com.txb != 0U) {
    n = 0;
  } else {
    n = SERIAL_TXBUF_SZ;
  }

  return (n);
}

/**
  Try to send len of characters from the specified buffer.

  If there is not enough space in the transmit buffer, number
  of characters sent will be less than specified with len.

  \return number of bytes actually sent or -1 in case of error
*/
int32_t Serial_SendBuf (const uint8_t *buf, uint32_t len) {
  uint32_t cnt;
  int32_t  n;
  int32_t  stat;

  cnt = len;

  if (cnt > SERIAL_TXBUF_SZ) {
    cnt = SERIAL_TXBUF_SZ;
  }

  memcpy (TxBuf, buf, cnt);

  stat = Com.drv->Send (&TxBuf[0], cnt);

  if (stat == ARM_DRIVER_OK) {
    Com.txb = 1U;
    n = (int32_t)cnt;
  }
  else {
    Com.txb = 0U;
    n = -1;
  }

  return n;
}


/**
  Read len characters from the serial receive buffers and put them into buffer buf.

  \return number of characters read
*/
int32_t Serial_ReadBuf(uint8_t *buf, uint32_t len) {
  uint32_t n;
  uint32_t i, k;

  n  = Com.rxc + Com.drv->GetRxCount();
  n -= Com.rxi;

  if (n > len) {
   n = len;
  }

  for (i = 0U; i < n; i++) {
    k  = Com.rxi++;
    k &= SERIAL_RXBUF_SZ-1;

    buf[i] = RxBuf[k];
  }

  return (int32_t)n;
}


/**
  Retrieve total number of bytes to read
*/
uint32_t Serial_GetRxCount(void) {
  uint32_t n;
  uint32_t u;

  u  = Com.drv->GetRxCount();
  n  = Com.rxc + u;
  n -= Com.rxi;

  return (n);
}

uint32_t Serial_GetTxCount(void) {
  uint32_t n;

  n = Com.drv->GetTxCount();
  
  return (n);
}


/**
  Callback from the CMSIS USART driver
*/
static void UART_Callback (uint32_t event) {
  int32_t stat;
  uint32_t flags;

  flags = 0U;

  if (event & (ARM_USART_EVENT_RX_TIMEOUT | ARM_USART_EVENT_RECEIVE_COMPLETE)) {
    flags |= SERIAL_CB_RX_DATA_AVAILABLE;

    if (event & ARM_USART_EVENT_RECEIVE_COMPLETE) {

      /* Initiate new receive */
      stat = Com.drv->Receive (&RxBuf[0], SERIAL_RXBUF_SZ);

      if (stat != ARM_DRIVER_OK) {
        flags |= SERIAL_CB_RX_ERROR;
      }

      /* Increment counter of received bytes */
      Com.rxc += SERIAL_RXBUF_SZ;
    }
  }

  if (event & ARM_USART_EVENT_SEND_COMPLETE) {
    flags |= SERIAL_CB_TX_DATA_COMPLETED;

    /* Clear tx busy flag */
    Com.txb = 0U;
  }

  /* Send events */
  Serial_Cb (flags);
}

/**
  Event callback.
*/
__WEAK void Serial_Cb (uint32_t event) {
  (void)event;
}
