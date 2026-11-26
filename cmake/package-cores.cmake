set(SUNLENS_PACKAGE_SING_BOX "" CACHE FILEPATH "Закреплённый sing-box для пакета")
set(SUNLENS_PACKAGE_XRAY "" CACHE FILEPATH "Закреплённый Xray для пакета")

function(sunlens_package_core path filename checksum)
    if(NOT IS_ABSOLUTE "${path}" OR NOT EXISTS "${path}" OR IS_DIRECTORY "${path}" OR IS_SYMLINK "${path}")
        message(FATAL_ERROR "Требуется абсолютный путь к обычному ${filename}")
    endif()
    file(SHA256 "${path}" actual)
    if(NOT actual STREQUAL checksum)
        message(FATAL_ERROR "Неверный SHA-256 ядра ${filename}")
    endif()
    install(PROGRAMS "${path}" DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/sunlens/cores"
        RENAME "${filename}")
endfunction()

sunlens_package_core("${SUNLENS_PACKAGE_SING_BOX}" sing-box "${SING_BOX_LINUX_HASH}")
sunlens_package_core("${SUNLENS_PACKAGE_XRAY}" xray "${XRAY_LINUX_HASH}")
