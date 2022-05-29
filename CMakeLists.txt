cmake_minimum_required (VERSION 3.8)

project ("TaskProject")
# 将源代码添加到此项目的可执行文件。
add_executable (TaskProject "task.cpp")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET TaskProject PROPERTY CXX_STANDARD 20)
endif()
