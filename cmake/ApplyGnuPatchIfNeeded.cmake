foreach(required PATCH_SOURCE_DIR PATCH_FILE)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

find_program(PATCH_EXECUTABLE patch REQUIRED)
execute_process(
  COMMAND "${PATCH_EXECUTABLE}" --batch --dry-run --forward -p1
          -d "${PATCH_SOURCE_DIR}" -i "${PATCH_FILE}"
  RESULT_VARIABLE can_apply
  OUTPUT_QUIET
  ERROR_QUIET
)
if(can_apply EQUAL 0)
  execute_process(
    COMMAND "${PATCH_EXECUTABLE}" --batch --forward -p1
            -d "${PATCH_SOURCE_DIR}" -i "${PATCH_FILE}"
    RESULT_VARIABLE apply_result
  )
  if(NOT apply_result EQUAL 0)
    message(FATAL_ERROR "failed to apply ${PATCH_FILE}")
  endif()
  return()
endif()

execute_process(
  COMMAND "${PATCH_EXECUTABLE}" --batch --dry-run --reverse -p1
          -d "${PATCH_SOURCE_DIR}" -i "${PATCH_FILE}"
  RESULT_VARIABLE already_applied
  OUTPUT_QUIET
  ERROR_QUIET
)
if(NOT already_applied EQUAL 0)
  message(FATAL_ERROR "${PATCH_FILE} neither applies nor is already applied")
endif()
