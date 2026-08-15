add_library(af_ai OBJECT
    ai_service.cpp
    ai_settings.cpp
    ai_task_runner.cpp
    ai_types.cpp
    http_client.cpp
    libcurl_http_client.cpp
    openai_provider.cpp
    secret_store.cpp
)
# libcurl is reached through one file, libcurl_http_client.cpp. Keeping it
# private is what stops a second HTTP path appearing elsewhere by accident.
target_link_libraries(af_ai PUBLIC af_common)
target_link_libraries(af_ai PRIVATE CURL::libcurl)

# Platform secret storage (issue #53). Windows uses the Credential Manager
# through wincred.h, which needs no CMake wiring; the other two do.
if(APPLE)
    # Keychain Services. Both frameworks ship with every macOS SDK, so this is
    # not optional and a missing one is a broken toolchain rather than a
    # configuration to tolerate.
    find_library(AF_SECURITY_FRAMEWORK Security REQUIRED)
    find_library(AF_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(af_ai PRIVATE ${AF_SECURITY_FRAMEWORK} ${AF_COREFOUNDATION_FRAMEWORK})
elseif(UNIX)
    # libsecret (Freedesktop Secret Service: GNOME Keyring, KWallet, ...).
    #
    # OPTIONAL, unlike the Apple frameworks: there is no keyring API every Linux
    # has, and making libsecret a hard build dependency would stop the
    # application building on a machine that has no desktop keyring at all.
    # Without it the secret store compiles to the unsupported implementation,
    # which refuses and names the package to install.
    #
    # It is deliberately NOT replaced by a file-based fallback. A key on disk
    # that the user believes is in a keyring is worse than a refusal.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(AF_LIBSECRET QUIET libsecret-1)
    endif()
    if(AF_LIBSECRET_FOUND)
        target_compile_definitions(af_ai PRIVATE AF_HAVE_LIBSECRET)
        target_include_directories(af_ai PRIVATE ${AF_LIBSECRET_INCLUDE_DIRS})
        target_link_libraries(af_ai PRIVATE ${AF_LIBSECRET_LIBRARIES})
        target_link_directories(af_ai PRIVATE ${AF_LIBSECRET_LIBRARY_DIRS})
        message(STATUS "Secret storage: libsecret ${AF_LIBSECRET_VERSION}")
    else()
        message(STATUS "Secret storage: libsecret not found - AI API keys cannot be stored on this build "
                       "(install libsecret-1-dev / libsecret-devel and re-configure)")
    endif()
endif()
