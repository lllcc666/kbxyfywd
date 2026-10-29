cmake_minimum_required(VERSION 3.16)

# 必须声明 C 语言：third_party/zlib 是 C 源文件，只声明 CXX 时 CMake 不会编译 .c，
# 那些符号就会在链接时退化到 vcpkg 的 zlib.lib 导入库，等于重新依赖 zlib1.dll。
project(WebView2Demo C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 项目目录结构
set(SCRIPTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
set(RESOURCES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(EMBEDDED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/embedded)
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 添加可执行文件（Win32 窗口应用程序）
add_executable(WebView2Demo WIN32
    ${SRC_DIR}/app/demo.cpp
    ${SRC_DIR}/app/app_host.cpp
    ${SRC_DIR}/hook/response_waiter.cpp
    ${SRC_DIR}/hook/wpe_hook.cpp
    ${SRC_DIR}/hook/horse_competition.cpp
    ${SRC_DIR}/hook/latest_activities.cpp
    ${SRC_DIR}/hook/wpe_hook_helpers.cpp
    ${SRC_DIR}/hook/hook_shared.cpp
    ${SRC_DIR}/hook/dungeon_jump.cpp
    ${SRC_DIR}/hook/battle_six.cpp
    ${SRC_DIR}/hook/spirit_collect.cpp
    ${SRC_DIR}/hook/shuangtai.cpp
    ${SRC_DIR}/hook/story_task.cpp
    ${SRC_DIR}/hook/eight_trigrams.cpp
    ${SRC_DIR}/hook/activity_minigames.cpp
    ${SRC_DIR}/hook/activity_counts.cpp
    ${SRC_DIR}/hook/battle.cpp
    ${SRC_DIR}/hook/battle_activity_hooks.cpp
    ${SRC_DIR}/hook/collect.cpp
    ${SRC_DIR}/hook/dungeon_deepdig.cpp
    ${SRC_DIR}/hook/bag_items.cpp
    ${SRC_DIR}/hook/daily_activity.cpp
    ${SRC_DIR}/hook/little_game.cpp
    ${SRC_DIR}/hook/activity_bus.cpp
    ${SRC_DIR}/hook/md5_check.cpp
    ${SRC_DIR}/hook/trial.cpp
    ${SRC_DIR}/hook/tower.cpp
    ${SRC_DIR}/hook/dance.cpp
    ${SRC_DIR}/hook/hijack.cpp
    ${SRC_DIR}/hook/packet_store.cpp
    ${SRC_DIR}/core/utils.cpp
    ${SRC_DIR}/core/managed_thread.cpp
    ${SRC_DIR}/protocol/packet_parser.cpp
    ${SRC_DIR}/protocol/battle_session.cpp
    ${SRC_DIR}/protocol/packet_builder.cpp
    ${SRC_DIR}/core/ui_bridge.cpp
    ${SRC_DIR}/core/web_message_handler.cpp

    # 内嵌 zlib（只保留解压/压缩实现，gz* 文件读写不参与编译）
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/adler32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/crc32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/zutil.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/inflate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/inftrees.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/inffast.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/uncompr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/compress.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/deflate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/trees.c
)

# 设置编译器使用 UTF-8 编码
if(MSVC)
    target_compile_options(WebView2Demo PRIVATE /utf-8)
    # /W4：警告等级提升到 Level4，用于拦截未使用变量/参数、收窄转换、
    # 有符号无符号混用等问题。暂不开启 /WX，避免历史代码阻断构建。
    target_compile_options(WebView2Demo PRIVATE /W4)
    # 全仓库没有 dynamic_cast/typeid，关掉 RTTI 省掉 type_info 与类名字符串。
    target_compile_options(WebView2Demo PRIVATE /GR-)
    target_include_directories(WebView2Demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib)

    # 第三方 zlib 源码在 /W4 下会刷一堆无关告警（C4244/C4127），单独降到 /W0。
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/adler32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/crc32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/zutil.c
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/inflate.c
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/inftrees.c
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/inffast.c
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/uncompr.c
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/compress.c
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/deflate.c
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/trees.c
        PROPERTIES COMPILE_OPTIONS "/W0")
endif()

# 设置 vcpkg 路径（本机绝对路径，换机器须修改）
set(VCPKG_ROOT "d:/AItrace/CE/.trae/vcpkg-master")
set(VCPKG_INCLUDE_DIR "${VCPKG_ROOT}/installed/x64-windows/include")
set(VCPKG_LIB_DIR "${VCPKG_ROOT}/installed/x64-windows/lib")

# 包含 WebView2 头文件和嵌入数据目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${VCPKG_INCLUDE_DIR})
include_directories(${EMBEDDED_DIR})
include_directories(${INCLUDE_DIR}/activities)
include_directories(${INCLUDE_DIR}/core)
include_directories(${INCLUDE_DIR}/hook)
include_directories(${INCLUDE_DIR}/internal)
include_directories(${INCLUDE_DIR}/protocol)

# 链接其他必要库
target_link_libraries(WebView2Demo PRIVATE
    ComCtl32.lib
    Comdlg32.lib
    dwmapi.lib
    mmdevapi.lib
    oleaut32.lib
    ws2_32.lib
    ole32.lib
    shell32.lib
    shlwapi.lib
    uuid.lib
    user32.lib
    advapi32.lib
    msimg32.lib
    gdi32.lib
    winhttp.lib
    wininet.lib
    winmm.lib
    "${VCPKG_LIB_DIR}/MemoryModule.lib"
)

# 添加资源文件并声明 app.ico 依赖
set_source_files_properties(${RESOURCES_DIR}/app.rc PROPERTIES 
    OBJECT_DEPENDS "${RESOURCES_DIR}/app.ico"
)
target_sources(WebView2Demo PRIVATE ${RESOURCES_DIR}/app.rc)

# 为资源编译器添加包含目录
set_property(SOURCE ${RESOURCES_DIR}/app.rc PROPERTY COMPILE_FLAGS "/I\"${RESOURCES_DIR}\"")

# 添加自定义命令：把 WebView2Loader.dll 转换为头文件
add_custom_command(
    OUTPUT ${EMBEDDED_DIR}/webview2loader_data.h
    COMMAND python3 ${SCRIPTS_DIR}/embed_dll.py
    "${VCPKG_ROOT}/installed/x64-windows/bin/WebView2Loader.dll"
    ${EMBEDDED_DIR}/webview2loader_data.h
    DEPENDS "${VCPKG_ROOT}/installed/x64-windows/bin/WebView2Loader.dll"
    COMMENT "Embedding WebView2Loader.dll into header file..."
)

# 添加自定义命令：把 ui.html 转换为头文件
add_custom_command(
    OUTPUT ${EMBEDDED_DIR}/ui_html.h
    COMMAND python3 ${SCRIPTS_DIR}/embed_html.py
    ${RESOURCES_DIR}/ui.html
    ${EMBEDDED_DIR}/ui_html.h
    DEPENDS ${RESOURCES_DIR}/ui.html
    COMMENT "Embedding UI.html into header file..."
)

# 将生成的头文件加入源文件列表
target_sources(WebView2Demo PRIVATE
    ${EMBEDDED_DIR}/webview2loader_data.h
    ${EMBEDDED_DIR}/ui_html.h
)

# 设置输出目录
set_target_properties(WebView2Demo PROPERTIES
    # VS 的 vcpkg 集成会给每个工程的链接命令行追加 "…\installedd-windows\lib\*.lib"
    # 并把依赖 DLL 拷到输出目录。那条通配会把 zlib.lib（zlib1.dll 的导入库）也链接进来，
    # 于是 exe 明明自己编了 zlib 源码，却仍然依赖 zlib1.dll；同时把 MemModule 之外的东西也带进来。
    # 我们只按完整路径显式链接 MemoryModule.lib，头文件也用自己的，所以这里整体关掉。
    VS_GLOBAL_VcpkgEnabled "false"
    VS_GLOBAL_VcpkgAutoLink "false"
    VS_GLOBAL_VcpkgApplocalDeps "false"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Release 配置：体积优先的编译与链接选项（/O1 /GL /GF /Gy /Gw 与 /LTCG）
if(MSVC)
    # Release 侧更偏向体积压缩，减少可执行文件和装载后的常驻体积。
    target_compile_options(WebView2Demo PRIVATE
        $<$<CONFIG:Release>:/O1>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Release>:/GF>
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Release>:/Gw>
        $<$<CONFIG:Release>:/Ob2>
        $<$<CONFIG:Release>:/EHsc>
    )

    target_link_options(WebView2Demo PRIVATE
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        $<$<CONFIG:Release>:/INCREMENTAL:NO>
    )

    # Use dynamic CRT to stay compatible with the vcpkg-built libraries.
    set_property(TARGET WebView2Demo PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

# 让 VS 解决方案按目录分组，避免 60+ 源文件平铺。
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "src" FILES
    ${SRC_DIR}/app/demo.cpp
    ${SRC_DIR}/app/app_host.cpp
    ${SRC_DIR}/protocol/packet_parser.cpp
    ${SRC_DIR}/protocol/battle_session.cpp
)
