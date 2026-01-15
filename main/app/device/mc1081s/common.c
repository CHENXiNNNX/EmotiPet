/***********************************************************************************************************************
    @file    common.c
    @author  FAE Team
    @date    2-2025
    @brief   THIS FILE PROVIDES ALL THE SYSTEM FUNCTIONS.
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
  
#include "common.h"
#include "i2c_adapter.h"

/**-----------------------------------------------------------------------
  * @brief  MC no parameter instruction transfer function (for sending only instructions, such as temperature measurement and capacitance conversion)
  * @param  DeviceAddr  address  *Buff  Data storage array  Size  Total length of data sent
  * @retval Whether the transmission was successful
-------------------------------------------------------------------------*/
unsigned char CAP_AFE_Transmit(unsigned char DeviceAddr, MC1081_REG RegAddr, unsigned char Data)
{
  return write_i2c(DeviceAddr, RegAddr, Data);	
}

/* Application assure size>=1. */
/**-----------------------------------------------------------------------
  * @brief  Data receiving function
  * @param  DeviceAddr  address  *Buff  Data storage array  Size  Total length of data sent
  * @retval Whether the transmission was successful
-------------------------------------------------------------------------*/
unsigned char CAP_AFE_Receive(unsigned char DeviceAddr, MC1081_REG RegAddr,unsigned char *pData,unsigned char size)
{
  return read_i2c(DeviceAddr, RegAddr, pData, size);	
}

/***********************************************************************************************************************
  * @brief  Initialize i2c bus with external bus handle
  * @note   none
  * @param  bus_handle External I2C master bus handle (can be NULL)
  * @retval none
  *********************************************************************************************************************/
void config_i2c_bus_with_handle(void* bus_handle)
{
  GPIOI2C_Bus_Init_WithHandle(bus_handle);
}

/***********************************************************************************************************************
  * @brief  Initialize i2c bus
  * @note   none
  * @param  none
  * @retval none
  *********************************************************************************************************************/
void config_i2c_bus(void)
{
  config_i2c_bus_with_handle(NULL);
}
/***********************************************************************************************************************
  * @brief  Initialize CAP AFE
  * @note   none
  * @param  MC1081_InitStructure
  * @retval none
  *********************************************************************************************************************/
 void Cap_Afe_Init(MC1081_InitStructure* Init_Structure)
 {
    config_i2c_bus();
    if(Init_Structure -> MC1081_OSC_MODE == OSC2)
    {
          /* Configure external oscillator frequency division coefficient */
          Init_Structure -> MC1081_FINDIV = FIN_DIV_8;
          /* Configure internal reference clock frequency division coefficient */
          Init_Structure -> MC1081_FREFDIV = FREF_DIV_1;	
          /* Configure differential amplitude */
          Init_Structure -> MC1081_OSC2_AMPLITUDE = OSC2_AMPLITUDE_1p2V;
          /* Configure drive current */
          Init_Structure -> MC1081_DRIVEI = DRIVE_I_42UA;
          /* Select measurement channels
          --------------------------------
            RSV |CH_REF|CH4|CH3|CH2|CH1|CH0
          --------------------------------
            7:6 |  5   | 4 | 3 | 2 | 1 | 0 
          --------------------------------
             00 |  1   | 1 | 1 | 1 | 1 | 1 
          --------------------------------
          */
          Init_Structure -> MC1081_OSC2_CHANNEL = OSC2_Channel_0|OSC2_Channel_1|OSC2_Channel_2|OSC2_Channel_3|OSC2_Channel_4| 
                                                  OSC2_Ref_Channel;
                              
          /* Configure Fin measurement cycle count */
          Init_Structure -> MC1081_CNT_CFG = 0xFF;
    }
    else if (Init_Structure -> MC1081_OSC_MODE == OSC1)
    {
          /* Configure external oscillator frequency divider */
          Init_Structure -> MC1081_FINDIV = FIN_DIV_8;
          /* Configure internal reference clock frequency divider */
          Init_Structure -> MC1081_FREFDIV = FREF_DIV_1;	
          /* Configure single-ended amplitude */
          Init_Structure -> MC1081_OSC1_AMPLITUDE = OSC1_AMPLITUDE_1p2V;  // 1.2V
          /* Configure drive current */
          Init_Structure -> MC1081_DRIVEI = DRIVE_I_16UA;  // 16uA
          /* Select measurement channels
          ---------------------------------------------------
          RSV |CH_REF|CH9|CH8|CH7|CH6|CH5|CH4|CH3|CH2|CH1|CH0
          ---------------------------------------------------
          15:11|  10  | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
          ---------------------------------------------------
          00000|   1  | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 
          ---------------------------------------------------
          */
          Init_Structure -> MC1081_OSC1_CHANNEL = OSC1_Channel_0|OSC1_Channel_1|OSC1_Channel_2|OSC1_Channel_3|OSC1_Channel_4|
                                                  OSC1_Channel_5|OSC1_Channel_6|OSC1_Channel_7|OSC1_Channel_8|OSC1_Channel_9|
                                                  OSC1_Ref_Channel;
          /* Configure Fin measurement cycle count */
          Init_Structure -> MC1081_CNT_CFG = 0x0A;  // 周期改为0x0A (10)
    }
   /*	CAP_AFE initialization	*/
   Registers_Init(Init_Structure);
 }

