# Define source files
set(SOURCES "app/assets/assets.cc"
            "app/battery/battery.cc"
            "app/common/i2c/i2c.cc"
            "app/device/button/button.cc"
            "app/device/led/led.cc"
            "app/device/touch/touch.cc"
            "app/media/audio/audio.cc"
            "app/media/audio/processor/processor.cc"
            "app/media/audio/wakeword/wakeword.cc"
            # "app/media/camera/camera.cc"
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
                 "app/common/i2c"
                 "app/device/button" 
                 "app/device/led"
                 "app/device/touch"
                 "app/media/audio"
                 "app/media/audio/processor"
                 "app/media/audio/wakeword"
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

# 配置自定义唤醒词模型（可通过 CMake 变量覆盖，例如：-DMULTINET_MODELS="mn6_cn;mn6_en"）
if(NOT DEFINED MULTINET_MODELS)
    # 默认使用中文和英文模型
    set(MULTINET_MODELS "mn6_cn" "mn6_en")
endif()

# 唤醒词配置（可选，如果不设置则不在 index.json 中包含唤醒词配置）
# 用户可以在运行时动态添加唤醒词
if(NOT DEFINED CN_WAKE_WORD)
    set(CN_WAKE_WORD "")  # 空字符串表示不配置
endif()

if(NOT DEFINED EN_WAKE_WORD)
    set(EN_WAKE_WORD "")  # 空字符串表示不配置
endif()

if(NOT DEFINED WAKE_WORD_THRESHOLD)
    set(WAKE_WORD_THRESHOLD "0.2")
endif()

# 函数：构建 assets.bin
function(build_assets_bin)
    if(NOT ESP_SR_MODEL_PATH)
        message(WARNING "ESP-SR 模型路径未设置，跳过 assets 打包")
        return()
    endif()
    
    # 设置输出路径
    set(GENERATED_ASSETS_BIN "${CMAKE_BINARY_DIR}/generated_assets.bin")
    
    # 准备参数列表
    set(BUILD_CMD python ${PROJECT_DIR}/scripts/build_assets.py
        --esp_sr_model_path "${ESP_SR_MODEL_PATH}"
        --output "${GENERATED_ASSETS_BIN}"
        --multinet_models
    )
    # 添加模型列表（每个模型作为单独的参数）
    list(APPEND BUILD_CMD ${MULTINET_MODELS})
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
        COMMENT "构建自定义唤醒词 assets.bin"
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
    string(REPLACE ";" " " MULTINET_MODELS_STR "${MULTINET_MODELS}")
    
    message(STATUS "Assets 打包配置: ${GENERATED_ASSETS_BIN}")
    message(STATUS "  Multinet 模型: ${MULTINET_MODELS_STR}")
    message(STATUS "  中文唤醒词: ${CN_WAKE_WORD}")
    message(STATUS "  英文唤醒词: ${EN_WAKE_WORD}")
    message(STATUS "  阈值: ${WAKE_WORD_THRESHOLD}")
endfunction()

# 检查 assets 分区是否存在
partition_table_get_partition_info(size "--partition-name assets" "size")
partition_table_get_partition_info(offset "--partition-name assets" "offset")

if("${size}" AND "${offset}")
    # 构建 assets.bin
    build_assets_bin()
    
    # 配置自动烧录（在 flash 目标中）
    if(DEFINED GENERATED_ASSETS_LOCAL_FILE)
        esptool_py_flash_to_partition(flash "assets" "${GENERATED_ASSETS_LOCAL_FILE}")
        message(STATUS "Assets 自动烧录已配置: ${GENERATED_ASSETS_LOCAL_FILE} -> assets 分区")
    endif()
else()
    message(STATUS "未找到 assets 分区，跳过 assets 打包")
endif()
