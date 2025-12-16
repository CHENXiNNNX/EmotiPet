#include "device/led/led.hpp"
#include "system/task/task.hpp"
#include "esp_log.h"

using namespace app::device::led;
using namespace app::sys::task;

static const char* TAG = "LED_DEMO";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "WS2812 LED 使用示例");
    ESP_LOGI(TAG, "========================================");

    // 创建WS2812对象
    WS2812 led;

    // 设置GPIO引脚（根据你的硬件连接修改）
    gpio_num_t gpio = GPIO_NUM_48;

    // 等待系统稳定
    TaskManager::delayMs(1000);

    // ========== 示例1: 基本颜色设置（常亮）==========
    ESP_LOGI(TAG, "\n[示例1] 设置红色并常亮");
    if (led.setColor(gpio, Color(255, 0, 0))) // RGB(255, 0, 0) = 红色
    {
        TaskManager::delayMs(2000); // 保持2秒
    }

    ESP_LOGI(TAG, "\n[示例1] 设置绿色并常亮");
    led.setColor(gpio, Color(0, 255, 0)); // RGB(0, 255, 0) = 绿色
    TaskManager::delayMs(2000);

    ESP_LOGI(TAG, "\n[示例1] 设置蓝色并常亮");
    led.setColor(gpio, Color(0, 0, 255)); // RGB(0, 0, 255) = 蓝色
    TaskManager::delayMs(2000);

    // ========== 示例2: 闪烁功能（有限次数）==========
    ESP_LOGI(TAG, "\n[示例2] 红色闪烁5次（间隔500ms）");
    // 步骤1: 设置闪烁参数（间隔500ms，闪烁5次）
    led.setBlinkConfig(gpio, 500, 5);
    // 步骤2: 设置闪烁的颜色
    led.setColor(gpio, Color(255, 0, 0));
    // 步骤3: 启动闪烁
    if (led.startBlink(gpio))
    {
        ESP_LOGI(TAG, "闪烁开始，等待完成...");
        // 等待闪烁完成：5次 * 2个周期 * 500ms = 5000ms，加上余量
        TaskManager::delayMs(6000);
        ESP_LOGI(TAG, "闪烁完成");
    }

    // ========== 示例3: 无限闪烁 ==========
    ESP_LOGI(TAG, "\n[示例3] 蓝色无限闪烁（间隔1000ms，持续5秒）");
    led.setBlinkConfig(gpio, 1000, -1); // count = -1 表示无限闪烁
    led.setColor(gpio, Color(0, 0, 255));
    if (led.startBlink(gpio))
    {
        ESP_LOGI(TAG, "无限闪烁开始");
        TaskManager::delayMs(5000); // 闪烁5秒
        led.stopBlink(gpio);        // 手动停止
        ESP_LOGI(TAG, "无限闪烁已停止");
    }

    // ========== 示例4: 常亮模式（interval_ms = -1）==========
    ESP_LOGI(TAG, "\n[示例4] 黄色常亮模式（interval_ms = -1）");
    led.setBlinkConfig(gpio, -1, -1);       // interval_ms = -1 表示常亮
    led.setColor(gpio, Color(255, 255, 0)); // 黄色
    if (led.startBlink(gpio))
    {
        ESP_LOGI(TAG, "黄色常亮模式启动");
        TaskManager::delayMs(3000);
    }

    // ========== 示例5: 混合颜色展示 ==========
    ESP_LOGI(TAG, "\n[示例5] 混合颜色展示");
    Color colors[] = {
        Color(255, 0, 0),     // 红色
        Color(0, 255, 0),     // 绿色
        Color(0, 0, 255),     // 蓝色
        Color(255, 255, 0),   // 黄色
        Color(255, 0, 255),   // 洋红
        Color(0, 255, 255),   // 青色
        Color(255, 255, 255), // 白色
    };
    const char* color_names[] = {"红色", "绿色", "蓝色", "黄色", "洋红", "青色", "白色"};

    for (int i = 0; i < 7; i++)
    {
        ESP_LOGI(TAG, "  显示 %s (1秒)", color_names[i]);
        led.setColor(gpio, colors[i]);
        TaskManager::delayMs(1000);
    }

    // ========== 示例6: 设置不同亮度级别 ==========
    ESP_LOGI(TAG, "\n[示例1] 亮度渐变效果（红色）");
    Color red(255, 0, 0);

    // 从0%到100%，每次增加10%
    for (uint8_t brightness = 0; brightness <= 100; brightness += 10)
    {
        led.setBrightness(brightness);
        led.setColor(gpio, red);
        ESP_LOGI(TAG, "  亮度: %d%%", brightness);
        TaskManager::delayMs(500);
    }

    // ========== 示例7: 降低亮度 ==========
    ESP_LOGI(TAG, "\n[示例2] 降低亮度到50%%");
    led.setBrightness(50);
    led.setColor(gpio, Color(255, 255, 255)); // 白色
    ESP_LOGI(TAG, "  当前亮度: %d%%", led.getBrightness());
    TaskManager::delayMs(2000);

    // ========== 示例8: 亮度为0（关闭）==========
    ESP_LOGI(TAG, "\n[示例3] 设置亮度为0（关闭）");
    led.setBrightness(0);
    led.setColor(gpio, Color(255, 0, 0)); // 即使设置红色，亮度为0也会关闭
    ESP_LOGI(TAG, "  LED已关闭");
    TaskManager::delayMs(1000);

    // ========== 示例9: 恢复亮度并闪烁 ==========
    ESP_LOGI(TAG, "\n[示例4] 设置亮度为30%%，然后闪烁");
    led.setBrightness(30);                // 30%亮度
    led.setBlinkConfig(gpio, 500, 5);     // 闪烁5次
    led.setColor(gpio, Color(0, 255, 0)); // 绿色
    led.startBlink(gpio);
    ESP_LOGI(TAG, "  30%%亮度绿色闪烁中...");
    TaskManager::delayMs(6000);

    // ========== 示例10: 动态调整亮度 ==========
    ESP_LOGI(TAG, "\n[示例5] 动态调整亮度（呼吸灯效果）");
    led.setColor(gpio, Color(0, 0, 255)); // 蓝色

    // 亮度从10%到100%，再回到10%
    for (uint8_t brightness = 10; brightness <= 100; brightness += 5)
    {
        led.setBrightness(brightness);
        TaskManager::delayMs(100);
    }
    for (uint8_t brightness = 100; brightness >= 10; brightness -= 5)
    {
        led.setBrightness(brightness);
        TaskManager::delayMs(100);
    }

    // ========== 示例11: 获取当前亮度 ==========
    ESP_LOGI(TAG, "\n[示例6] 获取当前亮度");
    uint8_t current_brightness = led.getBrightness();
    ESP_LOGI(TAG, "  当前亮度: %d%%", current_brightness);

    // ========== 示例12: 恢复100%亮度 ==========
    ESP_LOGI(TAG, "\n[示例7] 恢复100%%亮度");
    led.setBrightness(100);
    led.setColor(gpio, Color(255, 255, 255)); // 白色，最亮
    ESP_LOGI(TAG, "  LED已恢复最亮");
    TaskManager::delayMs(2000);

    // 关闭LED
    led.setColor(gpio, Color(0, 0, 0));
}
