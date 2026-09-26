# cmake -DCXX=<compiler> -DROOT=<repo root> -DSOURCES="<file>;<file>..." -P check_include_closure.cmake
# The include closure of the autopilot's sources is embedded/ and icd/ only (ADR-0008): no core/, plugins/,
# aircraft/ or Eigen header can reach it. `-MM` lists every non-system header a source pulls in; each must lie
# under <ROOT>/embedded/ or <ROOT>/icd/ (the HAL's public header is embedded/hal/astra_hal.h).
foreach(var CXX ROOT SOURCES)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "${var} is required")
  endif()
endforeach()

foreach(source IN LISTS SOURCES)
  execute_process(COMMAND "${CXX}" -std=c++17 "-I${ROOT}" -MM "${source}"
                  OUTPUT_VARIABLE deps RESULT_VARIABLE status ERROR_VARIABLE err)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "${CXX} -MM failed on ${source}: ${err}")
  endif()
  # "target.o: a.cpp b.hpp \<newline> c.hpp": drop the target, the line continuations, split on spaces.
  string(REGEX REPLACE "^[^:]*:" "" deps "${deps}")
  string(REPLACE "\\\n" " " deps "${deps}")
  string(REGEX REPLACE "[ \t\r\n]+" ";" deps "${deps}")
  foreach(dep IN LISTS deps)
    if(dep STREQUAL "")
      continue()
    endif()
    get_filename_component(path "${dep}" ABSOLUTE)
    if(NOT (path MATCHES "^${ROOT}/embedded/" OR path MATCHES "^${ROOT}/icd/"))
      message(FATAL_ERROR "${source} includes ${path}, outside embedded/ and icd/ (ADR-0008)")
    endif()
  endforeach()
  message(STATUS "${source}: include closure inside embedded/ and icd/")
endforeach()
