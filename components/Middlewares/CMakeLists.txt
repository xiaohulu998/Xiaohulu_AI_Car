# Middlewares（中间业务层）CMakeLists.txt文件


# 源码子文件目录
set(src_dirs
            src
            )

# 对外暴露头文件目录，其他文件 #include "XX.h" 可直接找到 Middlewares/XX/XX.h
set(include_dirs
            include
            )
# 当前Middlewares组件依赖组件目录
set(requires
            BSP
            driver
            freertos
            mqtt
            esp_http_client
            esp_https_ota
            app_update
            json
            mbedtls
            esp_system
            )

# 注册【Middlewares 组件】到组件列表中, 以便其他组件可以依赖使用
idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

# 编译选项：1. 浮点运算加速，精度轻微下降；2. 最高速度优化等级(O2兼容http_server)；3. printf格式错误只警告，不直接编译失败；4. 屏蔽printf格式相关警告。
component_compile_options(-ffast-math -O2 -Wno-error=format -Wno-format)

# 编译HTML文件夹下的网页资源，生成spiffs分区镜像文件 
#参数说明：1. 分区名称；2. 网页资源目录；3. 生成的分区镜像文件是否放入flash分区中
spiffs_create_partition_image(html "${CMAKE_CURRENT_SOURCE_DIR}/HTML" FLASH_IN_PROJECT)  