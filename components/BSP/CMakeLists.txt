# BSP组件（硬件底层驱动层）CMakeLists.txt文件

#[[ 
作用：负责屏蔽 ESP32-S3 芯片寄存器、GPIO、外设时序，给上层提供标准化硬件操作接口。
特点：
    2.1 强硬件绑定，换一块硬件板子就要修改 BSP 代码；
    2.2 只做读写硬件寄存器、电平、时序，不处理业务逻辑；
    2.3 不依赖中间件，可独立编译测试硬件是否正常。
]]

# 源码子文件目录
set(src_dirs
            WIFI    
            WIFI_M
            )

# 对外暴露头文件目录，其他文件 #include "XX.h" 可直接找到 BSP/XX/XX.h
set(include_dirs
            WIFI
            WIFI_M
            HTML
            )
# 当前BSP组件依赖官方组件目录
set(requires
            driver
            esp_wifi
            nvs_flash
            esp_netif
            esp_http_server
            esp_timer
            spiffs
            vfs
            json
                )

# 注册【BSP组件】到组件列表中, 以便其他组件可以依赖使用
idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

# 编译选项：1. 浮点运算加速，精度轻微下降；2. 最高速度优化等级(O2兼容http_server)；3. printf格式错误只警告，不直接编译失败；4. 屏蔽printf格式相关警告。
component_compile_options(-ffast-math -O2 -Wno-error=format -Wno-format)

# 编译HTML文件夹下的网页资源，生成spiffs分区镜像文件 
#参数说明：1. 分区名称；2. 网页资源目录；3. 生成的分区镜像文件是否放入flash分区中
spiffs_create_partition_image(html "${CMAKE_CURRENT_SOURCE_DIR}/HTML" FLASH_IN_PROJECT)  