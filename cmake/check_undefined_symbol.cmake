# cmake -DLIB=<shared library> -DSYMBOL=<name> -DEXPECT=present|absent -P check_undefined_symbol.cmake
# Fails unless the library's undefined dynamic symbols (nm -D --undefined-only) match EXPECT for SYMBOL.
# Used to prove the ASTRA_STATIC_PLUGINS build never references dlopen, and that the shared build does.
foreach(var LIB SYMBOL EXPECT)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "${var} is required")
  endif()
endforeach()
if(NOT EXPECT MATCHES "^(present|absent)$")
  message(FATAL_ERROR "EXPECT must be present or absent, got '${EXPECT}'")
endif()

find_program(NM nm REQUIRED)
execute_process(COMMAND "${NM}" -D --undefined-only "${LIB}"
                OUTPUT_VARIABLE symbols RESULT_VARIABLE status ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "nm failed on ${LIB}: ${error}")
endif()

string(REGEX MATCH "(^|\n)[^\n]* U ${SYMBOL}(@[^\n]*)?(\n|$)" hit "${symbols}")
if(hit)
  set(found present)
else()
  set(found absent)
endif()
if(NOT found STREQUAL EXPECT)
  message(FATAL_ERROR "${LIB}: undefined symbol '${SYMBOL}' is ${found}, expected ${EXPECT}")
endif()
message(STATUS "${LIB}: undefined symbol '${SYMBOL}' is ${found}, as expected")
