cmake_minimum_required(VERSION 3.21)
project(CirMakerQt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Svg PrintSupport Concurrent LinguistTools Test)
qt_standard_project_setup(I18N_SOURCE_LANGUAGE en I18N_TRANSLATED_LANGUAGES pt_BR)

# --- core: data model, connectivity, file formats, symbol library (no widgets)
add_library(cmcore STATIC
    src/core/Circuit.h src/core/Circuit.cpp
    src/core/NetlistBuilder.h src/core/NetlistBuilder.cpp
    src/core/WireGeometry.h src/core/WireGeometry.cpp
    src/core/Bom.h src/core/Bom.cpp
    src/core/PcbNetlist.h src/core/PcbNetlist.cpp
    src/core/UndoCommands.h src/core/UndoCommands.cpp
    src/core/io/NativeFormat.h src/core/io/NativeFormat.cpp
    src/core/io/CktImporter.h src/core/io/CktImporter.cpp
    src/library/SymbolDef.h
    src/library/LibraryRegistry.h src/library/LibraryRegistry.cpp
    src/library/Macros.h src/library/Macros.cpp
)
target_include_directories(cmcore PUBLIC src)
target_link_libraries(cmcore PUBLIC Qt6::Core Qt6::Gui)

# --- digital simulator
add_library(cmsim STATIC
    src/sim/digital/Logic.h
    src/sim/digital/Models.h src/sim/digital/Models.cpp
    src/sim/digital/ChipModels.cpp
    src/sim/analog/SpiceNetlister.h src/sim/analog/SpiceNetlister.cpp
    src/sim/analog/NgspiceEngine.h src/sim/analog/NgspiceEngine.cpp
    src/sim/digital/EventSimulator.h src/sim/digital/EventSimulator.cpp
)
target_link_libraries(cmsim PUBLIC cmcore)

# --- symbol library data, embedded as resources
file(GLOB SYMBOL_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/library/symbols/*.json)
add_library(cmlibdata STATIC)
qt_add_resources(cmlibdata symbols PREFIX /library BASE library FILES ${SYMBOL_FILES})
target_link_libraries(cmlibdata PUBLIC Qt6::Core)

option(CM_BUILD_APP "Build the GUI application" ON)

# --- application
if(CM_BUILD_APP)
add_library(cmui STATIC
    src/ui/MainWindow.h src/ui/MainWindow.cpp
    src/ui/SchematicScene.h src/ui/SchematicScene.cpp
    src/ui/SchematicView.h src/ui/SchematicView.cpp
    src/ui/ComponentItem.h src/ui/ComponentItem.cpp
    src/ui/WireItem.h src/ui/WireItem.cpp
    src/ui/TextNoteItem.h src/ui/TextNoteItem.cpp
    src/ui/TitleBlockItem.h src/ui/TitleBlockItem.cpp
    src/ui/SymbolPainter.h src/ui/SymbolPainter.cpp
    src/ui/LibraryBrowser.h src/ui/LibraryBrowser.cpp
    src/ui/PropertiesDialog.h src/ui/PropertiesDialog.cpp
    src/ui/DataSequencerDialog.h src/ui/DataSequencerDialog.cpp
    src/ui/PreferencesDialog.h src/ui/PreferencesDialog.cpp
    src/ui/WaveformView.h src/ui/WaveformView.cpp
    src/ui/AnalysisDialog.h src/ui/AnalysisDialog.cpp
    src/ui/Hotkeys.h src/ui/Hotkeys.cpp
)
target_link_libraries(cmui PUBLIC cmcore cmsim Qt6::Widgets Qt6::Svg Qt6::PrintSupport Qt6::Concurrent)
target_compile_definitions(cmui PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
set_target_properties(cmui PROPERTIES AUTOMOC ON)

qt_add_executable(cirmaker src/main.cpp)
target_link_libraries(cirmaker PRIVATE cmui cmlibdata)
set_target_properties(cirmaker PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
target_compile_definitions(cirmaker PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")

qt_add_translations(cirmaker TS_FILES i18n/cirmaker_pt_BR.ts SOURCE_TARGETS cirmaker cmui)
endif()

# --- tests
enable_testing()
function(cm_add_test name)
    qt_add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE cmcore cmsim cmlibdata Qt6::Test)
    target_compile_definitions(${name} PRIVATE CM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
endfunction()
cm_add_test(tst_logic)
cm_add_test(tst_netlist)
cm_add_test(tst_simulator)
cm_add_test(tst_ckt_import)
cm_add_test(tst_chips)
cm_add_test(tst_document)
cm_add_test(tst_analog)
cm_add_test(tst_macros)
if(CM_BUILD_APP)
    qt_add_executable(tst_gui tests/tst_gui.cpp)
    target_link_libraries(tst_gui PRIVATE cmui cmlibdata Qt6::Test)
    target_include_directories(tst_gui PRIVATE tests)
    add_test(NAME tst_gui COMMAND tst_gui -platform offscreen)
endif()
