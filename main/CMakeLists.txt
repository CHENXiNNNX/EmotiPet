# Define source files
set(SOURCES "app/assets/assets.cc"
            "app/battery/battery.cc"
            "app/chatbot/chatbot.cc"
            "app/chatbot/handle/receiver.cc"
            "app/chatbot/handle/sender.cc"
            "app/chatbot/message/message.cc"
            "app/i2c/i2c.cc"
            "app/logic/logic.cc"
            "app/device/qmi8658a/qmi8658a.cc"
            "app/device/apds9930/apds9930.cc"
            "app/device/button/button.cc"
            "app/device/led/led.cc"
            "app/device/m0404/m0404.cc"
            "app/device/mpr121/mpr121.cc"
            "app/device/mc1081s/mc1081.c"
            "app/device/mc1081s/common.c"
            "app/device/mc1081s/i2c_adapter.c"
            # "app/device/touch/touch.cc"
            "app/media/audio/audio.cc"
            "app/media/audio/capture/capture.cc"
            "app/media/audio/process/afe/afe.cc"
            "app/media/audio/process/opus/encode/opus_enc.cc"
            "app/media/audio/process/opus/decode/opus_dec.cc"
            "app/media/audio/wakeword/wakeword.cc"
            "app/media/camera/camera.cc"
            "app/media/camera/process/jpeg/encode/jpeg_enc.cc"
            "app/network/bluetooth/bluetooth.cc"
            "app/network/bluetooth/gatt/gatt.cc"
            "app/network/network.cc"
            "app/network/wifi/wifi.cc"
            "app/protocol/http/http.cc"
            "app/protocol/ntp/ntp.cc"
            "app/protocol/websocket/websocket.cc"
            "app/system/event/event.cc"
            "app/system/info/info.cc"
            "app/system/power/power.cc"
            "app/system/task/task.cc"
            # "app/tool/file/file.cc"  
            
            "app/tool/memory/memory.cc"
            "app/tool/ota/ota.cc"
            "app/tool/time/time.cc"
            "app/tool/uuid/uuid.cc"
            "app/app.cc"
            "main.cc"
            )

set(INCLUDE_DIRS "app"
                 "app/assets"
                 "app/battery"
                 "app/chatbot"
                 "app/chatbot/handle"
                 "app/chatbot/message"
                 "app/i2c"
                 "app/logic"
                 "app/device/apds9930"
                 "app/device/qmi8658a"
                 "app/device/button" 
                 "app/device/led"
                 "app/device/m0404"
                 "app/device/mpr121"
                 "app/device/mc1081s"
                #  "app/device/touch"
                 "app/media/audio"
                 "app/media/audio/capture"
                 "app/media/audio/process"
                 "app/media/audio/process/afe"
                 "app/media/audio/process/opus/encode"
                 "app/media/audio/process/opus/decode"
                 "app/media/audio/wakeword"
                 "app/media/camera"
                 "app/network"
                 "app/network/bluetooth"
                 "app/network/bluetooth/gatt"
                 "app/network/wifi"
                 "app/protocol/http"
                 "app/protocol/ntp"
                 "app/protocol/websocket"
                 "app/system/event"
                 "app/system/info"
                 "app/system/power"
                 "app/system/task"
                # "app/tool/file"
                 "app/media/camera/process/jpeg/encode"
                 "app/tool/memory"
                 "app/tool/ota"
                 "app/tool/time"
                 "app/tool/uuid"
                 )

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS ${INCLUDE_DIRS}
                       )

# =============================================================================
# Assets 打包配置
# =============================================================================

