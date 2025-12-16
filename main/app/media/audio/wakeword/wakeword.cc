#include "wakeword.hpp"

#include <algorithm>
#include <atomic>
#include <deque>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <esp_mn_speech_commands.h>

#include "assets/assets.hpp"

static const char* const TAG = "WakeWord";

namespace app
{
    namespace media
    {
        namespace audio
        {
            namespace wakeword
            {
                struct Command
                {
                    std::string command;
                    std::string text;
                    std::string action;
                };

                class CustomWakeWord : public WakeWord
                {
                public:
                    CustomWakeWord()  = default;
                    ~CustomWakeWord() override;

                    bool init(srmodel_list_t* models_list, int sample_rate,
                              int channels) override;
                    bool addCommand(const std::string& command, const std::string& text,
                                   const std::string& action = "wake") override;
                    void feed(const std::vector<int16_t>& data) override;
                    void setWakeWordDetected(
                        std::function<void(const std::string& wake_word)> callback) override;
                    void start() override;
                    void stop() override;
                    bool isRunning() const override
                    {
                        return running_;
                    }
                    size_t getFeedSize() const override
                    {
                        if (multinet_ == nullptr || multinet_model_data_ == nullptr)
                        {
                            return 0;
                        }
                        return multinet_->get_samp_chunksize(multinet_model_data_);
                    }
                    const std::string& getLastDetectedWakeWord() const override
                    {
                        return last_detected_wake_word_;
                    }

                private:
                    bool parseMultinetConfig();

                    esp_mn_iface_t*     multinet_            = nullptr;
                    model_iface_data_t* multinet_model_data_ = nullptr;
                    srmodel_list_t*     models_              = nullptr;
                    char*               mn_name_             = nullptr;

                    std::string         language_    = "cn";
                    int                 duration_    = 3000;
                    float               threshold_   = 0.2f;
                    std::deque<Command> commands_;
                    int                 sample_rate_ = 16000;
                    int                 channels_    = 1;

                    std::atomic<bool> running_ = false;
                    std::function<void(const std::string& wake_word)> wake_word_callback_;
                    std::string                                       last_detected_wake_word_;
                };

                CustomWakeWord::~CustomWakeWord()
                {
                    stop();

                    if (multinet_model_data_ != nullptr && multinet_ != nullptr)
                    {
                        multinet_->destroy(multinet_model_data_);
                        multinet_model_data_ = nullptr;
                    }
                }

                bool CustomWakeWord::parseMultinetConfig()
                {
                    auto& assets = app::assets::Assets::getInstance();

                    void*  ptr  = nullptr;
                    size_t size = 0;
                    if (!assets.getAssetData("index.json", ptr, size))
                    {
                        return false;
                    }

                    cJSON* root = cJSON_ParseWithLength(static_cast<const char*>(ptr), size);
                    if (root == nullptr)
                    {
                        return false;
                    }

                    cJSON* multinet_model = cJSON_GetObjectItem(root, "multinet_model");
                    if (cJSON_IsObject(multinet_model))
                    {
                        cJSON* languages_json = cJSON_GetObjectItem(multinet_model, "languages");
                        if (cJSON_IsArray(languages_json))
                        {
                            cJSON* first_lang = cJSON_GetArrayItem(languages_json, 0);
                            if (cJSON_IsString(first_lang))
                            {
                                language_ = first_lang->valuestring;
                            }
                        }
                        else
                        {
                            cJSON* language_json = cJSON_GetObjectItem(multinet_model, "language");
                            if (cJSON_IsString(language_json))
                            {
                                language_ = language_json->valuestring;
                            }
                        }

                        cJSON* duration_json = cJSON_GetObjectItem(multinet_model, "duration");
                        if (cJSON_IsNumber(duration_json))
                        {
                            duration_ = duration_json->valueint;
                        }

                        cJSON* threshold_json = cJSON_GetObjectItem(multinet_model, "threshold");
                        if (cJSON_IsNumber(threshold_json))
                        {
                            threshold_ = static_cast<float>(threshold_json->valuedouble);
                        }

                        cJSON* commands_json = cJSON_GetObjectItem(multinet_model, "commands");
                        if (cJSON_IsObject(commands_json))
                        {
                            cJSON* lang_commands = cJSON_GetObjectItem(commands_json, language_.c_str());
                            if (cJSON_IsArray(lang_commands))
                            {
                                int cmd_count = cJSON_GetArraySize(lang_commands);
                                for (int i = 0; i < cmd_count; i++)
                                {
                                    cJSON* cmd = cJSON_GetArrayItem(lang_commands, i);
                                    if (cJSON_IsObject(cmd))
                                    {
                                        cJSON* command_json = cJSON_GetObjectItem(cmd, "command");
                                        cJSON* text_json    = cJSON_GetObjectItem(cmd, "text");
                                        cJSON* action_json  = cJSON_GetObjectItem(cmd, "action");

                                        if (cJSON_IsString(command_json) && cJSON_IsString(text_json))
                                        {
                                            std::string action = "wake";
                                            if (cJSON_IsString(action_json))
                                            {
                                                action = action_json->valuestring;
                                            }
                                            commands_.push_back({command_json->valuestring, 
                                                               text_json->valuestring, action});
                                        }
                                    }
                                }
                            }
                        }
                        else if (cJSON_IsArray(commands_json))
                        {
                            int cmd_count = cJSON_GetArraySize(commands_json);
                            for (int i = 0; i < cmd_count; i++)
                            {
                                cJSON* cmd = cJSON_GetArrayItem(commands_json, i);
                                if (cJSON_IsObject(cmd))
                                {
                                    cJSON* command_json = cJSON_GetObjectItem(cmd, "command");
                                    cJSON* text_json    = cJSON_GetObjectItem(cmd, "text");
                                    cJSON* action_json  = cJSON_GetObjectItem(cmd, "action");

                                    if (cJSON_IsString(command_json) && cJSON_IsString(text_json))
                                    {
                                        std::string action = "wake";
                                        if (cJSON_IsString(action_json))
                                        {
                                            action = action_json->valuestring;
                                        }
                                        commands_.push_back({command_json->valuestring,
                                                           text_json->valuestring, action});
                                    }
                                }
                            }
                        }
                    }

                    cJSON_Delete(root);
                    return true;
                }

