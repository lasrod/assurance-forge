# --------------------------------------------------------------------
# packaging.cmake -- what a Windows release contains, and the packages built
# from it.
#
# The install() rules below are the single description of the shipped layout.
# CPack turns them into two packages from one staging:
#
#   ZIP         the portable archive, unzipped and run in place
#   INNOSETUP   a per-user installer (no admin rights), for alpha/beta testers
#
# Build both after a Release build with:
#
#   cpack --config build/CPackConfig.cmake -C Release -B build/package
#
# Everything here is Windows-only. Linux and macOS releases are still staged by
# hand in .github/workflows/release.yml; the macOS app is a bundle whose layout
# these rules do not describe.
# --------------------------------------------------------------------

if(NOT WIN32)
    return()
endif()

# The version the packages carry. The release workflow passes the git tag
# (e.g. 0.2.0-alpha.3), or dev-<sha> for a workflow_dispatch build.
set(AF_PACKAGE_VERSION "0.0.0-dev" CACHE STRING "Version written into release packages")
# Windows file-version resources take numbers only; the prerelease label lives
# in the display version, and a dev build has no number at all.
if(AF_PACKAGE_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(AF_PACKAGE_NUMERIC_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
else()
    set(AF_PACKAGE_NUMERIC_VERSION "0.0.0")
endif()

# One component holds everything a user receives. The fetched dependencies
# (curl, glfw, googletest, nlohmann_json, ...) add install rules of their own
# for headers and static libraries; packaging only this component keeps those
# out of the release.
set(AF_RUNTIME_COMPONENT af_runtime)

install(TARGETS assurance-forge assurance-forge-mcp
    RUNTIME DESTINATION .
    COMPONENT ${AF_RUNTIME_COMPONENT}
)

# Fonts, locale catalogues and the app icon. HelloImGui looks for them in an
# `assets` folder beside the exe.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/"
    DESTINATION assets
    COMPONENT ${AF_RUNTIME_COMPONENT}
)

# The SCCG catalogue. Both executables look for it in data/sccg/dist beside
# themselves, the same place af_copy_sccg_data() puts it in the build tree.
install(FILES "${AF_SCCG_DIST_DIR}/${AF_SCCG_CATALOG_FILE}"
    DESTINATION data/sccg/dist
    COMPONENT ${AF_RUNTIME_COMPONENT}
)

# Sample assurance cases to open on first run.
file(GLOB AF_SAMPLE_CASES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/data/*.xml")
install(FILES ${AF_SAMPLE_CASES}
    DESTINATION data
    COMPONENT ${AF_RUNTIME_COMPONENT}
)

install(FILES "${CMAKE_SOURCE_DIR}/README.md" "${CMAKE_SOURCE_DIR}/LICENSE.md"
    DESTINATION .
    COMPONENT ${AF_RUNTIME_COMPONENT}
)

# The Visual C++ runtime (msvcp140.dll, vcruntime140.dll, ...). Both exes link
# it dynamically, and a machine without the VC++ Redistributable refuses to
# start them. Deployed app-locally, which Microsoft supports for these DLLs.
# The Universal CRT is part of Windows 10 and later, so it is not shipped.
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT ${AF_RUNTIME_COMPONENT})
set(CMAKE_INSTALL_UCRT_LIBRARIES OFF)
include(InstallRequiredSystemLibraries)

# --- CPack ---
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_GENERATOR ZIP INNOSETUP)
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};${AF_RUNTIME_COMPONENT};/")

set(CPACK_PACKAGE_NAME "Assurance Forge")
set(CPACK_PACKAGE_VENDOR "Assurance Forge")
set(CPACK_PACKAGE_VERSION "${AF_PACKAGE_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Safety case engineering with SACM 2.3 and GSN")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/lasrod/assurance-forge")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Assurance Forge")
# The installer's name is set per generator in packaging_project_config.cmake.
set(CPACK_PACKAGE_FILE_NAME "assurance-forge.${AF_PACKAGE_VERSION}-windows-x64")
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/cmake/packaging_project_config.cmake")

# Start-menu shortcut, and the "Launch Assurance Forge" box on the last page.
set(CPACK_PACKAGE_EXECUTABLES assurance-forge "Assurance Forge")
set(CPACK_INNOSETUP_RUN_EXECUTABLES assurance-forge)
# One shortcut does not need a folder of its own.
set(CPACK_INNOSETUP_PROGRAM_MENU_FOLDER ".")

# Inno Setup does not put ISCC on PATH; look where its installer puts it.
set(_af_program_files_x86 "ProgramFiles(x86)")
find_program(AF_ISCC_EXECUTABLE ISCC
    PATHS
        "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6"
        "$ENV{${_af_program_files_x86}}/Inno Setup 6"
        "$ENV{ProgramFiles}/Inno Setup 6"
)
if(AF_ISCC_EXECUTABLE)
    set(CPACK_INNOSETUP_EXECUTABLE "${AF_ISCC_EXECUTABLE}")
endif()

set(CPACK_INNOSETUP_ARCHITECTURE x64)
set(CPACK_INNOSETUP_USE_MODERN_WIZARD ON)
set(CPACK_INNOSETUP_ICON_FILE "${CMAKE_SOURCE_DIR}/assets/app_settings/icon.ico")
set(CPACK_INNOSETUP_LANGUAGES english japanese)
# MIT asks for no acceptance; a license page would only be one more click.
set(CPACK_INNOSETUP_IGNORE_LICENSE_PAGE ON)

# Never change the AppId. It is how a newer installer finds the installed copy
# and upgrades it in place rather than installing a second one beside it.
set(CPACK_INNOSETUP_SETUP_AppId "{{7CF6066C-F278-421C-8938-4C75497F3BD8}")
set(CPACK_INNOSETUP_SETUP_AppVersion "${AF_PACKAGE_VERSION}")
set(CPACK_INNOSETUP_SETUP_VersionInfoVersion "${AF_PACKAGE_NUMERIC_VERSION}")
set(CPACK_INNOSETUP_SETUP_UninstallDisplayIcon "{app}\\assurance-forge.exe")
# Per-user by default: installs into %LOCALAPPDATA%\Programs without admin
# rights, which is what a tester on a managed laptop has. The dialog still
# offers an all-users install to someone who can elevate.
set(CPACK_INNOSETUP_SETUP_PrivilegesRequired lowest)
set(CPACK_INNOSETUP_SETUP_PrivilegesRequiredOverridesAllowed "commandline dialog")
# Settings in %APPDATA%\AssuranceForge are not the installer's files, so an
# uninstall leaves them; a tester moving between builds keeps their setup.

include(CPack)
