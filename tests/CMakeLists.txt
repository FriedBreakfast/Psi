find_package(PythonInterp REQUIRED)

macro(add_psi_test name)
  add_test(NAME ${name} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} COMMAND ${PYTHON_EXECUTABLE} run_compare.py ${name}.expect $<TARGET_FILE:psi> ${name}.psi)
endmacro()

add_psi_test(import)
add_psi_test(construct_destruct)
add_psi_test(construct_destruct_global)
