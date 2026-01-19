/***********************************************************************************************************************
    @file    mc1081.c
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
#include "mc1081.h"
#include "mc1081_reg.h"
#include "common.h"


/**-----------------------------------------------------------------------
  * @brief  Start temperature measurement
  * @param  *temp 
  * @retval Success or failure of the transmission
-------------------------------------------------------------------------*/
int MC1081_T_Measure(float *temp)
{
	int timeout = 10000;
	unsigned char status = 0x2;
	unsigned char dat[2];
	signed short tmp_dat = 0;

	CAP_AFE_Transmit(I2C_ADDR, STATUS, 0x10);	// Clear historical overflow flag

#if defined T_HIGH_RESOLUTION
	CAP_AFE_Transmit(I2C_ADDR, T_CMD, TCV_3MS|STC);	// Enable temperature high resolution conversion, completed in 3ms
	Delay_ms(3);		// Delay 3ms to wait for measurement completion

#else
	CAP_AFE_Transmit(I2C_ADDR, T_CMD, TCV_1p7MS|STC);	// Enable temperature low resolution conversion, completed in 1.7ms
	Delay_ms(2);		// Delay 2ms to wait for measurement completion
#endif

	do
	{
			CAP_AFE_Receive(I2C_ADDR, STATUS, &status, 1);
			timeout--;
	}while((status >> 1 &1) == 1 && timeout > 0);
	
	if(timeout == 0)
	{
			PR("\r\n### Temperature conversion completion flag not read, STATUS Value = %02X\r\n",status);
			return 0;
	}
	else
	{
			CAP_AFE_Receive(I2C_ADDR, T_MSB, dat, 2);
			tmp_dat = dat[0]<<8|dat[1];

			*temp= (float)tmp_dat/255.0f+28.7f;
			
	}
	PR("\r\n\r\nT:%.1f C\r\n",*temp);
	
	return 1;
}

/**-----------------------------------------------------------------------
  * @brief  Start differential mode measurement
  * @param  CAP_AFE_DoubleEnded
  * @param  MC1081_InitStructure
  * @retval Success or failure of the transmission
-------------------------------------------------------------------------*/
int MC1081_OSC2_Measure(CAP_AFE_DoubleEnded * Cap_Structure,MC1081_InitStructure * init_structure)
{
	int timeout = 10000;
	unsigned char status = 0x1;
	unsigned char dat[12];
	unsigned char OSC2_OF_FLAG = 0;
	unsigned char Fin_Div_VALUE,Fref_Div_VALUE;
	unsigned char Ch_Sel = 0;
	Ch_Sel = init_structure->MC1081_OSC2_CHANNEL;
	Fin_Div_VALUE = 1<<(init_structure ->MC1081_FINDIV);
	Fref_Div_VALUE = 1<<(init_structure ->MC1081_FREFDIV);
	/* Clear historical overflow flag */
	CAP_AFE_Transmit(I2C_ADDR, STATUS, OF_CLEAR);	
	/* Enable differential mode single conversion, sleep mode, 32 times averaging */
	CAP_AFE_Transmit(I2C_ADDR, C_CMD, OSC2_EN|SLEEP_EN|CAVG_32|OS_SD_ONE);	
	do
	{
		Delay_ms(1);
		/* Wait for conversion completion */
		CAP_AFE_Receive(I2C_ADDR, STATUS, &status, 1);
		timeout--;
	}while((status&1) == 1 && timeout > 0);
	
	if(timeout == 0)
	{
		PR("\r\n### Differential conversion completion flag not read, STATUS Value = %02X\r\n",status);
		return 0;
	}
	else
	{	
			CAP_AFE_Receive(I2C_ADDR, OSC2_OF, &OSC2_OF_FLAG, 1);
			CAP_AFE_Receive(I2C_ADDR, DREF_MSB, dat, 2);
			Cap_Structure -> data_ref = dat[0]<<8|dat[1];
			
			if(Cap_Structure -> data_ref>=65535)
			{
				PR("\r\n ### Internal channel data overflow, OSC2_OF:%02X \r\n",OSC2_OF_FLAG);
			}
			else
			{
				Cap_Structure -> freq_ref = (float)(init_structure ->MC1081_CNT_CFG) * IN_CLK * (float)Fin_Div_VALUE/(float)Fref_Div_VALUE/(float)(Cap_Structure -> data_ref);;
				PR("data_ref:%d\t\tfreq_ref:%.3f MHz\r\n",Cap_Structure -> data_ref,Cap_Structure -> freq_ref);
			}
					
				
			for(int i = 0; i < 5; i++)
			{
					if(((OSC2_OF_FLAG>>i)&1)!= 0)
					{
							PR("\r\n ### Channel %d data overflow, OSC2_OF:%02X \r\n",i,OSC2_OF_FLAG);
							continue;
					
					}
					else
					{
							CAP_AFE_Receive(I2C_ADDR, (MC1081_REG)(D0_MSB+2*i), dat, 2);
							Cap_Structure -> data_ch[i] = dat[0]<<8|dat[1];
							Cap_Structure -> freq_ch[i] = (float)(init_structure ->MC1081_CNT_CFG) * IN_CLK * (float)Fin_Div_VALUE/(float)Fref_Div_VALUE/(float)(Cap_Structure -> data_ch[i]);;
							PR("data_ch[%d]:%d \tfreq_ch[%d]:%.3f MHz\t",i,Cap_Structure -> data_ch[i],i,Cap_Structure -> freq_ch[i]);
							if((Ch_Sel&OSC2_Ref_Channel) && !(OSC2_OF_FLAG&OSC2_Ref_Channel))
							{
								Cap_Structure -> cap_ch[i] = (float)Cap_Structure -> data_ch[i] / (float)Cap_Structure -> data_ref * Cref;
								PR("cap_ch[%d]:%.3f pF\r\n",i,Cap_Structure -> cap_ch[i]);
							}
							
					}
			}
	}
	
	return 1;
}



