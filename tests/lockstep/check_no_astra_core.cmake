# cmake -DBINARY=<executable> -P check_no_astra_core.cmake
# ADR-0008's boundary on a built binary (M4 PR 4's echo peer; PR 5 adds the autopilot's own test):
# fails if its dynamic section NEEDs libastra_core or it has any undefined astra:: (_ZN5astra)
# symbol. Defined astra_hal_* C symbols from the statically linked HAL are allowed.
if(NOT DEFINED BINARY)
  message(FATAL_ERROR "BINARY is required")
endif()
find_program(READELF readelf REQUIRED)
find_program(NM nm REQUIRED)

execute_process(COMMAND "${READELF}" -d "${BINARY}" OUTPUT_VARIABLE dynamic RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "readelf failed on ${BINARY}")
endif()
if(dynamic MATCHES "libastra_core")
  message(FATAL_ERROR "${BINARY} NEEDs libastra_core (ADR-0008: embedded links only icd/ and the HAL)")
endif()

execute_process(COMMAND "${NM}" --undefined-only "${BINARY}" OUTPUT_VARIABLE undefined
                RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "nm failed on ${BINARY}")
endif()
string(REGEX MATCH "_ZN5astra[^\n]*" hit "${undefined}")
if(hit)
  message(FATAL_ERROR "${BINARY} references an astra_core symbol: ${hit}")
endif()
message(STATUS "${BINARY}: no libastra_core dependency and no undefined astra:: symbol")
