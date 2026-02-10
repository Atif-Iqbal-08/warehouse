cmake_minimum_required(VERSION 3.16)
project(warehouse_sku_generator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Widgets Sql PrintSupport QUIET)
if(Qt6_FOUND)
  set(QT Qt6)
else()
  find_package(Qt5 COMPONENTS Widgets Sql PrintSupport REQUIRED)
  set(QT Qt5)
endif()

add_executable(warehouse_sku_generator
  src/main.cpp
  src/printsettingsdialog.h
  src/printsettingsdialog.cpp
  src/mainwindow.h
  src/globals.h
  src/app_settings.h
  src/qrcodegen.hpp
  src/qrcodegen.cpp
  src/mainwindow.cpp
  src/mainwindow_history.cpp
  src/assets.qrc
  src/mainwindow.ui
)

target_link_libraries(warehouse_sku_generator PRIVATE ${QT}::Widgets ${QT}::Sql)
target_link_libraries(warehouse_sku_generator PRIVATE ${QT}::PrintSupport)

if (MSVC)
  target_compile_options(warehouse_sku_generator PRIVATE /W4)
else()
  target_compile_options(warehouse_sku_generator PRIVATE -Wall -Wextra -Wpedantic)
endif()
