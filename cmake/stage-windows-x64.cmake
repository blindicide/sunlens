cmake_minimum_required(VERSION 3.24)

# For a native Windows x64 Release build. Does not publish or start a tunnel.
foreach(input APP WINDEPLOYQT STAGE)
    if(NOT DEFINED ${input} OR "${${input}}" STREQUAL "")
        message(FATAL_ERROR "Specify -D${input}=...")
    endif()
endforeach()
if(NOT EXISTS "${APP}" OR NOT EXISTS "${WINDEPLOYQT}")
    message(FATAL_ERROR "APP or WINDEPLOYQT not found")
endif()
if(EXISTS "${STAGE}")
    message(FATAL_ERROR "Use a new staging directory: ${STAGE}")
endif()

set(source "${CMAKE_CURRENT_LIST_DIR}/..")
get_filename_component(stage "${STAGE}" ABSOLUTE)
file(MAKE_DIRECTORY "${stage}")
if(NOT QT_ONLY)
set(downloads "${stage}/.downloads")
set(core "${stage}/cores/sing-box")
file(MAKE_DIRECTORY "${downloads}" "${core}")

function(fetch_zip url sha256 archive)
    file(DOWNLOAD "${url}" "${downloads}/${archive}.zip"
        EXPECTED_HASH "SHA256=${sha256}" TLS_VERIFY ON TIMEOUT 180
        INACTIVITY_TIMEOUT 30)
    file(ARCHIVE_EXTRACT INPUT "${downloads}/${archive}.zip"
        DESTINATION "${downloads}/${archive}")
endfunction()

set(sing_version "1.14.5")
set(sing_name "sing-box-${sing_version}-windows-amd64")
fetch_zip("https://github.com/SagerNet/sing-box/releases/download/v${sing_version}/${sing_name}.zip"
    "b935f7b38bffcc1f600165e79e5d0c2d58b547942ac1b6cf61a27b5447c089e7" "sing-box")
fetch_zip("https://www.wintun.net/builds/wintun-0.14.1.zip"
    "07c256185d6ee3652e09fa55c0b673e2624b565e02c4b9091c79ca7d2f24ef51" "wintun")

file(COPY "${downloads}/sing-box/${sing_name}/sing-box.exe"
    "${downloads}/sing-box/${sing_name}/libcronet.dll"
    "${downloads}/sing-box/${sing_name}/LICENSE" DESTINATION "${core}")
file(COPY "${downloads}/wintun/wintun/bin/amd64/wintun.dll" DESTINATION "${core}")
file(COPY_FILE "${downloads}/wintun/wintun/LICENSE.txt" "${core}/Wintun-LICENSE.txt")
file(REMOVE_RECURSE "${downloads}")
endif()
file(COPY_FILE "${APP}" "${stage}/sunlens.exe")
file(COPY "${source}/VERSION" "${source}/LICENSE" "${source}/third_party/NOTICE.md"
    DESTINATION "${stage}")

# Use the separately installed official x64 VC++ Redistributable.
execute_process(COMMAND "${WINDEPLOYQT}" --release --no-compiler-runtime
    --qmldir "${source}/qml" "${stage}/sunlens.exe" COMMAND_ERROR_IS_FATAL ANY)

message(STATUS "Staged Windows x64 preview: ${stage}")
