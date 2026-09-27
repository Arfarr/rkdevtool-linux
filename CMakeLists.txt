cmake_minimum_required(VERSION 3.16)
project(rkdevtool_linux VERSION 2.93 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

add_executable(RKDevTool
  src/main.cpp
  src/mainwindow.cpp
  src/mainwindow.h
  src/usbscanner.cpp
  src/usbscanner.h
  src/upgrader.cpp
  src/upgrader.h
  src/paths.cpp
  src/paths.h
)

target_link_libraries(RKDevTool PRIVATE
  Qt5::Core Qt5::Gui Qt5::Widgets
  PkgConfig::LIBUSB
)

target_compile_definitions(RKDevTool PRIVATE
  RKDEVTOOL_VERSION="2.93"
)

install(TARGETS RKDevTool RUNTIME DESTINATION bin)
# desktop 模板含 @EXEC@ 占位符，install.sh 会生成填好绝对路径的版本到 build/
if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/rkdevtool.desktop")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rkdevtool.desktop"
          DESTINATION share/applications)
else()
  message(WARNING "build/rkdevtool.desktop 不存在，将安装未填充 @EXEC@ 的模板。"
                  "请通过 install.sh 安装以获得可用的启动项。")
  install(FILES packaging/rkdevtool.desktop DESTINATION share/applications)
endif()
install(FILES rules/51-rkdevtool.rules DESTINATION lib/udev/rules.d)
# 启动器必须用 PROGRAMS 安装，否则不带执行权限，
# 会导致 desktop 文件的 TryExec 检查失败、GNOME 菜单里搜不到条目
install(PROGRAMS packaging/rkdevtool.sh DESTINATION bin RENAME rkdevtool-launch)
install(FILES resources/rkdevtool.png
        DESTINATION share/icons/hicolor/128x128/apps)
