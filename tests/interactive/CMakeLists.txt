macro(add_interact_test name)
  add_test(NAME ${name} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} COMMAND ${PYTHON_EXECUTABLE} ${name}.py $<TARGET_FILE:psi>)
endmacro()

add_interact_test(error_recover)
add_interact_test(variable_reuse)
