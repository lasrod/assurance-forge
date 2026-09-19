# Read by CPack once per generator (CPACK_PROJECT_CONFIG_FILE), so settings can
# differ between the portable zip and the installer.

if(CPACK_GENERATOR STREQUAL "INNOSETUP")
    # A "-setup" suffix tells the installer apart from the portable zip on the
    # Releases page, where both otherwise carry the same name.
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-setup")
endif()
