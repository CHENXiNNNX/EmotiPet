/***********************************************************************************************************************
    @file    mc1081_reg.h
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
#ifndef _MC1081_REG_H_
#define _MC1081_REG_H_

#define I2C_ADDR 0x70  /*!< CAP_AFE IIC_ADDR */
#define Cref 20.0      /*!< Internal reference cap */
#define Reset_cmd 0x7A /*!< CAP_AFE Reset Cmd */
#define IN_CLK 19.2    /*!< Internal reference frequency */

#define OSC1_Channel_0 1 << 0x00   /*!< OSC1 Channel 0 */
#define OSC1_Channel_1 1 << 0x01   /*!< OSC1 Channel 1 */
#define OSC1_Channel_2 1 << 0x02   /*!< OSC1 Channel 2 */
#define OSC1_Channel_3 1 << 0x03   /*!< OSC1 Channel 3 */
#define OSC1_Channel_4 1 << 0x04   /*!< OSC1 Channel 4 */
#define OSC1_Channel_5 1 << 0x05   /*!< OSC1 Channel 5 */
#define OSC1_Channel_6 1 << 0x06   /*!< OSC1 Channel 6 */
#define OSC1_Channel_7 1 << 0x07   /*!< OSC1 Channel 7 */
#define OSC1_Channel_8 1 << 0x08   /*!< OSC1 Channel 8 */
#define OSC1_Channel_9 1 << 0x09   /*!< OSC1 Channel 9 */
#define OSC1_Ref_Channel 1 << 0x0A /*!< Internal reference channel */

#define OSC2_Channel_0 1 << 0x00   /*!< OSC2 Channel 0 */
#define OSC2_Channel_1 1 << 0x01   /*!< OSC2 Channel 1 */
#define OSC2_Channel_2 1 << 0x02   /*!< OSC2 Channel 2 */
#define OSC2_Channel_3 1 << 0x03   /*!< OSC2 Channel 3 */
#define OSC2_Channel_4 1 << 0x04   /*!< OSC2 Channel 4 */
#define OSC2_Ref_Channel 1 << 0x05 /*!< Internal reference channel */

typedef enum
{
    FIN_DIV_1  = 0x00, /*!< External oscillator frequency no division */
    FIN_DIV_2  = 0x01, /*!< External oscillator frequency divided by 2 */
    FIN_DIV_4  = 0x02, /*!< External oscillator frequency divided by 4 */
    FIN_DIV_8  = 0x03, /*!< External oscillator frequency divided by 8 */
    FIN_DIV_16 = 0x04, /*!< External oscillator frequency divided by 16 */
    FIN_DIV_32 = 0x05, /*!< External oscillator frequency divided by 32 */
    FIN_DIV_64 = 0x06, /*!< External oscillator frequency divided by 64 */
} MC1081_findiv_config;

typedef enum
{

    FREF_DIV_1 = 0x00, /*!< Internal reference clock no division */
    FREF_DIV_2 = 0x01, /*!< Internal reference clock divided by 2 */
    FREF_DIV_4 = 0x02, /*!< Internal reference clock divided by 4 */
    FREF_DIV_8 = 0x03, /*!< Internal reference clock divided by 8 */

} MC1081_frefdiv_config;

typedef enum
{

    DRIVE_I_4UA    = 0x00, /*!< Drive current 4uA */
    DRIVE_I_8UA    = 0x01, /*!< Drive current 8uA */
    DRIVE_I_16UA   = 0x02, /*!< Drive current 16uA */
    DRIVE_I_42UA   = 0x03, /*!< Drive current 42uA */
    DRIVE_I_100UA  = 0x04, /*!< Drive current 100uA */
    DRIVE_I_250UA  = 0x05, /*!< Drive current 250uA */
    DRIVE_I_500UA  = 0x06, /*!< Drive current 500uA */
    DRIVE_I_1000UA = 0x07, /*!< Drive current 1000uA */
    DRIVE_I_2000UA = 0x08  /*!< Drive current 2000uA */
} MC1081_driver_i_config;

typedef enum
{
    OSC1_AMPLITUDE_0p2V     = 0x00, /*!< Single-ended amplitude 0.2V with internal LDO power */
    OSC1_AMPLITUDE_0p4V     = 0x01, /*!< Single-ended amplitude 0.4V with internal LDO power */
    OSC1_AMPLITUDE_0p8V     = 0x02, /*!< Single-ended amplitude 0.8V with internal LDO power */
    OSC1_AMPLITUDE_1p2V     = 0x03, /*!< Single-ended amplitude 1.2V with internal LDO power */
    OSC1_AMPLITUDE_VDD_2p2V = 0x04, /*!< Single-ended amplitude VDD - 2.2V with VDD power */
    OSC1_AMPLITUDE_VDD_1p6V = 0x05, /*!< Single-ended amplitude VDD - 1.6V with VDD power */
    OSC1_AMPLITUDE_VDD_1p2V = 0x06, /*!< Single-ended amplitude VDD - 1.2V with VDD power */
    OSC1_AMPLITUDE_VDD_0p8V = 0x07, /*!< Single-ended amplitude VDD - 0.8V with VDD power */
} MC1081_osc1_amplitude_config;

