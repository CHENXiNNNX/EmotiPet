#include "wakeword.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <map>

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
                const char* WAKEWORD_EVENT_BASE = "WAKEWORD";

                struct Command
                {
                    std::string command;
                    std::string text;
                    std::string action;
                };

                class CustomWakeWord : public WakeWord
                {
                public:
                    CustomWakeWord() = default;
                    ~CustomWakeWord() override;

                    bool init(srmodel_list_t* models_list, int sample_rate, int channels) override;
                    bool addCommand(const std::string& command, const std::string& text,
                                    const std::string& action = "wake") override;
                    bool removeCommand(const std::string& text) override;
                    void clearCommands() override;
                    bool switchModel(const std::string& language) override;
                    void feed(const std::vector<int16_t>& data) override;
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
                    void postWakeWordEvent(const Command& cmd, float probability);

                    esp_mn_iface_t*     multinet_            = nullptr;
                    model_iface_data_t* multinet_model_data_ = nullptr;
                    srmodel_list_t*     models_              = nullptr;
                    char*               mn_name_             = nullptr;

                    std::string                                language_  = "cn";
                    int                                        duration_  = 3000;
                    float                                      threshold_ = 0.2f;
                    std::map<std::string, std::deque<Command>> commands_by_lang_;
                    int                                        sample_rate_ = 16000;
                    int                                        channels_    = 1;

                    std::atomic<bool> running_ = false;
                    std::string       last_detected_wake_word_;
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
                            cJSON* lang_commands =
                                cJSON_GetObjectItem(commands_json, language_.c_str());
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

                                        if (cJSON_IsString(command_json) &&
                                            cJSON_IsString(text_json))
                                        {
                                            std::string action = "wake";
                                            if (cJSON_IsString(action_json))
                                            {
                                                action = action_json->valuestring;
                                            }
                                            commands_by_lang_[language_].push_back(
                                                {command_json->valuestring, text_json->valuestring,
                                                 action});
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
                                        commands_by_lang_[language_].push_back(
                                            {command_json->valuestring, text_json->valuestring,
                                             action});
                                    }
                                }
                            }
                        }
                    }

                    cJSON_Delete(root);
                    return true;
                }

                void CustomWakeWord::postWakeWordEvent(const Command& cmd, float probability)
                {
                    WakeWordEventData event_data{};
                    strncpy(event_data.text, cmd.text.c_str(), sizeof(event_data.text) - 1);
                    strncpy(event_data.command, cmd.command.c_str(),
                            sizeof(event_data.command) - 1);
                    strncpy(event_data.action, cmd.action.c_str(), sizeof(event_data.action) - 1);
                    event_data.probability = probability;

                    auto& event_mgr = app::sys::event::EventManager::getInstance();
                    event_mgr.post(WAKEWORD_EVENT_BASE, WAKEWORD_EVENT_DETECTED,
                                   {&event_data, sizeof(event_data)});
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

                    // 注册已有的命令词
                    esp_mn_commands_clear();
                    auto& commands = commands_by_lang_[language_];
                    for (size_t i = 0; i < commands.size(); i++)
                    {
                        esp_mn_commands_add(static_cast<int>(i + 1), commands[i].command.c_str());
                    }
                    if (!commands.empty())
                    {
                        esp_mn_commands_update();
                    }

                    ESP_LOGI(TAG, "初始化成功 (模型: %s, 命令词: %zu)", mn_name_, commands.size());

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

                    // 存储到当前语言的命令列表
                    commands_by_lang_[language_].push_back({command, text, action});

                    // 更新 Multinet 命令
                    auto& commands = commands_by_lang_[language_];
                    esp_mn_commands_clear();
                    for (size_t i = 0; i < commands.size(); i++)
                    {
                        esp_mn_commands_add(static_cast<int>(i + 1), commands[i].command.c_str());
                    }
                    esp_mn_commands_update();

                    return true;
                }

                bool CustomWakeWord::removeCommand(const std::string& text)
                {
                    auto& commands = commands_by_lang_[language_];

                    auto it =
                        std::find_if(commands.begin(), commands.end(),
                                     [&text](const Command& cmd) { return cmd.text == text; });

                    if (it == commands.end())
                    {
                        return false;
                    }

                    commands.erase(it);

                    // 重新注册命令
                    esp_mn_commands_clear();
                    for (size_t i = 0; i < commands.size(); i++)
                    {
                        esp_mn_commands_add(static_cast<int>(i + 1), commands[i].command.c_str());
                    }
                    if (!commands.empty())
                    {
                        esp_mn_commands_update();
                    }

                    return true;
                }

                void CustomWakeWord::clearCommands()
                {
                    commands_by_lang_[language_].clear();
                    esp_mn_commands_clear();
                }

                bool CustomWakeWord::switchModel(const std::string& language)
                {
                    if (models_ == nullptr)
                    {
                        ESP_LOGE(TAG, "模型列表为空");
                        return false;
                    }

                    // 查找目标语言模型
                    char* new_mn_name =
                        esp_srmodel_filter(models_, ESP_MN_PREFIX, language.c_str());
                    if (new_mn_name == nullptr)
                    {
                        ESP_LOGE(TAG, "未找到语言 '%s' 的模型", language.c_str());
                        return false;
                    }

                    // 如果是同一个模型，直接返回
                    if (mn_name_ != nullptr && strcmp(mn_name_, new_mn_name) == 0)
                    {
                        return true;
                    }

                    // 停止当前检测
                    bool was_running = running_;
                    stop();

                    // 销毁旧模型
                    if (multinet_model_data_ != nullptr && multinet_ != nullptr)
                    {
                        multinet_->destroy(multinet_model_data_);
                        multinet_model_data_ = nullptr;
                    }

                    // 创建新模型
                    mn_name_  = new_mn_name;
                    language_ = language;

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

                    // 恢复该语言的命令词
                    esp_mn_commands_clear();
                    auto it = commands_by_lang_.find(language);
                    if (it != commands_by_lang_.end())
                    {
                        for (size_t i = 0; i < it->second.size(); i++)
                        {
                            esp_mn_commands_add(static_cast<int>(i + 1),
                                                it->second[i].command.c_str());
                        }
                        esp_mn_commands_update();
                    }

                    ESP_LOGI(TAG, "切换到模型: %s (命令词: %zu)", mn_name_,
                             it != commands_by_lang_.end() ? it->second.size() : 0);

                    // 恢复运行状态
                    if (was_running)
                    {
                        start();
                    }

                    return true;
                }

                void CustomWakeWord::start()
                {
                    if (running_)
                    {
                        return;
                    }

                    running_ = true;

                    auto& event_mgr = app::sys::event::EventManager::getInstance();
                    event_mgr.post(WAKEWORD_EVENT_BASE, WAKEWORD_EVENT_STARTED, {});
                }

                void CustomWakeWord::stop()
                {
                    if (!running_)
                    {
                        return;
                    }

                    running_ = false;

                    auto& event_mgr = app::sys::event::EventManager::getInstance();
                    event_mgr.post(WAKEWORD_EVENT_BASE, WAKEWORD_EVENT_STOPPED, {});
                }

                void CustomWakeWord::feed(const std::vector<int16_t>& data)
                {
                    if (!running_ || multinet_ == nullptr || multinet_model_data_ == nullptr ||
                        data.empty())
                    {
                        return;
                    }

                    std::vector<int16_t> mono_data;
                    int16_t*            feed_data = nullptr;

                    if (channels_ == 2)
                    {
                        mono_data.resize(data.size() / 2);
                        for (size_t i = 0, j = 0; i < mono_data.size(); ++i, j += 2)
                        {
                            mono_data[i] = data[j];
                        }
                        feed_data = mono_data.data();
                    }
                    else
                    {
                        feed_data = const_cast<int16_t*>(data.data());
                    }

                    esp_mn_state_t state = multinet_->detect(multinet_model_data_, feed_data);

                    if (state == ESP_MN_STATE_DETECTED)
                    {
                        esp_mn_results_t* results  = multinet_->get_results(multinet_model_data_);
                        auto&             commands = commands_by_lang_[language_];
                        if (results != nullptr && results->num > 0)
                        {
                            for (int i = 0; i < results->num; i++)
                            {
                                int command_id = results->command_id[i];
                                if (command_id > 0 &&
                                    command_id <= static_cast<int>(commands.size()))
                                {
                                    const Command& cmd = commands[command_id - 1];

                                    if (cmd.action == "wake")
                                    {
                                        last_detected_wake_word_ = cmd.text;
                                        running_                 = false;
                                        postWakeWordEvent(cmd, results->prob[i]);
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

            } // namespace wakeword
        }     // namespace audio
    }         // namespace media
} // namespace app

app::media::audio::wakeword::WakeWord* createCustomWakeWord()
{
    return new app::media::audio::wakeword::CustomWakeWord();
}
