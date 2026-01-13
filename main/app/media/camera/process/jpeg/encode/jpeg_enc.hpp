#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <cstdlib>

namespace app
{
    namespace media
    {
        namespace camera
        {
            namespace process
            {
                namespace jpeg
                {
                    namespace encode
                    {
                        /**
                         * @brief JPEG 数据智能指针
                         */
                        class JpegData
                        {
                        public:
                            /**
                             * @brief 自定义删除器，使用 free() 释放内存
                             */
                            struct Deleter
                            {
                                void operator()(uint8_t* ptr) const noexcept
                                {
                                    if (ptr)
                                    {
                                        std::free(ptr);
                                    }
                                }
                            };

                            using UniquePtr = std::unique_ptr<uint8_t[], Deleter>;

                            JpegData() : data_(nullptr), len_(0), success_(false) {}

                            JpegData(uint8_t* data, size_t len, bool success)
                                : data_(data ? UniquePtr(data) : UniquePtr()), len_(len),
                                  success_(success)
                            {
                            }

                            // 移动构造
                            JpegData(JpegData&& other) noexcept
                                : data_(std::move(other.data_)), len_(other.len_),
                                  success_(other.success_)
                            {
                                other.len_     = 0;
                                other.success_ = false;
                            }

                            // 移动赋值
                            JpegData& operator=(JpegData&& other) noexcept
                            {
                                if (this != &other)
                                {
                                    data_          = std::move(other.data_);
                                    len_           = other.len_;
                                    success_       = other.success_;
                                    other.len_     = 0;
                                    other.success_ = false;
                                }
                                return *this;
                            }

                            // 禁止拷贝
                            JpegData(const JpegData&)            = delete;
                            JpegData& operator=(const JpegData&) = delete;

                            /**
                             * @brief 获取原始数据指针
                             */
                            uint8_t* get() const
                            {
                                return data_.get();
                            }

                            /**
                             * @brief 获取数据长度
                             */
                            size_t len() const
                            {
                                return len_;
                            }

                            /**
                             * @brief 检查是否成功
                             */
                            bool success() const
                            {
                                return success_;
                            }

                            /**
                             * @brief 检查是否有数据
                             */
                            bool empty() const
                            {
                                return !data_ || len_ == 0;
                            }

                            /**
                             * @brief 释放数据（显式释放，通常不需要调用）
                             */
                            void reset()
                            {
                                data_.reset();
                                len_     = 0;
                                success_ = false;
                            }

                            /**
                             * @brief 转换为 bool（检查是否成功）
                             */
                            explicit operator bool() const
                            {
                                return success_;
                            }

                        private:
                            UniquePtr data_;
                            size_t    len_;
                            bool      success_;
                        };

                        /**
                         * @brief JPEG 编码结果
                         */
                        using EncodeResult = JpegData;

                        /**
                         * @brief JPEG 编码配置
                         */
                        struct EncodeConfig
                        {
                            uint8_t quality;   // JPEG 质量 (1-100)
                            bool    use_psram; // 是否使用 PSRAM 分配输出缓冲区

                            EncodeConfig() : quality(80), use_psram(true) {}
                        };

                        /**
                         * @brief 将 YUV422 格式图像编码为 JPEG
                         *
                         * @param yuv_data    YUV422 图像数据（YUYV 格式）
                         * @param width       图像宽度
                         * @param height      图像高度
                         * @param config      编码配置（可选，默认质量80）
                         * @return EncodeResult 编码结果
                         *
                         * @note YUV422 格式说明：
                         *   - 每个像素对 (Y0, Cb, Y1, Cr) 占用 4 字节
                         *   - 数据长度 = width * height * 2 字节
                         *   - 格式：YUYV (Y Cb Y Cr)
                         *
                         * @note 内存管理：
                         *   - 输入数据：不修改，不释放
                         *   - 输出数据：使用智能指针自动管理，离开作用域时自动释放
                         *   - 无需手动调用 free()，析构时自动释放内存
                         *
                         * @example
                         *   auto result = encodeYUV422ToJPEG(yuv_data, width, height);
                         *   if (result) {
                         *       // 使用 result.get() 获取数据指针
                         *       // 使用 result.len() 获取数据长度
                         *       // 离开作用域时自动释放内存
                         *   }
                         */
                        EncodeResult encodeYUV422ToJPEG(const uint8_t* yuv_data, uint16_t width,
                                                        uint16_t            height,
                                                        const EncodeConfig* config = nullptr);

                    } // namespace encode
                } // namespace jpeg
            } // namespace process
        } // namespace camera
    } // namespace media
} // namespace app
