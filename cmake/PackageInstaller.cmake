if(NOT DEFINED MELOBOX_INSTALLER_SCRIPT)
    message(FATAL_ERROR "MELOBOX_INSTALLER_SCRIPT is required.")
endif()

if(NOT DEFINED MELOBOX_INNO_SETUP_COMPILER OR NOT EXISTS "${MELOBOX_INNO_SETUP_COMPILER}")
    message(FATAL_ERROR "ISCC.exe was not found. Set MELOBOX_INNO_SETUP_COMPILER to the Inno Setup compiler path.")
endif()

execute_process(
    COMMAND "${MELOBOX_INNO_SETUP_COMPILER}" "${MELOBOX_INSTALLER_SCRIPT}"
    RESULT_VARIABLE meloBoxInstallerResult
)

if(NOT meloBoxInstallerResult EQUAL 0)
    message(FATAL_ERROR "Installer compilation failed.")
endif()
