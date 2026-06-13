# Seeds a dev-build phasma_settings.json pointing at a default project, but only when the
# file does not already exist — so a user's chosen project pin is never clobbered by a rebuild.
# Invoked as a build-time custom command (PHASMA_CONFIG_OUTPUT_DIR uses $<CONFIG>, which only
# resolves at build time). Args: -DSETTINGS_FILE=<path> -DPROJECT_PATH=<abs project root>
if(NOT EXISTS "${SETTINGS_FILE}")
    file(WRITE "${SETTINGS_FILE}" "{\n    \"project_path\": \"${PROJECT_PATH}\"\n}\n")
    message(STATUS "[Phasma] Seeded dev project pin: ${SETTINGS_FILE} -> ${PROJECT_PATH}")
endif()
