#include "led.hpp"
#include "esp_log.h"
#include "esp_check.h"
#include "system/task/task.hpp"
#include <cstring>

static const char* const TAG = "WS2812";

namespace app::device::led
{
// WS2812时序参数（80MHz时钟，每个时钟周期12.5ns）
#define WS2812_T0H_NS (400)  // 0码高电平时间（纳秒）
#define WS2812_T0L_NS (850)  // 0码低电平时间（纳秒）
#define WS2812_T1H_NS (800)  // 1码高电平时间（纳秒）
#define WS2812_T1L_NS (450)  // 1码低电平时间（纳秒）
#define WS2812_RESET_US (50) // 复位时间（微秒）

// 计算时钟周期数（80MHz = 12.5ns per tick）
#define NS_TO_TICKS(ns) ((ns) / 12.5)

    WS2812::WS2812()
        : encoder_handle_(nullptr), channel_handle_(nullptr), current_gpio_(GPIO_NUM_NC),
          brightness_(100)
    {
        memset(&blink_config_, 0, sizeof(blink_config_));
        blink_config_.interval_ms = -1;
        blink_config_.count       = -1;
        blink_config_.is_running  = false;

        memset(&breathing_config_, 0, sizeof(breathing_config_));
        breathing_config_.cycle_ms   = 2000;
        breathing_config_.led_count  = 1;
        breathing_config_.is_running = false;
    }

    WS2812::~WS2812()
    {
        stopBlink(current_gpio_);
        stopBreathing(current_gpio_);
        deinitRMTChannel();
    }

