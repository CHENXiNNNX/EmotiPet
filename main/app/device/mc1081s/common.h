/***********************************************************************************************************************
    @file    common.h
    @author  FAE Team
    @date    2-2025
    @brief   THIS FILE PROVIDES ALL THE SYSTEM FUNCTIONS.
  **********************************************************************************************************************
    @attention

    <h2><center>&copy; Copyright(c) <2025> <MySentech></center></h2>

      Redistribution and use in source and binary forms, with or without modification, are permitted
  provided that the following conditions are met:
    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, this list of
  conditions and the following disclaimer in the documentation and/or other materials provided with
  the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors may be used to
  endorse or promote products derived from this software without specific prior written permission.

      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
  FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
  OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *********************************************************************************************************************/

#ifndef _COMMON_H_
#define _COMMON_H_

#include "mc1081_reg.h"
#include <stdio.h>
#include <stdint.h>

#define DEBUG_EN // 启用调试输出，显示所有通道的详细数据

#if defined DEBUG_EN
#define PR(format, ...) printf(format, ##__VA_ARGS__);
#else
#define PR(format, ...)
#endif

/**-----------------------------------------------------------------------
  * @brief  MC no parameter instruction transfer function (for sending only instructions, such as
temperature measurement and capacitance conversion)
  * @param  DeviceAddr  address  *Buff  Data storage array  Size  Total length of data sent
  * @retval Whether the transmission was successful
-------------------------------------------------------------------------*/
unsigned char CAP_AFE_Transmit(unsigned char DeviceAddr, MC1081_REG RegAddr, unsigned char Data);

/* Application assure size>=1. */
/**-----------------------------------------------------------------------
  * @brief  Data receiving function
  * @param  DeviceAddr  address  *Buff  Data storage array  Size  Total length of data sent
  * @retval Whether the transmission was successful
-------------------------------------------------------------------------*/
unsigned char CAP_AFE_Receive(unsigned char DeviceAddr, MC1081_REG RegAddr, unsigned char* pData,
                              unsigned char size);

/***********************************************************************************************************************
 * @brief  Initialize CAP AFE
 * @note   none
 * @param  MCP1081_InitStructure
 * @retval none
 *********************************************************************************************************************/
void Cap_Afe_Init(MC1081_InitStructure* Init_Structure);

/***********************************************************************************************************************
 * @brief  Initialize CAP AFE with external I2C bus handle
 * @note   none
 * @param  MC1081_InitStructure
 * @param  i2c_bus_handle External I2C master bus handle (can be NULL)
 * @retval none
 *********************************************************************************************************************/
void Cap_Afe_Init_WithHandle(MC1081_InitStructure* Init_Structure, void* i2c_bus_handle);

/*******************************************************************
 * @brief  initialization Registers.
 * @param  MC1081_InitStructure.
 * @retval None.
 ******************************************************************/
void Registers_Init(MC1081_InitStructure* Init_Structure);

/***********************************************************************************************************************
 * @brief  Initialize i2c bus
 * @note   none
 * @param  none
 * @retval none
 *********************************************************************************************************************/
void config_i2c_bus(void);

/***********************************************************************************************************************
 * @brief  Initialize i2c bus with external bus handle
 * @note   none
 * @param  bus_handle External I2C master bus handle (can be NULL)
 * @retval none
 *********************************************************************************************************************/
void config_i2c_bus_with_handle(void* bus_handle);

/***********************************************************************************************************************
 * @brief  Delay in milliseconds
 * @note   none
 * @param  ms Number of milliseconds to delay
 * @retval none
 *********************************************************************************************************************/
void Delay_ms(uint32_t ms);

#endif

/**
 * @}
 */

/**
 * @}
 */

/**
 * @}
 */

/********************************************** (C) Copyright MySentech
 * **********************************************/
