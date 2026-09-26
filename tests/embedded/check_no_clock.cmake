# cmake -DOBJECTS="<file>;<file>..." -P check_no_clock.cmake
# The autopilot reads no clock (docs/plans/m4-pr5-autopilot.md D3): time reaches the control law only as the
# frame-derived hal_now() argument. Fails if any of the given object files or static-library archives has an
# undefined clock, sleep, or <chrono> symbol. Run on each autopilot object (the library's members and
# main.o), never on the linked binary: that legitimately contains astra_hal's one permitted site (ADR-0023).
if(NOT DEFINED OBJECTS)
  message(FATAL_ERROR "OBJECTS is required")
endif()
find_program(NM nm REQUIRED)

set(forbidden "clock_gettime|gettimeofday|time|clock|timespec_get|nanosleep|clock_nanosleep|usleep|sleep")
foreach(object IN LISTS OBJECTS)
  execute_process(COMMAND "${NM}" --undefined-only "${object}" OUTPUT_VARIABLE undefined
                  RESULT_VARIABLE status)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "nm failed on ${object}")
  endif()
  string(REPLACE "\n" ";" lines "${undefined}")
  foreach(line IN LISTS lines)
    if(line MATCHES "^ *U +(${forbidden})(@.*)?$" OR line MATCHES "^ *U +.*St6chrono")
      message(FATAL_ERROR "${object} references a clock or sleep symbol: ${line}")
    endif()
  endforeach()
  message(STATUS "${object}: no undefined clock, sleep, or <chrono> symbol")
endforeach()