                bool CustomWakeWord::init(srmodel_list_t* models_list, int sample_rate,
                                          int channels)
                {
                    sample_rate_ = sample_rate;
                    channels_    = channels;

                    if (models_list == nullptr)
                    {
                        ESP_LOGE(TAG, "模型列表为空");
                        return false;
                    }
                    models_ = models_list;

                    parseMultinetConfig();

                    mn_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, language_.c_str());
                    if (mn_name_ == nullptr)
                    {
                        mn_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, nullptr);
                    }

                    if (mn_name_ == nullptr)
                    {
                        ESP_LOGE(TAG, "未找到 Multinet 模型");
                        return false;
                    }

                    multinet_ = esp_mn_handle_from_name(mn_name_);
                    if (multinet_ == nullptr)
                    {
                        ESP_LOGE(TAG, "创建 Multinet 句柄失败");
                        return false;
                    }

                    multinet_model_data_ = multinet_->create(mn_name_, duration_);
                    if (multinet_model_data_ == nullptr)
                    {
                        ESP_LOGE(TAG, "创建 Multinet 模型数据失败");
                        return false;
                    }

                    multinet_->set_det_threshold(multinet_model_data_, threshold_);

                    esp_mn_commands_clear();
                    for (size_t i = 0; i < commands_.size(); i++)
                    {
                        esp_mn_commands_add(static_cast<int>(i + 1), commands_[i].command.c_str());
                    }
                    esp_mn_commands_update();

                    ESP_LOGI(TAG, "唤醒词检测器初始化成功 (模型: %s, 命令词: %d)", 
                             mn_name_, commands_.size());

                    return true;
                }

                bool CustomWakeWord::addCommand(const std::string& command, const std::string& text,
                                                const std::string& action)
                {
                    if (multinet_ == nullptr || multinet_model_data_ == nullptr)
                    {
                        ESP_LOGE(TAG, "Multinet 未初始化");
                        return false;
                    }

                    commands_.push_back({command, text, action});

                    esp_mn_commands_clear();
                    for (size_t i = 0; i < commands_.size(); i++)
                    {
                        esp_mn_commands_add(static_cast<int>(i + 1), commands_[i].command.c_str());
                    }
                    esp_mn_commands_update();

                    ESP_LOGI(TAG, "添加命令词: %s (%zu/%zu)", text.c_str(), 
                             commands_.size(), commands_.size());

                    return true;
                }

                void CustomWakeWord::start()
                {
                    if (running_)
                    {
                        return;
                    }

                    if (commands_.empty())
                    {
                        ESP_LOGW(TAG, "未配置命令词");
                    }

                    running_ = true;
                }

                void CustomWakeWord::stop()
                {
                    running_ = false;
                }

                void CustomWakeWord::feed(const std::vector<int16_t>& data)
                {
                    if (!running_ || multinet_ == nullptr || multinet_model_data_ == nullptr ||
                        data.empty())
                    {
                        return;
                    }

                    std::vector<int16_t> mono_data;
                    const int16_t*       feed_data = data.data();

                    if (channels_ == 2)
                    {
                        mono_data.resize(data.size() / 2);
                        for (size_t i = 0, j = 0; i < mono_data.size(); ++i, j += 2)
                        {
                            mono_data[i] = data[j];
                        }
                        feed_data = mono_data.data();
                    }

                    esp_mn_state_t state =
                        multinet_->detect(multinet_model_data_, const_cast<int16_t*>(feed_data));

                    if (state == ESP_MN_STATE_DETECTED)
                    {
                        esp_mn_results_t* results = multinet_->get_results(multinet_model_data_);
                        if (results != nullptr && results->num > 0)
                        {
                            for (int i = 0; i < results->num; i++)
                            {
                                int command_id = results->command_id[i];
                                if (command_id > 0 &&
                                    command_id <= static_cast<int>(commands_.size()))
                                {
                                    const Command& cmd = commands_[command_id - 1];

                                    ESP_LOGI(TAG, "检测到: %s (%.2f)", cmd.text.c_str(),
                                             results->prob[i]);

                                    if (cmd.action == "wake")
                                    {
                                        last_detected_wake_word_ = cmd.text;
                                        running_                 = false;

                                        if (wake_word_callback_)
                                        {
                                            wake_word_callback_(cmd.text);
                                        }
                                    }
                                }
                            }
                        }
                        multinet_->clean(multinet_model_data_);
                    }
                    else if (state == ESP_MN_STATE_TIMEOUT)
                    {
                        multinet_->clean(multinet_model_data_);
                    }
                }

                void CustomWakeWord::setWakeWordDetected(
                    std::function<void(const std::string& wake_word)> callback)
                {
                    wake_word_callback_ = callback;
                }

            } // namespace wakeword
        }     // namespace audio
    }         // namespace media
} // namespace app

app::media::audio::wakeword::WakeWord* createCustomWakeWord()
{
    return new app::media::audio::wakeword::CustomWakeWord();
}