typedef enum
{
    OSC2_AMPLITUDE_0p4V = 0x00, /*!< Differential amplitude 0.4V with internal LDO power */
    OSC2_AMPLITUDE_0p8V = 0x01, /*!< Differential amplitude 0.8V with internal LDO power */
    OSC2_AMPLITUDE_1p2V = 0x02, /*!< Differential amplitude 1.2V with internal LDO power */
    OSC2_AMPLITUDE_1p6V = 0x03, /*!< Differential amplitude 1.6V with internal LDO power */
    OSC2_AMPLITUDE_2p0V = 0x04, /*!< Differential amplitude 2.0V with internal LDO power */
    OSC2_AMPLITUDE_2p4V = 0x05  /*!< Differential amplitude 2.4V with internal LDO power */
} MC1081_osc2_amplitude_config;

typedef enum
{
    OSC1 = 0, /*!< Single-ended measurement mode */
    OSC2 = 1  /*!< Differential measurement mode */
} MC1081_osc_mode;

typedef struct
{
    unsigned char MC1081_CNT_CFG; /*!< Configure FIN measurement cycle count, range: 0-0xFF */
    MC1081_findiv_config  MC1081_FINDIV;   /*!< Configure FIN signal division ratio */
    MC1081_frefdiv_config MC1081_FREFDIV;  /*!< Configure Fref division ratio */
    MC1081_osc_mode       MC1081_OSC_MODE; /*!< Configure measurement mode */
    MC1081_osc1_amplitude_config
        MC1081_OSC1_AMPLITUDE; /*!< Configure single-ended measurement amplitude */
    MC1081_osc2_amplitude_config
        MC1081_OSC2_AMPLITUDE;            /*!< Configure differential measurement amplitude */
    MC1081_driver_i_config MC1081_DRIVEI; /*!< Configure drive current */
    unsigned short MC1081_OSC1_CHANNEL; /*!< Configure single-ended measurement channel selection */
    unsigned char  MC1081_OSC2_CHANNEL; /*!< Configure differential measurement channel selection */
    unsigned char  MC1081_SHLD_CFG;     /*!< Configure shield */
} MC1081_InitStructure;

typedef enum
{

    OS_SD_Continuous_Trans = 0x00, /*!< Enable periodic conversion */
    OS_SD_Stop_Trans       = 0x01, /*!< Stop conversion */
    OS_SD_Single_Trans     = 0x03, /*!< Single conversion */
} MeasureMode_TypeDef;

typedef struct
{
    unsigned short data_ch[5]; /*!< Differential channel data values */
    unsigned short data_ref;   /*!< Reference channel data value */
    float          freq_ch[5]; /*!< Differential channel frequency values */
    float          freq_ref;   /*!< Reference channel frequency value */
    float          cap_ch[5];  /*!< Differential channel capacitance values */
    float          temp;       /*!< Temperature value */
} CAP_AFE_DoubleEnded;

typedef struct
{
    unsigned short data_ch[10]; /*!< Single-ended channel data values */
    unsigned short data_ref;    /*!< Reference channel data value */
    float          freq_ch[10]; /*!< Single-ended channel frequency values */
    float          freq_ref;    /*!< Reference channel frequency value */
    float          cap_ch[10];  /*!< Single-ended channel capacitance values */
    float          temp;        /*!< Temperature value */
} CAP_AFE_SingleEnded;

