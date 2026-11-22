cmake_minimum_required(VERSION 3.24)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SUNLENS_VERSION LIMIT_COUNT 1)
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

qt_add_executable(sunlens src/app/main.cpp)
target_include_directories(sunlens PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(sunlens PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2)
qt_add_qml_module(sunlens
    URI Sunlens
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES qml/Main.qml
    RESOURCES assets/logo.svg assets/pattern.svg
)
if(MSVC)
    target_compile_options(sunlens PRIVATE /W4 /permissive-)
else()
    target_compile_options(sunlens PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS sunlens RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES assets/sunlens.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES assets/logo.svg DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
    RENAME sunlens.svg)
install(FILES VERSION LICENSE third_party/NOTICE.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/sunlens")

if(BUILD_TESTING)
    add_test(NAME app-version COMMAND sunlens --version)
    set_tests_properties(app-version PROPERTIES PASS_REGULAR_EXPRESSION "SUNLENS ${PROJECT_VERSION}"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_test(NAME gui-smoke COMMAND sunlens --smoke-test)
    set_tests_properties(gui-smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software"
        TIMEOUT 20)
endif()
