#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "TBB::tbb" for configuration "Debug"
set_property(TARGET TBB::tbb APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(TBB::tbb PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/VS2015/ARM64/lib/tbb12_debug.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/VS2015/ARM64/bin/tbb12_debug.dll"
  )

list(APPEND _cmake_import_check_targets TBB::tbb )
list(APPEND _cmake_import_check_files_for_TBB::tbb "${_IMPORT_PREFIX}/VS2015/ARM64/lib/tbb12_debug.lib" "${_IMPORT_PREFIX}/VS2015/ARM64/bin/tbb12_debug.dll" )

# Import target "TBB::tbbmalloc" for configuration "Debug"
set_property(TARGET TBB::tbbmalloc APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(TBB::tbbmalloc PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/VS2015/ARM64/lib/tbbmalloc_debug.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/VS2015/ARM64/bin/tbbmalloc_debug.dll"
  )

list(APPEND _cmake_import_check_targets TBB::tbbmalloc )
list(APPEND _cmake_import_check_files_for_TBB::tbbmalloc "${_IMPORT_PREFIX}/VS2015/ARM64/lib/tbbmalloc_debug.lib" "${_IMPORT_PREFIX}/VS2015/ARM64/bin/tbbmalloc_debug.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
