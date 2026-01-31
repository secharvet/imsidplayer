
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was install-config.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

include(CMakeFindDependencyMacro)

# Handle EETF format dependency (Erlang)
# The FindErlang module is embedded directly to avoid file collisions with other projects
set(_glaze_EETF_FORMAT "")
if(_glaze_EETF_FORMAT)

endif()
unset(_glaze_EETF_FORMAT)

# Handle SSL dependency (OpenSSL)
set(_glaze_ENABLE_SSL "")
if(_glaze_ENABLE_SSL)
    find_dependency(OpenSSL REQUIRED)
endif()
unset(_glaze_ENABLE_SSL)

include("${CMAKE_CURRENT_LIST_DIR}/glazeTargets.cmake")
