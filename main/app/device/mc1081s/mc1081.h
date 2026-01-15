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
#ifndef _MC1081_H_
#define _MC1081_H_

#include "mc1081_reg.h"
#include "common.h"


/* Private define *****************************************************************************************************/

#define T_HIGH_RESOLUTION
						
/*******************************************************************
  * @brief  Convert and read Temperature.
  * @note  	none
  * @param  *temp.
  * @retval None.
 ******************************************************************/
int MC1081_T_Measure(float *temp);
						
/*******************************************************************
  * @brief  Convert and read double ended capacity.
  * @note  	none
  * @param  CAP_AFE_DoubleEnded.
  * @param  MCP1081_InitStructure.
  * @retval None.
 ******************************************************************/
int MC1081_OSC2_Measure(CAP_AFE_DoubleEnded * Cap_Structure,MC1081_InitStructure * init_structure);

/**-----------------------------------------------------------------------
  * @brief  Start single-ended mode measurement
  * @param  *Cap_Structure: Single-ended mode capacitor related information
  * @param  *init_structure: Capacitor initialization configuration
  * @retval Transmission success status
-------------------------------------------------------------------------*/
int MC1081_OSC1_Measure(CAP_AFE_SingleEnded * Cap_Structure,MC1081_InitStructure * init_structure);

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

/********************************************** (C) Copyright MySentech **********************************************/