/**-----------------------------------------------------------------------
  * @brief  Start single-ended mode measurement
  * @param  *Cap_Structure: Single-ended mode capacitor related information
  * @param  *init_structure: Capacitor initialization configuration
  * @retval Transmission success status
-------------------------------------------------------------------------*/
int MC1081_OSC1_Measure(CAP_AFE_SingleEnded * Cap_Structure,MC1081_InitStructure * init_structure)
{
	int timeout = 10000;
	unsigned char status = 0x1;
	unsigned char dat[2];
	unsigned char OSC1_OF_FLAG[2] = {0};
	unsigned char Fin_Div_VALUE,Fref_Div_VALUE;
	unsigned short  CNT_VALUE;
	unsigned short Ch_Sel = 0;
	unsigned short OF_FLAG;
	CNT_VALUE = init_structure ->MC1081_CNT_CFG;
	Fin_Div_VALUE = 1<<(init_structure ->MC1081_FINDIV);
	Fref_Div_VALUE = 1<<(init_structure ->MC1081_FREFDIV);
	Ch_Sel  = init_structure ->MC1081_OSC1_CHANNEL;
	/* Clear the history overflow flag */
	CAP_AFE_Transmit(I2C_ADDR, STATUS, OF_CLEAR);	
	/* Start single-ended mode capacitor single conversion, sleep mode, 32 times average */
	CAP_AFE_Transmit(I2C_ADDR, C_CMD, OSC1_EN|SLEEP_EN|CAVG_32|OS_SD_ONE);	
	do
	{
		Delay_ms(1);		
		/* Wait for conversion to complete */
		CAP_AFE_Receive(I2C_ADDR, STATUS, &status, 1);
		timeout--;
	}while((status&1) == 1 && timeout > 0);
	
	if(timeout == 0)
	{
			PR("\r\n### Failed to read the capacitor conversion complete flag, STATUS Value = %02X\r\n",status);
			return 0;
	}
	else
	{	
			CAP_AFE_Receive(I2C_ADDR, OSC1_OF_MSB, OSC1_OF_FLAG, 2);
			OF_FLAG = OSC1_OF_FLAG[0]<<8|OSC1_OF_FLAG[1];
			if((Ch_Sel&OSC1_Ref_Channel))
			{
				if(OF_FLAG&OSC1_Ref_Channel)
				{
					PR("\r\n ### Internal channel data overflow, OSC1_OF:%04X \r\n",(OSC1_OF_FLAG[0]<<8|OSC1_OF_FLAG[1]));
				}
				else
				{
					CAP_AFE_Receive(I2C_ADDR, DREF_MSB, dat, 2);
					Cap_Structure -> data_ref = dat[0]<<8|dat[1];
					Cap_Structure -> freq_ref = (float)(CNT_VALUE) * IN_CLK * (float)Fin_Div_VALUE/(float)Fref_Div_VALUE/(float)(Cap_Structure -> data_ref);
					//PR("data_ref:%d\tfreq_ref:%.3f MHz\r\n",Cap_Structure -> data_ref,Cap_Structure -> freq_ref);  // 注释掉参考通道调试信息
				}
					
			}	
			for(int i = 0; i < 10; i++)
			{
					if(((OF_FLAG>>i)&1)!= 0)
					{
							PR("\r\n ### Measurement channel %d has data overflow, OSC1_OF:%04X \r\n",i,(OSC1_OF_FLAG[0]<<8|OSC1_OF_FLAG[1]));
							continue;
					
					}
					else
					{
							CAP_AFE_Receive(I2C_ADDR, (MC1081_REG)(D0_MSB+2*i), dat, 2);
							Cap_Structure -> data_ch[i] = dat[0]<<8|dat[1];
							Cap_Structure -> freq_ch[i] = (float)(CNT_VALUE) * IN_CLK * (float)Fin_Div_VALUE/(float)Fref_Div_VALUE/(float)(Cap_Structure ->  data_ch[i]);
							//PR("data_ch[%d]:%d\tfreq_ch[%d]:%.3f MHz\t",i,Cap_Structure -> data_ch[i],i,Cap_Structure -> freq_ch[i]);  // 注释掉通道数据和频率调试信息
							if((Ch_Sel&OSC1_Ref_Channel) && !(OF_FLAG&OSC1_Ref_Channel))
							{
								Cap_Structure -> cap_ch[i] = (float)Cap_Structure -> data_ch[i] / (float)Cap_Structure -> data_ref * Cref;
								//PR("cap_ch[%d]:%.3f pF\r\n",i,Cap_Structure -> cap_ch[i]);  // 注释掉通道电容值调试信息
							}
							
					}
			}
	}
	
	return 1;
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

