#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "pugixml::static" for configuration "Debug"
set_property(TARGET pugixml::static APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(pugixml::static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/VS2015/x64/lib/pugixml_d.lib"
  )

list(APPEND _cmake_import_check_targets pugixml::static )
list(APPEND _cmake_import_check_files_for_pugixml::static "${_IMPORT_PREFIX}/VS2015/x64/lib/pugixml_d.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
