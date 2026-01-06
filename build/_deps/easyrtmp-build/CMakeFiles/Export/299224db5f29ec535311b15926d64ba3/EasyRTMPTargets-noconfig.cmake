#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "EasyRTMP::easyrtmp" for configuration ""
set_property(TARGET EasyRTMP::easyrtmp APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(EasyRTMP::easyrtmp PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/easyrtmp/libeasyrtmp.a"
  )

list(APPEND _cmake_import_check_targets EasyRTMP::easyrtmp )
list(APPEND _cmake_import_check_files_for_EasyRTMP::easyrtmp "${_IMPORT_PREFIX}/lib/easyrtmp/libeasyrtmp.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
