cmake_minimum_required(VERSION 2.8)

project(test_xcb_xdamage)

find_package(PkgConfig)

pkg_check_modules(XCB xcb)
pkg_check_modules(XCBDAMAGE xcb-damage)

# message(${XCB_INCLUDE_DIRS} ${XCB_LIBRARIES})

add_executable(${PROJECT_NAME} "main.c")

target_link_libraries(${PROJECT_NAME} ${XCB_LIBRARIES} ${XCBDAMAGE_LIBRARIES})