typedef enum
{
    T_MSB = 0x00, // Temperature conversion data MSB
    T_LSB = 0x01, // Temperature conversion data LSB

    D0_MSB = 0x02, // Single/differential channel 0 conversion data MSB
    D0_LSB = 0x03, // Single/differential channel 0 conversion data LSB
    D1_MSB = 0x04, // Single/differential channel 1 conversion data MSB
    D1_LSB = 0x05, // Single/differential channel 1 conversion data LSB
    D2_MSB = 0x06, // Single/differential channel 2 conversion data MSB
    D2_LSB = 0x07, // Single/differential channel 2 conversion data LSB
    D3_MSB = 0x08, // Single/differential channel 3 conversion data MSB
    D3_LSB = 0x09, // Single/differential channel 3 conversion data LSB
    D4_MSB = 0x0A, // Single/differential channel 4 conversion data MSB
    D4_LSB = 0x0B, // Single/differential channel 4 conversion data LSB

    D5_MSB   = 0x0C, // Single-ended channel 5 conversion data MSB
    D5_LSB   = 0x0D, // Single-ended channel 5 conversion data LSB
    D6_MSB   = 0x0E, // Single-ended channel 6 conversion data MSB
    D6_LSB   = 0x0F, // Single-ended channel 6 conversion data LSB
    D7_MSB   = 0x10, // Single-ended channel 7 conversion data MSB
    D7_LSB   = 0x11, // Single-ended channel 7 conversion data LSB
    D8_MSB   = 0x12, // Single-ended channel 8 conversion data MSB
    D8_LSB   = 0x13, // Single-ended channel 8 conversion data LSB
    D9_MSB   = 0x14, // Single-ended channel 9 conversion data MSB
    D9_LSB   = 0x15, // Single-ended channel 9 conversion data LSB
    DREF_MSB = 0x16, // Single/differential reference channel conversion data MSB
    DREF_LSB = 0x17, // Single/differential reference channel conversion data LSB

    OSC1_OF_MSB = 0x18, // Single-ended mode overflow flag MSB
    OSC1_OF_LSB = 0x19, // Single-ended mode overflow flag LSB
    OSC2_OF     = 0x1A, // Differential mode overflow flag

    STATUS = 0x1B, // Data conversion flag and overflow flag clear
    T_CMD  = 0x1C, // Temperature measurement control
    C_CMD  = 0x1D, // Capacitance measurement control

    CNT_CFG = 0x1E, // FIN count cycle setting, range 0~255
    DIV_CFG = 0x1F, // Clock signal division configuration

    OSC1_CHS_MSB = 0x20, // Single-ended mode channel selection
    OSC1_CHS_LSB = 0x21, // Single-ended mode channel selection
    OSC1_MCHS    = 0x22, // Single-ended mutual capacitance channel selection
    OSC1_CFG     = 0x23, // Single-ended mode parameter configuration

    OSC2_DCHS     = 0x24, // Differential mode channel selection
    OSC2_CFG      = 0x25, // Differential mode parameter configuration
    SHLD_CFG      = 0x26, // Shield configuration
    MCP1081_RESET = 0x69, // Software reset
    CHIP_ID_MSB   = 0x7E, // Default value 0x10: Chip ID, read-only
    CHIP_ID_LSB   = 0x7F, // Default value 0x81: Chip ID, read-only
} MC1081_REG;

#define OF_CLEAR 0x10

#define TCV_POS (7)
#define TCV_3MS (0x00U << TCV_POS)
#define TCV_1p7MS (0x01U << TCV_POS)

#define STC 0x01 // Temperature One shot Conversation

#define DIV_SETTLE_POS (7)
#define DIV_SETTLE_1 (0x00U << DIV_SETTLE_POS)
#define DIV_SETTLE_4 (0x01U << DIV_SETTLE_POS)

#define DIV_SYNC_POS (3)
#define DIV_SYNC_EN (0x01U << DIV_SYNC_POS)
#define DIV_SYNC_DIS (0x00U << DIV_SYNC_POS)

#define OSC1_LDO_H (0x01U << 7)
#define OSC1_LDO_L (0x00U << 7)

#define CS_POS (6)
#define CS_HZ (0 << CS_POS)   // Sensor pads high resistance
#define CS_GND (1 << CS_POS)  // Sensor pads connect to GND
#define CS_SHLD (2 << CS_POS) // Sensor pads connect to SHLD

#define SHLD_EN 0x01  // Open shilde driver
#define SHLD_DIS 0x00 // Close shilde driver

#define SHLD_HP 0x02 // High power driver mode
#define SHLD_LP 0x00 // Low power driver mode

#define OSC_SEL_POS (7)

#define OSC1_EN (0x00U << OSC_SEL_POS) // OSC1 enable
#define OSC2_EN (0x01U << OSC_SEL_POS) // OSC2 enable

#define SLEEP_POS (6)
#define SLEEP_EN (0x01U << SLEEP_POS)  // Enable sleep mode
#define SLEEP_DIS (0x00U << SLEEP_POS) // Disable sleep mode

#define CAVG_POS (4)
#define CAVG_1 (0x00U << CAVG_POS)  // Average=1
#define CAVG_4 (0x01U << CAVG_POS)  // Average=4
#define CAVG_8 (0x02U << CAVG_POS)  // Average=8
#define CAVG_32 (0x03U << CAVG_POS) // Average=32

#define CR_POS (2)
#define CR_10S (0x00U << CR_POS)  // Cycle period=10s
#define CR_1S (0x01U << CR_POS)   // Cycle period=1s
#define CR_0p1S (0x02U << CR_POS) // Cycle period=0.1s
#define CR_0s (0x03U << CR_POS)   // Cycle period=0s (Continuous mode)

#define OS_SD_CC 0x00   // Cycle Conversation
#define OS_SD_STOP 0x01 // Stop
#define OS_SD_ONE 0x03  // One shot Conversation

unsigned char CAP_AFE_Transmit(unsigned char DeviceAddr, MC1081_REG RegAddr, unsigned char Data);
unsigned char CAP_AFE_Receive(unsigned char DeviceAddr, MC1081_REG RegAddr, unsigned char* pData,
                              unsigned char size);
/*******************************************************************
 * @brief  initialization Registers.
 * @param  Cap_Structure.
 * @retval None.
 ******************************************************************/
void Registers_Init(MC1081_InitStructure* Init_Structure);

#endif

/********************************************** (C) Copyright MySentech
 * **********************************************/