    bool WS2812::initRMTChannel(gpio_num_t gpio_num)
    {
        // 如果已经初始化了相同的GPIO，直接返回
        if (channel_handle_ != nullptr && current_gpio_ == gpio_num)
        {
            return true;
        }

        // 如果GPIO不同，先释放旧的通道
        if (channel_handle_ != nullptr)
        {
            deinitRMTChannel();
        }

        // 配置RMT编码器
        rmt_bytes_encoder_config_t bytes_encoder_config = {};
        bytes_encoder_config.bit0.level0                = 1;
        bytes_encoder_config.bit0.duration0 =
            (uint32_t)NS_TO_TICKS(WS2812_T0H_NS); // 转换为时钟周期
        bytes_encoder_config.bit0.level1     = 0;
        bytes_encoder_config.bit0.duration1  = (uint32_t)NS_TO_TICKS(WS2812_T0L_NS);
        bytes_encoder_config.bit1.level0     = 1;
        bytes_encoder_config.bit1.duration0  = (uint32_t)NS_TO_TICKS(WS2812_T1H_NS);
        bytes_encoder_config.bit1.level1     = 0;
        bytes_encoder_config.bit1.duration1  = (uint32_t)NS_TO_TICKS(WS2812_T1L_NS);
        bytes_encoder_config.flags.msb_first = 1; // MSB优先

        ESP_RETURN_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &encoder_handle_), TAG,
                            "创建RMT编码器失败");

        // 配置RMT通道
        rmt_tx_channel_config_t tx_chan_config = {};
        tx_chan_config.gpio_num                = gpio_num;
        tx_chan_config.clk_src                 = RMT_CLK_SRC_DEFAULT;
        tx_chan_config.resolution_hz           = 80 * 1000 * 1000; // 80MHz
        tx_chan_config.mem_block_symbols       = 64;
        tx_chan_config.trans_queue_depth       = 4;
        tx_chan_config.flags.invert_out        = false;
        tx_chan_config.flags.with_dma          = false;

        ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_chan_config, &channel_handle_), TAG,
                            "创建RMT通道失败");

        // 使能RMT通道
        ESP_RETURN_ON_ERROR(rmt_enable(channel_handle_), TAG, "使能RMT通道失败");

        current_gpio_ = gpio_num;
        ESP_LOGI(TAG, "RMT通道初始化成功，GPIO: %d", gpio_num);
        return true;
    }

    void WS2812::deinitRMTChannel()
    {
        if (channel_handle_ != nullptr)
        {
            rmt_disable(channel_handle_);
            rmt_del_channel(channel_handle_);
            channel_handle_ = nullptr;
        }

        if (encoder_handle_ != nullptr)
        {
            rmt_del_encoder(encoder_handle_);
            encoder_handle_ = nullptr;
        }

        current_gpio_ = GPIO_NUM_NC;
    }

    bool WS2812::sendColor(const Color& color)
    {
        if (channel_handle_ == nullptr || encoder_handle_ == nullptr)
        {
            ESP_LOGE(TAG, "RMT通道未初始化");
            return false;
        }

        // 根据亮度调整颜色值
        Color adjusted_color;
        if (brightness_ == 100)
        {
            // 亮度100%，直接使用原色
            adjusted_color = color;
        }
        else if (brightness_ == 0)
        {
            // 亮度0%，关闭LED
            adjusted_color = Color(0, 0, 0);
        }
        else
        {
            // 计算亮度比例（0.0 - 1.0）
            float brightness_factor = brightness_ / 100.0f;
            adjusted_color.r        = (uint8_t)(color.r * brightness_factor);
            adjusted_color.g        = (uint8_t)(color.g * brightness_factor);
            adjusted_color.b        = (uint8_t)(color.b * brightness_factor);
        }

        // WS2812数据格式：GRB（注意顺序）
        uint8_t led_data[3] = {adjusted_color.g, adjusted_color.r, adjusted_color.b};

        rmt_transmit_config_t tx_config = {};
        tx_config.loop_count            = 0;
        tx_config.flags.eot_level       = 0;

        ESP_RETURN_ON_ERROR(
            rmt_transmit(channel_handle_, encoder_handle_, led_data, sizeof(led_data), &tx_config),
            TAG, "发送颜色数据失败");

        // 等待传输完成并发送复位信号
        rmt_tx_wait_all_done(channel_handle_, portMAX_DELAY);
        app::sys::task::TaskManager::delayMs(WS2812_RESET_US / 1000);

        return true;
    }

    bool WS2812::sendColors(const Color* colors, size_t count)
    {
        if (channel_handle_ == nullptr || encoder_handle_ == nullptr)
        {
            ESP_LOGE(TAG, "RMT通道未初始化");
            return false;
        }

        if (colors == nullptr || count == 0)
        {
            ESP_LOGE(TAG, "颜色数组为空或数量为0");
            return false;
        }

        // 为所有LED准备数据（每个LED 3个字节：GRB）
        uint8_t* led_data = new uint8_t[count * 3];
        if (led_data == nullptr)
        {
            ESP_LOGE(TAG, "内存分配失败");
            return false;
        }

        // 填充LED数据
        for (size_t i = 0; i < count; i++)
        {
            Color adjusted_color;
            if (brightness_ == 100)
            {
                adjusted_color = colors[i];
            }
            else if (brightness_ == 0)
            {
                adjusted_color = Color(0, 0, 0);
            }
            else
            {
                float brightness_factor = brightness_ / 100.0f;
                adjusted_color.r        = (uint8_t)(colors[i].r * brightness_factor);
                adjusted_color.g        = (uint8_t)(colors[i].g * brightness_factor);
                adjusted_color.b        = (uint8_t)(colors[i].b * brightness_factor);
            }

            // WS2812数据格式：GRB（注意顺序）
            led_data[i * 3 + 0] = adjusted_color.g;
            led_data[i * 3 + 1] = adjusted_color.r;
            led_data[i * 3 + 2] = adjusted_color.b;
        }

        rmt_transmit_config_t tx_config = {};
        tx_config.loop_count            = 0;
        tx_config.flags.eot_level       = 0;

        esp_err_t ret =
            rmt_transmit(channel_handle_, encoder_handle_, led_data, count * 3, &tx_config);
        delete[] led_data;

        ESP_RETURN_ON_ERROR(ret, TAG, "发送多个LED颜色数据失败");

        // 等待传输完成并发送复位信号
        rmt_tx_wait_all_done(channel_handle_, portMAX_DELAY);
        app::sys::task::TaskManager::delayMs(WS2812_RESET_US / 1000);

        return true;
    }

    bool WS2812::setBlinkConfig(gpio_num_t gpio_num, int32_t interval_ms, int32_t count)
    {
        // 停止当前闪烁任务
        if (blink_config_.is_running && blink_task_ != nullptr)
        {
            stopBlink(current_gpio_);
        }

        // 保存配置
        blink_config_.interval_ms = interval_ms;
        blink_config_.count       = count;

        // 初始化RMT通道
        if (!initRMTChannel(gpio_num))
        {
            ESP_LOGE(TAG, "初始化RMT通道失败，GPIO: %d", gpio_num);
            return false;
        }

        ESP_LOGI(TAG, "设置闪烁配置 - GPIO: %d, 间隔: %ld ms, 次数: %ld", gpio_num,
                 (long)interval_ms, (long)count);
        return true;
    }

    bool WS2812::setColor(gpio_num_t gpio_num, const Color& color)
    {
        // 初始化RMT通道
        if (!initRMTChannel(gpio_num))
        {
            ESP_LOGE(TAG, "初始化RMT通道失败，GPIO: %d", gpio_num);
            return false;
        }

        // 保存颜色
        blink_config_.color = color;

        // 发送颜色
        if (!sendColor(color))
        {
            ESP_LOGE(TAG, "设置颜色失败，GPIO: %d", gpio_num);
            return false;
        }

        ESP_LOGI(TAG, "设置颜色成功 - GPIO: %d, RGB(%d, %d, %d)", gpio_num, color.r, color.g,
                 color.b);
        return true;
    }

    bool WS2812::setColors(gpio_num_t gpio_num, const Color* colors, size_t count)
    {
        if (colors == nullptr || count == 0)
        {
            ESP_LOGE(TAG, "颜色数组为空或数量为0");
            return false;
        }

        // 初始化RMT通道
        if (!initRMTChannel(gpio_num))
        {
            ESP_LOGE(TAG, "初始化RMT通道失败，GPIO: %d", gpio_num);
            return false;
        }

        // 发送多个LED的颜色数据
        if (!sendColors(colors, count))
        {
            ESP_LOGE(TAG, "设置多个LED颜色失败，GPIO: %d", gpio_num);
            return false;
        }

        // ESP_LOGI(TAG, "设置多个LED颜色成功 - GPIO: %d, LED数量: %lu", gpio_num, (unsigned
        // long)count);
        return true;
    }

    bool WS2812::startBlink(gpio_num_t gpio_num)
    {
        // 如果已经在运行，先停止
        if (blink_config_.is_running)
        {
            stopBlink(current_gpio_);
        }

        // 确保RMT通道已初始化
        if (!initRMTChannel(gpio_num))
        {
            ESP_LOGE(TAG, "初始化RMT通道失败，GPIO: %d", gpio_num);
            return false;
        }

        // 如果间隔为-1，表示常亮，直接设置颜色
        if (blink_config_.interval_ms == -1)
        {
            if (!sendColor(blink_config_.color))
            {
                ESP_LOGE(TAG, "设置常亮颜色失败");
                return false;
            }
            ESP_LOGI(TAG, "LED常亮 - GPIO: %d", gpio_num);
            return true;
        }

        // 创建闪烁任务
        app::sys::task::Config task_config;
        task_config.name       = "led_blink";
        task_config.stack_size = 4096;
        task_config.priority   = app::sys::task::Priority::NORMAL;
        task_config.core_id    = -1;
        task_config.delay_ms   = 0;

        blink_task_ = std::unique_ptr<app::sys::task::Task>(new app::sys::task::Task(
            [this](void* param) { this->blinkTaskFunction(param); }, task_config, this));

        if (!blink_task_)
        {
            ESP_LOGE(TAG, "创建闪烁任务对象失败");
            return false;
        }

        blink_config_.is_running = true;
        if (!blink_task_->start())
        {
            ESP_LOGE(TAG, "启动闪烁任务失败");
            blink_config_.is_running = false;
            blink_task_.reset();
            return false;
        }

        ESP_LOGI(TAG, "开始闪烁 - GPIO: %d", gpio_num);
        return true;
    }

    bool WS2812::stopBlink(gpio_num_t gpio_num)
    {
        if (!blink_config_.is_running)
        {
            return true;
        }

        // 停止任务
        blink_config_.is_running = false;

        // 删除任务
        if (blink_task_ != nullptr)
        {
            blink_task_->destroy();
            blink_task_.reset();
        }

        // 关闭LED
        Color black(0, 0, 0);
        sendColor(black);

        ESP_LOGI(TAG, "停止闪烁 - GPIO: %d", gpio_num);
        return true;
    }

    void WS2812::blinkTaskFunction(void* param)
    {
        int32_t count       = blink_config_.count;
        int32_t interval_ms = blink_config_.interval_ms;
        bool    infinite    = (count == -1);

        ESP_LOGI(TAG, "闪烁任务开始 - 间隔: %ld ms, 次数: %s", (long)interval_ms,
                 infinite ? "无限" : "有限");

        while (blink_config_.is_running)
        {
            // 点亮LED
            sendColor(blink_config_.color);
            app::sys::task::TaskManager::delayMs(interval_ms);

            // 如果任务还在运行，熄灭LED
            if (blink_config_.is_running)
            {
                Color black(0, 0, 0);
                sendColor(black);
                app::sys::task::TaskManager::delayMs(interval_ms);
            }

            // 如果不是无限闪烁，减少计数
            if (!infinite)
            {
                count--;
                if (count <= 0)
                {
                    break;
                }
            }
        }

        // 任务结束，关闭LED
        Color black(0, 0, 0);
        sendColor(black);
        blink_config_.is_running = false;

        ESP_LOGI(TAG, "闪烁任务结束");
    }

    bool WS2812::setBrightness(uint8_t brightness)
    {
        // 验证亮度值范围
        if (brightness > 100)
        {
            ESP_LOGE(TAG, "亮度值超出范围，应为0-100");
            return false;
        }

        brightness_ = brightness;
        ESP_LOGI(TAG, "设置亮度: %d%%", brightness);

        // 如果LED当前正在显示颜色，立即应用新的亮度
        if (blink_config_.is_running)
        {
            // 如果正在闪烁，重新发送当前颜色以应用新亮度
            sendColor(blink_config_.color);
        }
        else if (current_gpio_ != GPIO_NUM_NC)
        {
            // 如果LED常亮，重新发送当前颜色以应用新亮度
            sendColor(blink_config_.color);
        }

        return true;
    }

    bool WS2812::startBreathing(gpio_num_t gpio_num, const Color& color, uint32_t cycle_ms,
                                size_t led_count)
    {
        // 如果已经在运行，先停止
        if (breathing_config_.is_running)
        {
            stopBreathing(current_gpio_);
        }

        // 确保RMT通道已初始化
        if (!initRMTChannel(gpio_num))
        {
            ESP_LOGE(TAG, "初始化RMT通道失败，GPIO: %d", gpio_num);
            return false;
        }

        // 保存配置
        breathing_config_.color     = color;
        breathing_config_.cycle_ms  = cycle_ms;
        breathing_config_.led_count = led_count;

        // 创建呼吸灯任务
        app::sys::task::Config task_config;
        task_config.name       = "led_breathing";
        task_config.stack_size = 4096;
        task_config.priority   = app::sys::task::Priority::NORMAL;
        task_config.core_id    = -1;
        task_config.delay_ms   = 0;

        breathing_task_ = std::unique_ptr<app::sys::task::Task>(new app::sys::task::Task(
            [this](void* param) { this->breathingTaskFunction(param); }, task_config, this));

        if (!breathing_task_)
        {
            ESP_LOGE(TAG, "创建呼吸灯任务对象失败");
            return false;
        }

        breathing_config_.is_running = true;
        if (!breathing_task_->start())
        {
            ESP_LOGE(TAG, "启动呼吸灯任务失败");
            breathing_config_.is_running = false;
            breathing_task_.reset();
            return false;
        }

        ESP_LOGI(TAG, "开始呼吸灯 - GPIO: %d, 周期: %lu ms, LED数量: %lu", gpio_num,
                 (unsigned long)cycle_ms, (unsigned long)led_count);
        return true;
    }

    bool WS2812::stopBreathing(gpio_num_t gpio_num)
    {
        if (!breathing_config_.is_running)
        {
            return true;
        }

        // 停止任务
        breathing_config_.is_running = false;

        // 删除任务
        if (breathing_task_ != nullptr)
        {
            breathing_task_->destroy();
            breathing_task_.reset();
        }

        // 关闭LED
        if (breathing_config_.led_count == 1)
        {
            Color black(0, 0, 0);
            sendColor(black);
        }
        else
        {
            Color* colors = new Color[breathing_config_.led_count];
            for (size_t i = 0; i < breathing_config_.led_count; i++)
            {
                colors[i] = Color(0, 0, 0);
            }
            sendColors(colors, breathing_config_.led_count);
            delete[] colors;
        }

        ESP_LOGI(TAG, "停止呼吸灯 - GPIO: %d", gpio_num);
        return true;
    }

    bool WS2812::updateBreathingColor(const Color& color)
    {
        if (!breathing_config_.is_running)
        {
            ESP_LOGW(TAG, "呼吸灯未运行，无法更新颜色");
            return false;
        }

        // 更新颜色配置（呼吸灯任务会读取最新颜色）
        breathing_config_.color = color;
        return true;
    }

    void WS2812::breathingTaskFunction(void* param)
    {
        uint32_t cycle_ms  = breathing_config_.cycle_ms;
        size_t   led_count = breathing_config_.led_count;

        ESP_LOGI(TAG, "呼吸灯任务开始 - 周期: %lu ms, LED数量: %lu", (unsigned long)cycle_ms,
                 (unsigned long)led_count);

        // 计算每步的延迟时间（使用100步来实现平滑的呼吸效果）
        const uint32_t steps      = 100;
        uint32_t       step_delay = cycle_ms / (2 * steps); // 一半时间上升，一半时间下降

        while (breathing_config_.is_running)
        {
            // 每次循环都读取最新的颜色（支持动态切换颜色）
            Color base_color = breathing_config_.color;

            // 上升阶段：从0到255
            for (uint32_t step = 0; step <= steps && breathing_config_.is_running; step++)
            {
                // 每次循环都读取最新的颜色（支持动态切换颜色）
                base_color = breathing_config_.color;

                // 计算当前亮度（0-255）
                uint8_t brightness = (uint8_t)((step * 255) / steps);

                // 根据亮度调整颜色
                Color current_color;
                current_color.r = (uint8_t)((base_color.r * brightness) / 255);
                current_color.g = (uint8_t)((base_color.g * brightness) / 255);
                current_color.b = (uint8_t)((base_color.b * brightness) / 255);

                // 发送颜色
                if (led_count == 1)
                {
                    sendColor(current_color);
                }
                else
                {
                    Color* colors = new Color[led_count];
                    for (size_t i = 0; i < led_count; i++)
                    {
                        colors[i] = current_color;
                    }
                    sendColors(colors, led_count);
                    delete[] colors;
                }

                app::sys::task::TaskManager::delayMs(step_delay);
            }

            // 下降阶段：从255到0
            for (uint32_t step = steps; step > 0 && breathing_config_.is_running; step--)
            {
                // 每次循环都读取最新的颜色（支持动态切换颜色）
                base_color = breathing_config_.color;

                // 计算当前亮度（0-255）
                uint8_t brightness = (uint8_t)((step * 255) / steps);

                // 根据亮度调整颜色
                Color current_color;
                current_color.r = (uint8_t)((base_color.r * brightness) / 255);
                current_color.g = (uint8_t)((base_color.g * brightness) / 255);
                current_color.b = (uint8_t)((base_color.b * brightness) / 255);

                // 发送颜色
                if (led_count == 1)
                {
                    sendColor(current_color);
                }
                else
                {
                    Color* colors = new Color[led_count];
                    for (size_t i = 0; i < led_count; i++)
                    {
                        colors[i] = current_color;
                    }
                    sendColors(colors, led_count);
                    delete[] colors;
                }

                app::sys::task::TaskManager::delayMs(step_delay);
            }
        }

        // 任务结束，关闭LED
        if (led_count == 1)
        {
            Color black(0, 0, 0);
            sendColor(black);
        }
        else
        {
            Color* colors = new Color[led_count];
            for (size_t i = 0; i < led_count; i++)
            {
                colors[i] = Color(0, 0, 0);
            }
            sendColors(colors, led_count);
            delete[] colors;
        }

        breathing_config_.is_running = false;
        ESP_LOGI(TAG, "呼吸灯任务结束");
    }

} // namespace app::device::led
