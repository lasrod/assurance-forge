# Read by CPack once per generator (CPACK_PROJECT_CONFIG_FILE), so settings can
# differ between the portable zip and the installer.

if(CPACK_GENERATOR STREQUAL "INNOSETUP")
    # A "-setup" suffix tells the installer apart from the portable zip on the
    # Releases page, where both otherwise carry the same name.
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-setup")

    # Tie the optional files to the choices in the installer: the MCP server to
    # the AI page's checkbox (installer_code.pas), the samples to their
    # component (installer.iss), so unticking either leaves its files out. Set
    # here rather than in packaging.cmake because CPack reads this file as-is:
    # the instructions' quotes and escaped semicolons survive unchanged.
    set(_af_instructions
        "assurance-forge-mcp.exe"
        "Source: \"assurance-forge-mcp.exe\"\; DestDir: \"{app}\"\; Flags: ignoreversion\; Check: ShouldInstallMcp")
    foreach(_af_sample IN LISTS CPACK_AF_SAMPLE_NAMES)
        list(APPEND _af_instructions
            "data/${_af_sample}"
            "Source: \"data\\${_af_sample}\"\; DestDir: \"{app}\\data\"\; Flags: ignoreversion\; Components: samples")
    endforeach()
    set(CPACK_INNOSETUP_CUSTOM_INSTALL_INSTRUCTIONS "${_af_instructions}")
endif()
