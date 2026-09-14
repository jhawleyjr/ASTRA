# The interface control document (ADR-0006): C11 headers only, no dependencies. Consumed by the core's
# transports and by embedded/, which links this and the HAL but never astra_core (ADR-0008).
add_library(astra_icd INTERFACE)
add_library(astra::icd ALIAS astra_icd)
target_include_directories(astra_icd INTERFACE "${ASTRA_SOURCE_DIR}")
