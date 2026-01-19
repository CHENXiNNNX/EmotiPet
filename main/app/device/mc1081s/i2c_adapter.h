/***********************************************************************************************************************
    @file    i2c_adapter.h
    @author  FAE Team
    @date    2-2025
    @brief   I2C adapter for ESP-IDF platform
  **********************************************************************************************************************
    @attention

    <h2><center>&copy; Copyright(c) <2025> <MySentech></center></h2>

      Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
    following conditions are met:
    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
       the following disclaimer in the documentation and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or
       promote products derived from this software without specific prior written permission.

      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *********************************************************************************************************************/
  
#ifndef _I2C_ADAPTER_H_
#define _I2C_ADAPTER_H_

#include "mc1081_reg.h"
#include <stdint.h>

/* I2C transaction result */
#define GPIOI2C_XFER_LASTNACK     0x00    /*!< No error */
#define GPIOI2C_XFER_ADDRNACK     0x01    /*!< No Device */
#define GPIOI2C_XFER_ABORTNACK    0x02    /*!< NACK before last byte */
#define GPIOI2C_XFER_LASTACK      0x04    /*!< ACK last byte */
#define GPIOI2C_XFER_BUSERR       0x10    /*!< Bus error */

/**-----------------------------------------------------------------------
  * @brief  Initialize the IIC bus (ESP-IDF implementation)
  * @param  None
  * @retval None
-------------------------------------------------------------------------*/
void GPIOI2C_Bus_Init(void);

/**-----------------------------------------------------------------------
  * @brief  Initialize the IIC bus with an existing bus handle
  * @param  bus_handle Existing I2C master bus handle (can be NULL to create new)
  * @retval None
-------------------------------------------------------------------------*/
void GPIOI2C_Bus_Init_WithHandle(void* bus_handle);

/**-----------------------------------------------------------------------
  * @brief  Write data to an I2C device
  * @param  DeviceAddr I2C device address
  * @param  RegAddr Register address
  * @param  Data Data to write
  * @retval Transmission status (0 = success, non-zero = error)
-------------------------------------------------------------------------*/
unsigned char write_i2c(unsigned char DeviceAddr, MC1081_REG RegAddr, unsigned char Data);

/**-----------------------------------------------------------------------
  * @brief  Read data from an I2C device
  * @param  DeviceAddr I2C device address
  * @param  RegAddr Register address
  * @param  pData Pointer to store received data
  * @param  size Number of bytes to read
  * @retval Transmission status (0 = success, non-zero = error)
-------------------------------------------------------------------------*/
unsigned char read_i2c(unsigned char DeviceAddr, MC1081_REG RegAddr, unsigned char *pData, unsigned char size);

/**-----------------------------------------------------------------------
  * @brief  Delay in milliseconds
  * @param  ms Number of milliseconds to delay
  * @retval None
-------------------------------------------------------------------------*/
void Delay_ms(uint32_t ms);

#endif

/********************************************** (C) Copyright MySentech **********************************************/

