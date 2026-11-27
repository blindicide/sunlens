cmake_minimum_required(VERSION 3.24)
set(version_file "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VERSION")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${version_file}")
file(STRINGS "${version_file}" SUNLENS_VERSION LIMIT_COUNT 1)
if(NOT SUNLENS_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "VERSION: требуется версия MAJOR.MINOR.PATCH")
endif()
project(sunlens VERSION "${SUNLENS_VERSION}" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)
include(GNUInstallDirs)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Sql)
qt_standard_project_setup()
configure_file(cmake/version.hpp.in generated/sunlens/version.hpp @ONLY)

add_library(sunlens_proxy STATIC src/app/proxy.cpp src/app/proxy.hpp)
target_include_directories(sunlens_proxy PUBLIC src/app)
target_link_libraries(sunlens_proxy PUBLIC Qt6::Network)

qt_add_executable(sunlens src/app/main.cpp)
target_include_directories(sunlens PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(sunlens PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 sunlens_proxy)
qt_add_qml_module(sunlens
    URI Sunlens
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES qml/Main.qml
    RESOURCES assets/logo.svg assets/pattern.svg
)
if(MSVC)
    target_compile_options(sunlens PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(sunlens_proxy PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(sunlens PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(sunlens_proxy PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS sunlens RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES assets/sunlens.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES assets/logo.svg DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
    RENAME sunlens.svg)
install(FILES "${version_file}" LICENSE third_party/NOTICE.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/sunlens")

if(BUILD_TESTING)
    add_executable(proxy_tests tests/proxy_tests.cpp)
    target_link_libraries(proxy_tests PRIVATE sunlens_proxy)
    add_test(NAME proxy-settings-and-probe COMMAND proxy_tests)
    set_tests_properties(proxy-settings-and-probe PROPERTIES TIMEOUT 12)
    add_test(NAME app-version COMMAND sunlens --version)
    string(REPLACE "." "[.]" VERSION_REGEX "${PROJECT_VERSION}")
    set_tests_properties(app-version PROPERTIES PASS_REGULAR_EXPRESSION "SUNLENS ${VERSION_REGEX}"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_test(NAME gui-smoke COMMAND sunlens --smoke-test)
    set_tests_properties(gui-smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
        TIMEOUT 20)
endif()
