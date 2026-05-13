find_gtest()

add_executable(${PROJECT_NAME}_scene_graph_models_unit scene_graph_standard_item_unit.cpp)
target_link_libraries(
  ${PROJECT_NAME}_scene_graph_models_unit
  PRIVATE GTest::GTest
          GTest::Main
          ${PROJECT_NAME}_scene_graph_models
          tesseract::tesseract_scene_graph
          Qt5::Core
          Qt5::Gui
          Qt5::Widgets)
target_compile_options(${PROJECT_NAME}_scene_graph_models_unit PRIVATE ${TESSERACT_COMPILE_OPTIONS_PRIVATE}
                                                                       ${TESSERACT_COMPILE_OPTIONS_PUBLIC})
target_compile_definitions(${PROJECT_NAME}_scene_graph_models_unit PRIVATE ${TESSERACT_COMPILE_DEFINITIONS})
target_clang_tidy(${PROJECT_NAME}_scene_graph_models_unit ENABLE ${TESSERACT_ENABLE_CLANG_TIDY})
target_cxx_version(${PROJECT_NAME}_scene_graph_models_unit PUBLIC VERSION ${TESSERACT_CXX_VERSION})
target_code_coverage(
  ${PROJECT_NAME}_scene_graph_models_unit
  PRIVATE
  ALL
  EXCLUDE ${COVERAGE_EXCLUDE}
  ENABLE ${TESSERACT_ENABLE_CODE_COVERAGE})
add_gtest_discover_tests(${PROJECT_NAME}_scene_graph_models_unit)
add_dependencies(${PROJECT_NAME}_scene_graph_models_unit ${PROJECT_NAME}_scene_graph_models)
if(TARGET run_tests)
  add_dependencies(run_tests ${PROJECT_NAME}_scene_graph_models_unit)
endif()