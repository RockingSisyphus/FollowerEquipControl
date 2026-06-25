if(NOT DEFINED OUTPUT_FOLDER OR OUTPUT_FOLDER STREQUAL "")
  message(FATAL_ERROR "Deploy.cmake: OUTPUT_FOLDER is not set")
endif()

if(NOT DEFINED DLL_PATH OR DLL_PATH STREQUAL "")
  message(FATAL_ERROR "Deploy.cmake: DLL_PATH is not set")
endif()

if(NOT EXISTS "${DLL_PATH}")
  message(FATAL_ERROR "Deploy.cmake: DLL_PATH does not exist: ${DLL_PATH}")
endif()

set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")
file(MAKE_DIRECTORY "${DLL_FOLDER}")

get_filename_component(_dll_name "${DLL_PATH}" NAME)
file(COPY_FILE "${DLL_PATH}" "${DLL_FOLDER}/${_dll_name}" ONLY_IF_DIFFERENT)

if(DEFINED PDB_PATH AND NOT PDB_PATH STREQUAL "" AND EXISTS "${PDB_PATH}")
  get_filename_component(_pdb_name "${PDB_PATH}" NAME)
  file(COPY_FILE "${PDB_PATH}" "${DLL_FOLDER}/${_pdb_name}" ONLY_IF_DIFFERENT)
endif()
