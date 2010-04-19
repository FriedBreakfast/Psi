include_directories(${Boost_INCLUDE_DIRS})

add_executable(type-test TypeTest.cpp)

add_executable(test TestMain.cpp VariantTest.cpp)
set_target_properties(test PROPERTIES COMPILE_DEFINITIONS "BOOST_TEST_DYN_LINK")
target_link_libraries(test ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY})