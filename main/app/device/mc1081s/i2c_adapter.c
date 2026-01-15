/***********************************************************************************************************************
    @file    i2c_adapter.c
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

#include "i2c_adapter.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <driver/i2c_master.h>
#include <driver/gpio.h>

static const char* const TAG = "MC1081_I2C";
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t i2c_dev_handle = NULL;
static bool i2c_initialized = false;

// I2C 引脚配置（与 config.hpp 中的配置保持一致）
#define MC1081_I2C_SDA_GPIO  GPIO_NUM_17
#define MC1081_I2C_SCL_GPIO  GPIO_NUM_18

/**-----------------------------------------------------------------------
  * @brief  Initialize the IIC bus with an existing bus handle
  * @param  bus_handle Existing I2C master bus handle (can be NULL to create new)
  * @retval None
-------------------------------------------------------------------------*/
void GPIOI2C_Bus_Init_WithHandle(void* bus_handle)
{
    if (i2c_initialized) {
        ESP_LOGW(TAG, "I2C bus already initialized");
        return;
    }

    i2c_master_bus_handle_t external_bus = (i2c_master_bus_handle_t)bus_handle;

    if (external_bus != NULL) {
        // 使用外部提供的 I2C 总线句柄
        i2c_bus_handle = external_bus;
        ESP_LOGI(TAG, "Using external I2C bus handle");
    } else {
        // 创建新的 I2C 总线
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = MC1081_I2C_SDA_GPIO,
            .scl_io_num = MC1081_I2C_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags = {
                .enable_internal_pullup = true,
            },
        };

        esp_err_t ret = i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(ret));
            return;
        }
        ESP_LOGI(TAG, "Created new I2C bus");
    }

    // 创建 MC1081 设备句柄
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x70,  // MC1081 I2C address
        .scl_speed_hz = 100000,  // 100kHz
    };

    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &i2c_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        if (external_bus == NULL) {
            i2c_del_master_bus(i2c_bus_handle);
        }
        i2c_bus_handle = NULL;
        return;
    }

    i2c_initialized = true;
    ESP_LOGI(TAG, "I2C bus initialized successfully");
}

/**-----------------------------------------------------------------------
  * @brief  Initialize the IIC bus (ESP-IDF implementation)
  * @param  None
  * @retval None
-------------------------------------------------------------------------*/
void GPIOI2C_Bus_Init(void)
{
    GPIOI2C_Bus_Init_WithHandle(NULL);
}

/**-----------------------------------------------------------------------
  * @brief  Write data to an I2C device
  * @param  DeviceAddr I2C device address
  * @param  RegAddr Register address
  * @param  Data Data to write
  * @retval Transmission status (0 = success, non-zero = error)
-------------------------------------------------------------------------*/
unsigned char write_i2c(unsigned char DeviceAddr, MC1081_REG RegAddr, unsigned char Data)
{
    if (!i2c_initialized || i2c_dev_handle == NULL) {
        ESP_LOGE(TAG, "I2C not initialized");
        return GPIOI2C_XFER_BUSERR;
    }

    uint8_t write_buf[2] = {(unsigned char)RegAddr, Data};
    
    esp_err_t ret = i2c_master_transmit(i2c_dev_handle, write_buf, 2, pdMS_TO_TICKS(1000));
    
    if (ret == ESP_OK) {
        return GPIOI2C_XFER_LASTACK;
    } else if (ret == ESP_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "I2C write timeout");
        return GPIOI2C_XFER_BUSERR;
    } else {
        ESP_LOGE(TAG, "I2C write error: %s", esp_err_to_name(ret));
        return GPIOI2C_XFER_ADDRNACK;
    }
}

/**-----------------------------------------------------------------------
  * @brief  Read data from an I2C device
  * @param  DeviceAddr I2C device address
  * @param  RegAddr Register address
  * @param  pData Pointer to store received data
  * @param  size Number of bytes to read
  * @retval Transmission status (0 = success, non-zero = error)
-------------------------------------------------------------------------*/
unsigned char read_i2c(unsigned char DeviceAddr, MC1081_REG RegAddr, unsigned char *pData, unsigned char size)
{
    if (!i2c_initialized || i2c_dev_handle == NULL) {
        ESP_LOGE(TAG, "I2C not initialized");
        return GPIOI2C_XFER_BUSERR;
    }

    if (size == 0 || pData == NULL) {
        return GPIOI2C_XFER_BUSERR;
    }

    // 使用组合的写入-读取操作来确保正确的时序
    // 先写入寄存器地址，然后立即读取数据（中间没有停止条件）
    uint8_t reg_addr = (unsigned char)RegAddr;
    
    // 使用 i2c_master_transmit_receive 进行组合操作
    // 这样可以确保在写入地址后立即读取，中间没有停止条件
    esp_err_t ret = i2c_master_transmit_receive(i2c_dev_handle, 
                                                 &reg_addr, 1,  // 写入寄存器地址
                                                 pData, size,   // 读取数据
                                                 pdMS_TO_TICKS(1000));
    
    if (ret == ESP_OK) {
        // 调试：打印读取的数据（仅在调试模式下）
        #ifdef DEBUG_EN
        if (size == 1) {
            ESP_LOGD(TAG, "I2C read success: reg=0x%02X, data=0x%02X", reg_addr, pData[0]);
        } else if (size == 2) {
            ESP_LOGD(TAG, "I2C read success: reg=0x%02X, data=0x%02X%02X", reg_addr, pData[0], pData[1]);
        }
        #endif
        return GPIOI2C_XFER_LASTNACK;
    } else if (ret == ESP_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "I2C read timeout (reg=0x%02X, size=%d): %s", reg_addr, size, esp_err_to_name(ret));
        return GPIOI2C_XFER_BUSERR;
    } else {
        ESP_LOGE(TAG, "I2C read error (reg=0x%02X, size=%d): %s", reg_addr, size, esp_err_to_name(ret));
        return GPIOI2C_XFER_ADDRNACK;
    }
}

/**-----------------------------------------------------------------------
  * @brief  Delay in milliseconds
  * @param  ms Number of milliseconds to delay
  * @retval None
-------------------------------------------------------------------------*/
void Delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/********************************************** (C) Copyright MySentech **********************************************/

