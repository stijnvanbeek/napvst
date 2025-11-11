cmake_minimum_required(VERSION 3.18.4)

project(VstTest VERSION 1.0.0  DESCRIPTION "Test Effect")
enable_language(OBJCXX)

set(vst3sdk_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../VST_SDK/vst3sdk)
set(NAP_DIR ${CMAKE_CURRENT_LIST_DIR}/../nap)

add_subdirectory(${NAP_DIR} ${PROJECT_BINARY_DIR}/nap)
add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
include(${NAP_DIR}/cmake/nap_utilities.cmake)

set(SMTG_RUN_VST_VALIDATOR OFF)

smtg_enable_vst3_sdk()

smtg_add_vst3plugin(VstTest
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
        src/version.h
        src/napplugin.h
        src/napplugin.cpp
        src/sdlpoller.h
        src/sdlpoller.cpp
        src/nappluginview.h
        src/nappluginview.cpp
)

target_link_libraries(VstTest PRIVATE sdk naprtti napaudioadvanced napscene napmidi napcontrol napparameter napfmsynth naprender napparametergui)
codesign_target(VstTest)

smtg_target_configure_version_file(VstTest)

if(SMTG_MAC)
    smtg_target_set_bundle(VstTest
            BUNDLE_IDENTIFIER org.stijnvanbeek.VstTest
            COMPANY_NAME StijnVanBeek
    )
    smtg_target_set_debug_executable(VstTest
            "/Applications/VST3PluginTestHost.app"
            "--pluginfolder;$(BUILT_PRODUCTS_DIR)"
    )
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