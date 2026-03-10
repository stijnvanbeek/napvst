cmake_minimum_required(VERSION 3.18.4)

# Get the plugin name from the directory name
get_filename_component(plugin_name ${CMAKE_CURRENT_SOURCE_DIR} NAME)

project(${plugin_name} VERSION 1.0.0  DESCRIPTION "Test Effect")
enable_language(OBJCXX)

# Set global directories and paths
set(VST3SDK_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../VST_SDK/vst3sdk)
set(NAP_ROOT ${CMAKE_CURRENT_LIST_DIR}/../nap)
set(BIN_DIR ${CMAKE_BINARY_DIR}/bin)
set(APP_INSTALL_NAME "${PROJECT_NAME}.vst3")

# Include NAP, VST3 SDK and NAP cmake utilites
add_subdirectory(${NAP_ROOT} ${PROJECT_BINARY_DIR}/nap)
add_subdirectory(${VST3SDK_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
include(${NAP_ROOT}/cmake/nap_utilities.cmake)

# Scan for an app module
try_add_module_from_dir(${CMAKE_CURRENT_SOURCE_DIR}/module)

# Initialize VST3 SDK
set(SMTG_RUN_VST_VALIDATOR OFF)
smtg_enable_vst3_sdk()

# Add all cpp files to SOURCES
file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS src/*.h src/*.hpp)

smtg_add_vst3plugin(${PROJECT_NAME}
        ${VST3SDK_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
        ${VST3SDK_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
        ${SOURCES}
        ${HEADERS}
)

# Pull in the project module if it exists
if (TARGET nap${PROJECT_NAME})
    target_link_libraries(${PROJECT_NAME} nap${PROJECT_NAME})
endif()

if (BUILD_STATIC)
    target_link_nap_static(${PROJECT_NAME})
    target_link_libraries(${PROJECT_NAME} PRIVATE sdk vstgui_support)
else ()
    target_link_libraries(${PROJECT_NAME} PRIVATE sdk vstgui_support napimgui napparametergui napfmsynth napcontrol)
endif ()

smtg_target_configure_version_file(${PROJECT_NAME})

set(app_install_data_dir ${BIN_DIR}/app_install_data/${PROJECT_NAME})

set(source_data_dir ${CMAKE_CURRENT_SOURCE_DIR}/data)

# Copy data directory to app specific bin
set(bin_data_dir ${app_install_data_dir}/data)
add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${source_data_dir} ${bin_data_dir})

if (WIN32)
    # Install all artifacts in app root on windows
    set(CMAKE_INSTALL_BINDIR ${APP_INSTALL_NAME})
    set(CMAKE_INSTALL_LIBDIR ${APP_INSTALL_NAME})
    set(CMAKE_INSTALL_DATADIR ${APP_INSTALL_NAME})
    set(CMAKE_INSTALL_DOCDIR ${APP_INSTALL_NAME}/doc)
    set(CMAKE_INSTALL_MODULEINFODIR ${APP_INSTALL_NAME}/lib)
else()
    if (APPLE)
        # Install in app bundle structure on MacOS
        set(CMAKE_INSTALL_BINDIR ${APP_INSTALL_NAME}/Contents/MacOS)
        set(CMAKE_INSTALL_LIBDIR ${APP_INSTALL_NAME}/Contents/MacOS/lib)
        set(CMAKE_INSTALL_DATADIR ${APP_INSTALL_NAME}/Contents/Resources)
        set(CMAKE_INSTALL_DOCDIR ${APP_INSTALL_NAME}/Contents/Resources/doc)
        set(CMAKE_INSTALL_INFODIR ${APP_INSTALL_NAME}/Contents) # Used for Info.plist file
        set(CMAKE_INSTALL_MODULEINFODIR ${APP_INSTALL_NAME}/Contents/Resources/lib)
    else ()
        # Install libraries in lib, executable in app root on Linux
        set(CMAKE_INSTALL_BINDIR ${APP_INSTALL_NAME})
        set(CMAKE_INSTALL_LIBDIR ${APP_INSTALL_NAME}/lib)
        set(CMAKE_INSTALL_DATADIR ${APP_INSTALL_NAME})
        set(CMAKE_INSTALL_DOCDIR ${APP_INSTALL_NAME}/doc)
        set(CMAKE_INSTALL_MODULEINFODIR ${APP_INSTALL_NAME}/lib)
    endif()
endif()

# Install to packaged app
install(DIRECTORY ${bin_data_dir} TYPE DATA OPTIONAL)
install(DIRECTORY ${bin_license_dir} TYPE DOC OPTIONAL)

if(SMTG_MAC)
    set_source_files_properties(src/nappluginview.cpp PROPERTIES LANGUAGE OBJCXX)
    smtg_target_set_bundle(${PROJECT_NAME}
            BUNDLE_IDENTIFIER org.stijnvanbeek.VstTest
            COMPANY_NAME StijnVanBeek
    )
    smtg_target_set_debug_executable(${PROJECT_NAME}
            "/Applications/VST3PluginTestHost.app"
            "--pluginfolder;$(BUILT_PRODUCTS_DIR)"
    )
    get_target_property(bundle_path ${PROJECT_NAME} SMTG_PLUGIN_PACKAGE_PATH)
    message("vst path ${bundle_path}")

    # Update executable rpath if it hasn't been set already
    # Using a bash script, it checks if the '@executablepath/%{LIB_RPATH}/' already exists in the list displayed by 'otool -l'. If not, it calls the CMAKE_INSTALL_NAME_TOOL to install the name.
    add_custom_command(TARGET ${PROJECT_NAME}
            POST_BUILD COMMAND
            if ! otool -l $<TARGET_FILE:${PROJECT_NAME}> | grep -q @loader_path/lib\; then ${CMAKE_INSTALL_NAME_TOOL} -add_rpath "@loader_path/lib" $<TARGET_FILE:${PROJECT_NAME}>\; fi)
    add_custom_command(TARGET ${PROJECT_NAME}
            POST_BUILD COMMAND
            if ! otool -l $<TARGET_FILE:${PROJECT_NAME}> | grep -q @loader_path\; then ${CMAKE_INSTALL_NAME_TOOL} -add_rpath "@loader_path" $<TARGET_FILE:${PROJECT_NAME}>\; fi)

    set(output_path "${CMAKE_BINARY_DIR}/VST3/${CMAKE_BUILD_TYPE}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${output_path})

elseif(SMTG_WIN)
#    target_sources(MyPlugin
#            PRIVATE
#            resource/win32resource.rc
#    )
#    if(MSVC)
#        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT MyPlugin)
#
#        smtg_target_set_debug_executable(MyPlugin
#                "$(ProgramW6432)/Steinberg/VST3PluginTestHost/VST3PluginTestHost.exe"
#                "--pluginfolder \"$(OutDir)/\""
#        )
#    endif()
endif()

codesign_target(${PROJECT_NAME})
