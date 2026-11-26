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
if(UNIX AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Каталог установки" FORCE)
endif()
include(GNUInstallDirs)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Права пакета не должны зависеть от umask пользователя сборки.
    set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Sql Concurrent)
qt_standard_project_setup()
configure_file(cmake/version.hpp.in generated/sunlens/version.hpp @ONLY)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/dependencies.json" SUNLENS_DEPENDENCIES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS dependencies.json VERSION)
string(JSON SING_BOX_VERSION GET "${SUNLENS_DEPENDENCIES}" cores sing-box version)
string(JSON SING_BOX_LINUX_HASH GET "${SUNLENS_DEPENDENCIES}"
    cores sing-box assets linux-amd64 binarySha256)
string(JSON XRAY_VERSION GET "${SUNLENS_DEPENDENCIES}" cores xray version)
string(JSON XRAY_LINUX_HASH GET "${SUNLENS_DEPENDENCIES}"
    cores xray assets linux-amd64 binarySha256)
configure_file(cmake/core-dependencies.hpp.in generated/sunlens/core-dependencies.hpp @ONLY)

qt_add_library(sunlens-domain STATIC
    src/subscriptions/profile.cpp
    src/subscriptions/subscription-manager.cpp
    src/persistence/profile-repository.cpp
    src/app/application-controller.cpp
    src/app/controller-analytics.cpp
    src/analytics/analytics-repository.cpp
    src/core/sing-box-adapter.cpp
    src/core/xray-adapter.cpp
    src/core/core-options.cpp
    src/core/external-core.cpp
    src/core/profile-check.cpp
    src/tunnel/tunnel-types.cpp
    src/tunnel/recovery-journal.cpp
    src/tunnel/tunnel-manager.cpp
    src/tunnel/helper-client.cpp
    include/sunlens/tunnel.hpp
    include/sunlens/tunnel-client.hpp
    include/sunlens/subscription-manager.hpp
    include/sunlens/application-controller.hpp
)
target_include_directories(sunlens-domain PUBLIC include)
target_include_directories(sunlens-domain PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(sunlens-domain PUBLIC Qt6::Core Qt6::Network Qt6::Sql Qt6::Concurrent)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    configure_file(cmake/helper-paths.hpp.in generated/sunlens/helper-paths.hpp @ONLY)
    target_sources(sunlens-domain PRIVATE src/platform/linux/linux-tunnel-platform.cpp)
    qt_add_executable(sunlens-tunnel-helper src/platform/linux/helper-main.cpp)
    target_include_directories(sunlens-tunnel-helper PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(sunlens-tunnel-helper PRIVATE sunlens-domain)
    configure_file(packaging/linux/org.sunlens.tunnel.policy.in org.sunlens.tunnel.policy @ONLY)
    configure_file(packaging/linux/sunlens-recover.service.in sunlens-recover.service @ONLY)
    install(TARGETS sunlens-tunnel-helper RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/sunlens")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.sunlens.tunnel.policy"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/polkit-1/actions")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sunlens-recover.service"
        DESTINATION "lib/systemd/system")
endif()

qt_add_executable(sunlens src/app/main.cpp)
target_include_directories(sunlens PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(sunlens PRIVATE sunlens-domain Qt6::Gui Qt6::Quick Qt6::QuickControls2)
qt_add_qml_module(sunlens
    URI Sunlens
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES qml/Main.qml
    RESOURCES assets/logo.svg assets/pattern.svg
)
foreach(target IN ITEMS sunlens sunlens-domain)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
# MSVC должен читать русские сообщения и тестовые строки как UTF-8.
if(MSVC)
    add_compile_options(/utf-8)
endif()

install(TARGETS sunlens RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES assets/sunlens.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES assets/logo.svg DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
    RENAME sunlens.svg)
install(FILES VERSION LICENSE third_party/NOTICE.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/sunlens")
install(FILES dependencies.json DESTINATION "${CMAKE_INSTALL_DATADIR}/sunlens")

option(SUNLENS_PACKAGE_DEB "Собрать DEB для Ubuntu 24.04" OFF)
option(SUNLENS_PACKAGE_RPM "Собрать RPM для Fedora 44" OFF)
if(SUNLENS_PACKAGE_DEB AND SUNLENS_PACKAGE_RPM)
    message(FATAL_ERROR "Используйте отдельную сборку для каждого baseline пакетов")
endif()
if(SUNLENS_PACKAGE_DEB)
    include(cmake/debian-package.cmake)
elseif(SUNLENS_PACKAGE_RPM)
    include(cmake/fedora-package.cmake)
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME core-acquisition
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/test-fetch-core.py")
    foreach(suite IN ITEMS profiles subscriptions core tunnel analytics)
        qt_add_executable(test-${suite} tests/unit/test-${suite}.cpp)
        target_link_libraries(test-${suite} PRIVATE sunlens-domain Qt6::Test)
        add_test(NAME ${suite} COMMAND test-${suite})
        set_tests_properties(${suite} PROPERTIES TIMEOUT 45)
    endforeach()
    set(SUNLENS_SING_BOX_EXECUTABLE "" CACHE FILEPATH "Путь к закреплённому sing-box для проверки")
    set(SUNLENS_XRAY_EXECUTABLE "" CACHE FILEPATH "Путь к закреплённому Xray для проверки")
    if(SUNLENS_SING_BOX_EXECUTABLE)
        qt_add_executable(test-sing-box tests/integration/test-sing-box.cpp)
        target_link_libraries(test-sing-box PRIVATE sunlens-domain Qt6::Test)
        add_test(NAME sing-box-validation COMMAND test-sing-box)
        set_tests_properties(sing-box-validation PROPERTIES TIMEOUT 120
            ENVIRONMENT "SUNLENS_SING_BOX_EXECUTABLE=${SUNLENS_SING_BOX_EXECUTABLE}")
    endif()
    if(SUNLENS_XRAY_EXECUTABLE)
        qt_add_executable(test-xray tests/integration/test-xray.cpp)
        target_link_libraries(test-xray PRIVATE sunlens-domain Qt6::Test)
        add_test(NAME xray-validation COMMAND test-xray)
        set_tests_properties(xray-validation PROPERTIES TIMEOUT 120
            ENVIRONMENT "SUNLENS_XRAY_EXECUTABLE=${SUNLENS_XRAY_EXECUTABLE}")
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if(SUNLENS_SING_BOX_EXECUTABLE OR SUNLENS_XRAY_EXECUTABLE)
            find_program(SUNLENS_OPENSSL_CLI openssl REQUIRED)
            qt_add_executable(profile-check-driver tests/integration/profile-check-driver.cpp)
            target_link_libraries(profile-check-driver PRIVATE sunlens-domain)
            foreach(core IN ITEMS sing-box xray)
                string(TOUPPER "${core}" core_variable)
                string(REPLACE "-" "_" core_variable "${core_variable}")
                if(SUNLENS_${core_variable}_EXECUTABLE)
                    add_test(NAME "${core}-profile-check"
                        COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/profile-check.py"
                        --name "${core}" --binary "${SUNLENS_${core_variable}_EXECUTABLE}"
                        --driver "$<TARGET_FILE:profile-check-driver>"
                        --openssl "${SUNLENS_OPENSSL_CLI}")
                    set_tests_properties("${core}-profile-check" PROPERTIES TIMEOUT 90)
                endif()
            endforeach()
        endif()
        qt_add_executable(tunnel-test-driver tests/integration/tunnel-driver.cpp)
        target_link_libraries(tunnel-test-driver PRIVATE sunlens-domain)
        option(SUNLENS_ENABLE_NAMESPACE_TESTS "Живые тесты в отдельном user/network namespace" OFF)
        if(SUNLENS_ENABLE_NAMESPACE_TESTS)
            if(NOT SUNLENS_SING_BOX_EXECUTABLE)
                message(FATAL_ERROR "Для живого теста требуется закреплённый sing-box")
            endif()
            add_test(NAME linux-live-tun COMMAND unshare --user --map-root-user --mount --net
                "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linux-tun.py"
                --core "${SUNLENS_SING_BOX_EXECUTABLE}" --driver "$<TARGET_FILE:tunnel-test-driver>"
                --report "${CMAKE_CURRENT_BINARY_DIR}/evidence/linux-tun")
            set_tests_properties(linux-live-tun PROPERTIES TIMEOUT 180 LABELS "live-tun;isolated")
            if(SUNLENS_XRAY_EXECUTABLE)
                add_test(NAME linux-live-xray COMMAND unshare --user --map-root-user --mount --net
                    "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linux-tun.py"
                    --core "${SUNLENS_SING_BOX_EXECUTABLE}" --xray "${SUNLENS_XRAY_EXECUTABLE}"
                    --driver "$<TARGET_FILE:tunnel-test-driver>"
                    --report "${CMAKE_CURRENT_BINARY_DIR}/evidence/linux-xray")
                set_tests_properties(linux-live-xray PROPERTIES TIMEOUT 180 LABELS "live-tun;isolated")
            endif()
        endif()
    endif()
    add_test(NAME app-version COMMAND sunlens --version)
    set_tests_properties(app-version PROPERTIES PASS_REGULAR_EXPRESSION "SUNLENS ${PROJECT_VERSION}"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_test(NAME gui-smoke COMMAND sunlens --smoke-test)
    set_tests_properties(gui-smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
        TIMEOUT 20)
endif()
