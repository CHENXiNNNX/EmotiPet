#include "encoder.hpp"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_jpeg_common.h>
#include <esp_jpeg_enc.h>
#include <cstdlib>
#include <cstring>

static const char* const TAG = "JPEGEncoder";

namespace app
{
    namespace tool
    {
        namespace jpeg
        {
            namespace encode
            {

                // 内存分配辅助函数
                static void* allocateAligned(size_t size, bool use_psram)
                {
                    if (use_psram)
                    {
                        // 优先使用 PSRAM
                        void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                        if (ptr)
                        {
                            return ptr;
                        }
                        ESP_LOGW(TAG, "PSRAM 分配失败，回退到 SRAM");
                    }
                    // 使用 SRAM
                    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                }

                EncodeResult encodeYUV422ToJPEG(const uint8_t* yuv_data,
                                                 uint16_t       width,
                                                 uint16_t       height,
                                                 const EncodeConfig* config)
                {
                    EncodeResult result;

                    if (!yuv_data || width == 0 || height == 0)
                    {
                        ESP_LOGE(TAG, "无效参数: yuv_data=%p, width=%u, height=%u",
                                 yuv_data, width, height);
                        return result;
                    }

                    // 使用默认配置或用户配置
                    EncodeConfig cfg;
                    if (config)
                    {
                        cfg = *config;
                    }

                    // 限制质量范围
                    if (cfg.quality < 1)
                        cfg.quality = 1;
                    if (cfg.quality > 100)
                        cfg.quality = 100;

                    // 验证 YUV422 数据长度
                    size_t expected_len = (size_t)width * (size_t)height * 2;
                    ESP_LOGD(TAG, "编码 YUV422: %ux%u, 期望长度=%u 字节, 质量=%u",
                             width, height, (unsigned int)expected_len, cfg.quality);

                    // 分配对齐的输入缓冲区（16字节对齐，提升性能）
                    size_t input_size = expected_len;
                    uint8_t* aligned_input = (uint8_t*)jpeg_calloc_align(input_size, 16);
                    if (!aligned_input)
                    {
                        ESP_LOGE(TAG, "分配输入缓冲区失败: %u 字节", (unsigned int)input_size);
                        return result;
                    }

                    // 拷贝 YUV422 数据到对齐缓冲区
                    memcpy(aligned_input, yuv_data, input_size);

                    // 配置 JPEG 编码器
                    jpeg_enc_config_t enc_cfg = DEFAULT_JPEG_ENC_CONFIG();
                    enc_cfg.width      = width;
                    enc_cfg.height     = height;
                    enc_cfg.src_type   = JPEG_PIXEL_FORMAT_YCbYCr; // YUV422 格式
                    enc_cfg.subsampling = JPEG_SUBSAMPLE_420;      // 4:2:0 色度抽样
                    enc_cfg.quality    = cfg.quality;
                    enc_cfg.rotate     = JPEG_ROTATE_0D;
                    enc_cfg.task_enable = false; // 同步编码

                    // 打开编码器
                    jpeg_enc_handle_t encoder_handle = nullptr;
                    jpeg_error_t     jpeg_err        = jpeg_enc_open(&enc_cfg, &encoder_handle);
                    if (jpeg_err != JPEG_ERR_OK || encoder_handle == nullptr)
                    {
                        jpeg_free_align(aligned_input);
                        ESP_LOGE(TAG, "jpeg_enc_open 失败: %d", (int)jpeg_err);
                        return result;
                    }

                    // 估算输出缓冲区大小：宽高 * 1.5 + 64KB
                    size_t output_capacity = (size_t)width * (size_t)height * 3 / 2 + 64 * 1024;
                    if (output_capacity < 128 * 1024)
                    {
                        output_capacity = 128 * 1024; // 最小 128KB
                    }

                    // 分配输出缓冲区
                    uint8_t* output_buffer = (uint8_t*)allocateAligned(output_capacity, cfg.use_psram);
                    if (!output_buffer)
                    {
                        jpeg_enc_close(encoder_handle);
                        jpeg_free_align(aligned_input);
                        ESP_LOGE(TAG, "分配输出缓冲区失败: %u 字节", (unsigned int)output_capacity);
                        return result;
                    }

                    // 执行 JPEG 编码
                    int output_len = 0;
                    jpeg_err = jpeg_enc_process(encoder_handle, aligned_input, (int)input_size,
                                                output_buffer, (int)output_capacity, &output_len);

                    // 清理编码器和输入缓冲区
                    jpeg_enc_close(encoder_handle);
                    jpeg_free_align(aligned_input);

                    if (jpeg_err != JPEG_ERR_OK)
                    {
                        free(output_buffer);
                        ESP_LOGE(TAG, "jpeg_enc_process 失败: %d", (int)jpeg_err);
                        return result;
                    }

                    // 编码成功，使用 RAII 智能指针管理内存
                    result = EncodeResult(output_buffer, (size_t)output_len, true);

                    ESP_LOGD(TAG, "JPEG 编码成功: %ux%u → %u 字节 (压缩比: %.2f%%, 质量=%u)",
                             width, height, (unsigned int)output_len,
                             (output_len * 100.0f) / expected_len, cfg.quality);

                    return result;
                }

            } // namespace encode
        }     // namespace jpeg
    }         // namespace tool
} // namespace app

