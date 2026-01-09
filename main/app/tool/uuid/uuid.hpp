#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace app
{
    namespace tool
    {
        namespace uuid
        {

            constexpr size_t UUID_BYTE_SIZE   = 16;
            constexpr size_t UUID_STRING_SIZE = 37; // 36 字符 + null 终止符

            /**
             * @brief UUID 结构体（128 位）
             */
            struct Uuid
            {
                uint8_t data[UUID_BYTE_SIZE];

                bool operator==(const Uuid& other) const
                {
                    return memcmp(data, other.data, UUID_BYTE_SIZE) == 0;
                }

                bool operator!=(const Uuid& other) const
                {
                    return !(*this == other);
                }
            };

            /**
             * @brief 生成 UUID v4（随机 UUID）
             * @return UUID 结构体
             */
            Uuid generate();

            /**
             * @brief 将 UUID 格式化为字符串
             * @param uuid UUID 结构体
             * @param buffer 输出缓冲区（至少 UUID_STRING_SIZE 字节）
             * @param buffer_size 缓冲区大小
             * @return 成功返回 true，失败返回 false
             */
            bool toString(const Uuid& uuid, char* buffer, size_t buffer_size);

            /**
             * @brief 从字符串解析 UUID
             * @param str UUID 字符串（格式：xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx）
             * @param uuid 输出的 UUID 结构体
             * @return 成功返回 true，失败返回 false
             */
            bool fromString(const char* str, Uuid& uuid);

            /**
             * @brief 检查 UUID 是否为空（全零）
             * @param uuid UUID 结构体
             * @return 如果为空返回 true，否则返回 false
             */
            bool isEmpty(const Uuid& uuid);

        } // namespace uuid
    } // namespace tool
} // namespace app
