if(WIN32)
    # FIXME: Add support for LibLine on Windows
    lagom_utility(js SOURCES js.cpp LIBS LibCrypto LibJS LibUnicode LibMain LibTextCodec LibGC Threads::Threads)
    lagom_utility(wasm SOURCES wasm.cpp LIBS LibFileSystem LibWasm LibMain LibJS LibCrypto LibGC)
else()
    lagom_utility(js SOURCES js.cpp LIBS LibCrypto LibJS LibLine LibUnicode LibMain LibTextCodec LibGC Threads::Threads)
    lagom_utility(wasm SOURCES wasm.cpp LIBS LibFileSystem LibWasm LibLine LibMain LibJS LibCrypto LibGC)
endif()

lagom_utility(xml SOURCES xml.cpp LIBS LibFileSystem LibMain LibXML LibURL)
lagom_utility(dns SOURCES dns.cpp LIBS LibDNS LibMain LibTLS LibCrypto)

if (ENABLE_GUI_TARGETS)
    lagom_utility(image SOURCES image.cpp LIBS LibGfx LibMain)
endif()

# FIXME: Increase support for building targets on Windows
if (WIN32 AND ENABLE_WINDOWS_CI)
    return()
endif()


lagom_utility(test262-runner SOURCES test262-runner.cpp LIBS LibJS LibFileSystem LibGC)

# ============================================================================
# Itbaa (اطبع) - HTML to PDF Library and CLI
# Copyright (c) 2025, sudorw <ahmedrowaihi@sudorw.com>
# ============================================================================
include(skia)

# Itbaa Core Library (static)
add_library(itbaa STATIC
    Itbaa/lib/Itbaa.cpp
    Itbaa/lib/Renderer.cpp
    Itbaa/lib/PDFWriter.cpp
    Itbaa/lib/DisplayListPlayerPDF.cpp
)

target_include_directories(itbaa PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/Itbaa/lib
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/Lagom/Libraries
)

target_link_libraries(itbaa PUBLIC
    LibWeb 
    LibGfx 
    LibCore 
    LibJS
    LibGC
    LibIPC
    LibURL
    LibWebView
    LibRequests
    skia
)

set_target_properties(itbaa PROPERTIES
    OUTPUT_NAME "itbaa"
    VERSION "1.0.0"
    SOVERSION "1"
)

# Itbaa CLI Tool
lagom_utility(itbaa-cli
    SOURCES 
        Itbaa/cli/main.cpp
    LIBS 
        itbaa
        LibMain
)

target_include_directories(itbaa-cli PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/Itbaa/lib
)

set_target_properties(itbaa-cli PROPERTIES
    OUTPUT_NAME "itbaa"
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckCSourceCompiles)
    # Check for musl's declaration of __assert_fail
    check_c_source_compiles("
    #include <assert.h>
    __attribute__((__noreturn__)) void __assert_fail(char const* assertion, char const* file, int line, char const* function) {}
    int main() {}
    "
    ASSERT_FAIL_HAS_INT
    )
endif()

if (ASSERT_FAIL_HAS_INT)
    target_compile_definitions(test262-runner PRIVATE ASSERT_FAIL_HAS_INT)
endif()

# Determine the Git commit hash (fallback to 'unknown' if not a Git repo)
execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if (NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "unknown")
endif()

# Write it to the build directory
set(GIT_COMMIT_FILE "${CMAKE_BINARY_DIR}/COMMIT")
file(WRITE "${GIT_COMMIT_FILE}" "${GIT_COMMIT_HASH}\n")

if (NOT CMAKE_SKIP_INSTALL_RULES)
    install(TARGETS js COMPONENT js)
    install(FILES "${GIT_COMMIT_FILE}" COMPONENT js DESTINATION .)

    install(TARGETS wasm COMPONENT wasm)
    install(FILES "${GIT_COMMIT_FILE}" COMPONENT wasm DESTINATION .)

    # Install Itbaa library and CLI
    install(TARGETS itbaa-cli COMPONENT itbaa)
    install(TARGETS itbaa COMPONENT itbaa)
    install(FILES Itbaa/lib/Itbaa.h COMPONENT itbaa DESTINATION include)
    install(FILES "${GIT_COMMIT_FILE}" COMPONENT itbaa DESTINATION .)

    set(CPACK_GENERATOR "TGZ")
    set(CPACK_STRIP_FILES TRUE)
    set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
    set(CPACK_COMPONENTS_ALL js wasm itbaa)
    set(CPACK_COMPONENTS_GROUPING ONE_PER_GROUP)
    if (APPLE)
        if("arm64" IN_LIST CMAKE_OSX_ARCHITECTURES AND "x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES)
            set(CPACK_SYSTEM_NAME "macOS-universal2")
        else()
            set(CPACK_SYSTEM_NAME "macOS-${CMAKE_SYSTEM_PROCESSOR}")
        endif()
    else()
        set(CPACK_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    set(CPACK_ARCHIVE_JS_FILE_NAME "ladybird-js-${CPACK_SYSTEM_NAME}")
    set(CPACK_ARCHIVE_WASM_FILE_NAME "ladybird-wasm-${CPACK_SYSTEM_NAME}")
    set(CPACK_ARCHIVE_ITBAA_FILE_NAME "itbaa-${CPACK_SYSTEM_NAME}")

    include(CPack)
endif()