/***********************************************************************************************************************
  * @brief  Initialize CAP AFE with external I2C bus handle
  * @note   none
  * @param  MC1081_InitStructure
  * @param  i2c_bus_handle External I2C master bus handle (can be NULL)
  * @retval none
  *********************************************************************************************************************/
 void Cap_Afe_Init_WithHandle(MC1081_InitStructure* Init_Structure, void* i2c_bus_handle)
 {
    config_i2c_bus_with_handle(i2c_bus_handle);
    if(Init_Structure -> MC1081_OSC_MODE == OSC2)
    {
          /* Configure external oscillator frequency division coefficient */
          Init_Structure -> MC1081_FINDIV = FIN_DIV_8;
          /* Configure internal reference clock frequency division coefficient */
          Init_Structure -> MC1081_FREFDIV = FREF_DIV_1;	
          /* Configure differential amplitude */
          Init_Structure -> MC1081_OSC2_AMPLITUDE = OSC2_AMPLITUDE_1p2V;
          /* Configure drive current */
          Init_Structure -> MC1081_DRIVEI = DRIVE_I_42UA;
          /* Select measurement channels
          --------------------------------
            RSV |CH_REF|CH4|CH3|CH2|CH1|CH0
          --------------------------------
            7:6 |  5   | 4 | 3 | 2 | 1 | 0 
          --------------------------------
             00 |  1   | 1 | 1 | 1 | 1 | 1 
          --------------------------------
          */
          Init_Structure -> MC1081_OSC2_CHANNEL = OSC2_Channel_0|OSC2_Channel_1|OSC2_Channel_2|OSC2_Channel_3|OSC2_Channel_4| 
                                                  OSC2_Ref_Channel;
                              
          /* Configure Fin measurement cycle count */
          Init_Structure -> MC1081_CNT_CFG = 0xFF;
    }
    else if (Init_Structure -> MC1081_OSC_MODE == OSC1)
    {
          /* Configure external oscillator frequency divider */
          Init_Structure -> MC1081_FINDIV = FIN_DIV_8;
          /* Configure internal reference clock frequency divider */
          Init_Structure -> MC1081_FREFDIV = FREF_DIV_1;	
          /* Configure single-ended amplitude */
          Init_Structure -> MC1081_OSC1_AMPLITUDE = OSC1_AMPLITUDE_1p2V;  // 1.2V
          /* Configure drive current */
          Init_Structure -> MC1081_DRIVEI = DRIVE_I_16UA;  // 16uA
          /* Select measurement channels
          ---------------------------------------------------
          RSV |CH_REF|CH9|CH8|CH7|CH6|CH5|CH4|CH3|CH2|CH1|CH0
          ---------------------------------------------------
          15:11|  10  | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
          ---------------------------------------------------
          00000|   1  | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 
          ---------------------------------------------------
          */
          Init_Structure -> MC1081_OSC1_CHANNEL = OSC1_Channel_0|OSC1_Channel_1|OSC1_Channel_2|OSC1_Channel_3|OSC1_Channel_4|
                                                  OSC1_Channel_5|OSC1_Channel_6|OSC1_Channel_7|OSC1_Channel_8|OSC1_Channel_9|
                                                  OSC1_Ref_Channel;
          /* Configure Fin measurement cycle count */
          Init_Structure -> MC1081_CNT_CFG = 0x0A;  // 周期改为0x0A (10)
    }
   /*	CAP_AFE initialization	*/
   Registers_Init(Init_Structure);
 }
 
 
 
 /*******************************************************************
   * @brief  initialization Registers.
   * @param  MC1081_InitStructure.
   * @retval None.
  ******************************************************************/
 void Registers_Init(MC1081_InitStructure* Init_Structure)
 {
   unsigned char div_value;
   unsigned char cnt_value;
   unsigned char cfg_value;
   unsigned char shield_value;
   unsigned short ch_sel;
   div_value = (Init_Structure->MC1081_FINDIV) <<4| Init_Structure->MC1081_FREFDIV ;
   cnt_value	= Init_Structure -> MC1081_CNT_CFG;
   shield_value = Init_Structure ->MC1081_SHLD_CFG;
	 
   /*  softreset  */
   CAP_AFE_Transmit(I2C_ADDR, MCP1081_RESET, 0x7A);
   CAP_AFE_Transmit(I2C_ADDR, DIV_CFG, div_value);
   CAP_AFE_Transmit(I2C_ADDR, CNT_CFG, cnt_value);
   CAP_AFE_Transmit(I2C_ADDR, SHLD_CFG, 0x40);
   if(Init_Structure ->MC1081_OSC_MODE == OSC1)
   {
	     cfg_value = (Init_Structure -> MC1081_DRIVEI)|(Init_Structure -> MC1081_OSC1_AMPLITUDE )<<4|OSC1_LDO_H;
	     ch_sel	= Init_Structure -> MC1081_OSC1_CHANNEL;
       CAP_AFE_Transmit(I2C_ADDR, OSC1_CHS_MSB, ch_sel>>8);
       CAP_AFE_Transmit(I2C_ADDR, OSC1_CHS_LSB, ch_sel);
       CAP_AFE_Transmit(I2C_ADDR, OSC1_CFG, cfg_value);
       CAP_AFE_Transmit(I2C_ADDR, SHLD_CFG, shield_value);
   }
   else
   {
	     ch_sel	= Init_Structure -> MC1081_OSC2_CHANNEL;
	     cfg_value = (Init_Structure -> MC1081_DRIVEI)|(Init_Structure -> MC1081_OSC2_AMPLITUDE )<<4|OSC1_LDO_H;
       CAP_AFE_Transmit(I2C_ADDR, OSC2_DCHS, ch_sel);
       CAP_AFE_Transmit(I2C_ADDR, OSC2_CFG, cfg_value);
   }
 }

 
/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/********************************************** (C) Copyright MySentech **********************************************/

