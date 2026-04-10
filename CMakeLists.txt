cmake_minimum_required(VERSION 3.16)

project(WebView2Demo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 妞ゅ湱娲伴惄顔肩秿缂佹挻鐎?
set(SCRIPTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
set(RESOURCES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(EMBEDDED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/embedded)
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 濞ｈ濮為崣顖涘⒔鐞涘本鏋冩禒璁圭礄Win32 缁愭褰涙惔鏃傛暏缁嬪绨敍?
add_executable(WebView2Demo WIN32
    ${SRC_DIR}/app/demo.cpp
    ${SRC_DIR}/app/app_host.cpp
    ${SRC_DIR}/hook/wpe_hook.cpp
    ${SRC_DIR}/hook/horse_competition.cpp
    ${SRC_DIR}/hook/wpe_hook_helpers.cpp
    ${SRC_DIR}/core/utils.cpp
    ${SRC_DIR}/protocol/packet_parser.cpp
    ${SRC_DIR}/protocol/packet_builder.cpp
    ${SRC_DIR}/core/ui_bridge.cpp
    ${SRC_DIR}/core/web_message_handler.cpp
)

# 鐠佸墽鐤嗙紓鏍槯閸ｃ劋濞囬悽鈺慣F-8缂傛牜鐖?
if(MSVC)
    target_compile_options(WebView2Demo PRIVATE /utf-8)
endif()

# 鐠佸墽鐤?vcpkg 鐠侯垰绶?
set(VCPKG_ROOT "d:/AItrace/CE/.trae/vcpkg-master")
set(VCPKG_INCLUDE_DIR "${VCPKG_ROOT}/installed/x64-windows/include")
set(VCPKG_LIB_DIR "${VCPKG_ROOT}/installed/x64-windows/lib")

# 閸栧懎鎯?WebView2 婢跺瓨鏋冩禒璺烘嫲瀹撳苯鍙嗗蹇旀殶閹诡喚娲拌ぐ?
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${VCPKG_INCLUDE_DIR})
include_directories(${EMBEDDED_DIR})
include_directories(${INCLUDE_DIR}/activities)
include_directories(${INCLUDE_DIR}/core)
include_directories(${INCLUDE_DIR}/hook)
include_directories(${INCLUDE_DIR}/internal)
include_directories(${INCLUDE_DIR}/protocol)

# 闁剧偓甯撮崗鏈电铂韫囧懓顩︽惔?
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
    "${VCPKG_LIB_DIR}/zlib.lib"
)

# 濞ｈ濮炵挧鍕爱閺傚洣娆?- 鐠佸墽鐤嗗锝団€橀惃鍕紣娴ｆ粎娲拌ぐ?
set_source_files_properties(${RESOURCES_DIR}/app.rc PROPERTIES 
    OBJECT_DEPENDS "${RESOURCES_DIR}/app.ico"
)
target_sources(WebView2Demo PRIVATE ${RESOURCES_DIR}/app.rc)

# 娑撻缚绁┃鎰椽鐠囨垵娅掑ǎ璇插閸栧懎鎯堥惄顔肩秿
set_property(SOURCE ${RESOURCES_DIR}/app.rc PROPERTY COMPILE_FLAGS "/I\"${RESOURCES_DIR}\"")

# 濞ｈ濮為懛顏勭暰娑斿鎳℃禒銈忕窗鐏忓摛ebView2Loader.dll鏉烆剚宕叉稉绡?+婢跺瓨鏋冩禒?
add_custom_command(
    OUTPUT ${EMBEDDED_DIR}/webview2loader_data.h
    COMMAND python3 ${SCRIPTS_DIR}/embed_dll.py
    "${VCPKG_ROOT}/installed/x64-windows/bin/WebView2Loader.dll"
    ${EMBEDDED_DIR}/webview2loader_data.h
    DEPENDS "${VCPKG_ROOT}/installed/x64-windows/bin/WebView2Loader.dll"
    COMMENT "Embedding WebView2Loader.dll into header file..."
)

# 濞ｈ濮為懛顏勭暰娑斿鎳℃禒銈忕窗鐏忓敇i.html鏉烆剚宕叉稉绡?+婢跺瓨鏋冩禒?
add_custom_command(
    OUTPUT ${EMBEDDED_DIR}/ui_html.h
    COMMAND python3 ${SCRIPTS_DIR}/embed_html.py
    ${RESOURCES_DIR}/ui.html
    ${EMBEDDED_DIR}/ui_html.h
    DEPENDS ${RESOURCES_DIR}/ui.html
    COMMENT "Embedding UI.html into header file..."
)

# 濞ｈ濮為懛顏勭暰娑斿鎳℃禒銈忕窗鐏忓敒lib1.dll鏉烆剚宕叉稉绡?+婢跺瓨鏋冩禒?
add_custom_command(
    OUTPUT ${EMBEDDED_DIR}/zlib_data.h
    COMMAND python3 ${SCRIPTS_DIR}/embed_dll.py
    "${VCPKG_ROOT}/installed/x64-windows/bin/zlib1.dll"
    ${EMBEDDED_DIR}/zlib_data.h
    DEPENDS "${VCPKG_ROOT}/installed/x64-windows/bin/zlib1.dll"
    COMMENT "Embedding zlib1.dll into header file..."
)

# 鐏忓棛鏁撻幋鎰畱婢跺瓨鏋冩禒鑸靛潑閸旂姴鍩屽┃鎰瀮娴犺泛鍨悰?
target_sources(WebView2Demo PRIVATE
    ${EMBEDDED_DIR}/webview2loader_data.h
    ${EMBEDDED_DIR}/ui_html.h
    ${EMBEDDED_DIR}/zlib_data.h
)

# 鐠佸墽鐤嗘潏鎾冲毉閻╊喖缍?
set_target_properties(WebView2Demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Release 浣跨敤鏍囧噯浼樺寲锛岄伩鍏嶈繃搴︽縺杩涚殑浣撶Н鍘嬬缉褰卞搷鍏煎鎬?
if(MSVC)
    target_compile_options(WebView2Demo PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GF>
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Release>:/Ob2>
        $<$<CONFIG:Release>:/EHsc>
    )

    target_link_options(WebView2Demo PRIVATE
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        $<$<CONFIG:Release>:/INCREMENTAL:NO>
    )

    # Use dynamic CRT to stay compatible with the vcpkg-built libraries.
    set_property(TARGET WebView2Demo PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()
