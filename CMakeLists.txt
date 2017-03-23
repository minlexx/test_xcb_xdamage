cmake_minimum_required(VERSION 2.8)

project(test_xcb_xdamage)

find_package(PkgConfig)

pkg_check_modules(XCB xcb)
pkg_check_modules(XCBDAMAGE xcb-damage)
pkg_check_modules(XCBSHM xcb-shm)
pkg_check_modules(XCBIMAGE xcb-image)
pkg_check_modules(XCBRANDR xcb-randr)

pkg_check_modules(LIBPNG libpng)

# message(${XCB_INCLUDE_DIRS} ${XCB_LIBRARIES})

add_executable(${PROJECT_NAME} "main.c" "png_saver.c")

target_include_directories(${PROJECT_NAME} PUBLIC
    ${LIBPNG_INCLUDEDIR}
    ${XCB_INCLUDEDIR}
)

target_link_libraries(${PROJECT_NAME}
    ${XCB_LIBRARIES}
    ${XCBDAMAGE_LIBRARIES}
    ${XCBSHM_LIBRARIES}
    ${XCBIMAGE_LIBRARIES}
    ${XCBRANDR_LIBRARIES}
    ${LIBPNG_LIBRARIES}
)