# 获取项目根目录
get_filename_component(PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# 查找 ESP-SR 组件
idf_build_get_property(build_components BUILD_COMPONENTS)
set(ESP_SR_COMPONENT_PATH "")
foreach(COMPONENT ${build_components})
    if(COMPONENT MATCHES "espressif__esp-sr")
        idf_component_get_property(COMPONENT_PATH ${COMPONENT} COMPONENT_DIR)
        set(ESP_SR_COMPONENT_PATH "${COMPONENT_PATH}")
        break()
    endif()
endforeach()

if(ESP_SR_COMPONENT_PATH)
    set(ESP_SR_MODEL_PATH "${ESP_SR_COMPONENT_PATH}/model")
    message(STATUS "找到 ESP-SR 模型路径: ${ESP_SR_MODEL_PATH}")
else()
    message(WARNING "未找到 ESP-SR 组件，跳过 assets 打包")
    set(ESP_SR_MODEL_PATH "")
endif()

# 从 Kconfig 读取配置
if(CONFIG_ENABLE_ASSETS_BUILD)
    # 解析模型列表（空格分隔的字符串转换为列表）
    # 支持多种模型类型：WakeNet, VADNet, NSNet, MultiNet
    string(REPLACE " " ";" ASSETS_MODELS "${CONFIG_ASSETS_MODELS}")
    
    # 唤醒词配置（仅用于 Multinet 模型）
    set(CN_WAKE_WORD "${CONFIG_ASSETS_CN_WAKE_WORD}")
    set(EN_WAKE_WORD "${CONFIG_ASSETS_EN_WAKE_WORD}")
    set(WAKE_WORD_THRESHOLD "${CONFIG_ASSETS_WAKE_WORD_THRESHOLD}")
else()
    # Assets 构建已禁用
    set(ASSETS_MODELS "")
    set(CN_WAKE_WORD "")
    set(EN_WAKE_WORD "")
    set(WAKE_WORD_THRESHOLD "0.2")
endif()

# 函数：构建 assets.bin
function(build_assets_bin)
    if(NOT CONFIG_ENABLE_ASSETS_BUILD)
        message(STATUS "Assets 构建已禁用 (Kconfig)")
        return()
    endif()
    
    if(NOT ESP_SR_MODEL_PATH)
        message(WARNING "ESP-SR 模型路径未设置，跳过 assets 打包")
        return()
    endif()
    
    if(NOT ASSETS_MODELS)
        message(WARNING "未配置模型，跳过 assets 打包")
        return()
    endif()
    
    # 设置输出路径
    set(GENERATED_ASSETS_BIN "${CMAKE_BINARY_DIR}/generated_assets.bin")
    
    # 准备参数列表
    set(BUILD_CMD python ${PROJECT_DIR}/scripts/build_assets.py
        --esp_sr_model_path "${ESP_SR_MODEL_PATH}"
        --output "${GENERATED_ASSETS_BIN}"
        --models
    )
    # 添加模型列表（每个模型作为单独的参数）
    list(APPEND BUILD_CMD ${ASSETS_MODELS})
    # 只在非空时添加唤醒词参数
    if(CN_WAKE_WORD AND NOT CN_WAKE_WORD STREQUAL "")
        list(APPEND BUILD_CMD --cn_wake_word "${CN_WAKE_WORD}")
    endif()
    if(EN_WAKE_WORD AND NOT EN_WAKE_WORD STREQUAL "")
        list(APPEND BUILD_CMD --en_wake_word "${EN_WAKE_WORD}")
    endif()
    # 添加阈值参数
    list(APPEND BUILD_CMD --threshold "${WAKE_WORD_THRESHOLD}")
    
    # 创建自定义命令
    add_custom_command(
        OUTPUT ${GENERATED_ASSETS_BIN}
        COMMAND ${BUILD_CMD}
        DEPENDS
            ${PROJECT_DIR}/scripts/build_assets.py
        COMMENT "构建 assets.bin（支持多种 ESP-SR 模型）"
        VERBATIM
        WORKING_DIRECTORY ${PROJECT_DIR}
    )
    
    # 创建目标（ALL 表示每次构建都执行）
    add_custom_target(generated_assets ALL
        DEPENDS ${GENERATED_ASSETS_BIN}
    )
    
    # 设置输出文件路径到父作用域
    set(GENERATED_ASSETS_LOCAL_FILE ${GENERATED_ASSETS_BIN} PARENT_SCOPE)
    
    # 转换为字符串用于显示
    string(REPLACE ";" " " ASSETS_MODELS_STR "${ASSETS_MODELS}")
    
    message(STATUS "Assets 打包配置 (Kconfig):")
    message(STATUS "  输出文件: ${GENERATED_ASSETS_BIN}")
    message(STATUS "  模型列表: ${ASSETS_MODELS_STR}")
    if(CN_WAKE_WORD)
    message(STATUS "  中文唤醒词: ${CN_WAKE_WORD}")
    else()
        message(STATUS "  中文唤醒词: (未配置)")
    endif()
    if(EN_WAKE_WORD)
    message(STATUS "  英文唤醒词: ${EN_WAKE_WORD}")
    else()
        message(STATUS "  英文唤醒词: (未配置)")
    endif()
    message(STATUS "  阈值: ${WAKE_WORD_THRESHOLD}")
endfunction()

# 检查 assets 分区是否存在
partition_table_get_partition_info(size "--partition-name assets" "size")
partition_table_get_partition_info(offset "--partition-name assets" "offset")

if("${size}" AND "${offset}")
    # 构建 assets.bin（仅在启用时）
    if(CONFIG_ENABLE_ASSETS_BUILD)
    build_assets_bin()
    
    # 配置自动烧录（在 flash 目标中）
    if(DEFINED GENERATED_ASSETS_LOCAL_FILE)
        esptool_py_flash_to_partition(flash "assets" "${GENERATED_ASSETS_LOCAL_FILE}")
        message(STATUS "Assets 自动烧录已配置: ${GENERATED_ASSETS_LOCAL_FILE} -> assets 分区")
        endif()
    else()
        message(STATUS "Assets 构建已禁用 (Kconfig: CONFIG_ENABLE_ASSETS_BUILD=n)")
    endif()
else()
    message(STATUS "未找到 assets 分区，跳过 assets 打包")
endif()
