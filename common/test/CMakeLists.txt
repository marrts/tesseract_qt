find_gtest()

add_executable(${PROJECT_NAME}_common_threading_unit
               environment_wrapper_threading_unit.cpp
               scene_graph_render_manager_unit.cpp)
target_link_libraries(
  ${PROJECT_NAME}_common_threading_unit
  PRIVATE GTest::GTest
          GTest::Main
          ${PROJECT_NAME}_common
          tesseract::tesseract_environment
          tesseract::tesseract_scene_graph
          Qt5::Core
          Qt5::Gui
          Qt5::Widgets)
target_compile_options(${PROJECT_NAME}_common_threading_unit PRIVATE ${TESSERACT_COMPILE_OPTIONS_PRIVATE}
                                                                     ${TESSERACT_COMPILE_OPTIONS_PUBLIC})
target_compile_definitions(${PROJECT_NAME}_common_threading_unit PRIVATE ${TESSERACT_COMPILE_DEFINITIONS})
target_clang_tidy(${PROJECT_NAME}_common_threading_unit ENABLE ${TESSERACT_ENABLE_CLANG_TIDY})
target_cxx_version(${PROJECT_NAME}_common_threading_unit PUBLIC VERSION ${TESSERACT_CXX_VERSION})
target_code_coverage(
  ${PROJECT_NAME}_common_threading_unit
  PRIVATE
  ALL
  EXCLUDE ${COVERAGE_EXCLUDE}
  ENABLE ${TESSERACT_ENABLE_CODE_COVERAGE})
add_gtest_discover_tests(${PROJECT_NAME}_common_threading_unit)
add_dependencies(${PROJECT_NAME}_common_threading_unit ${PROJECT_NAME}_common)
if(TARGET run_tests)
  add_dependencies(run_tests ${PROJECT_NAME}_common_threading_unit)
endif()