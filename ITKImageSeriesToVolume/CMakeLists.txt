project(ITKImageSeriesToVolume)

cmake_minimum_required(VERSION 2.6)


# On Visual Studio 8 MS deprecated C. This removes all 1.276E1265 security
# warnings. Copied from ITK CMakeLists.
IF(WIN32)
  IF(NOT BORLAND)
    IF(NOT CYGWIN)
      IF(NOT MINGW)
          ADD_DEFINITIONS(
            -D_CRT_FAR_MAPPINGS_NO_DEPRECATE
            -D_CRT_IS_WCTYPE_NO_DEPRECATE
            -D_CRT_MANAGED_FP_NO_DEPRECATE
            -D_CRT_NONSTDC_NO_DEPRECATE
            -D_CRT_SECURE_NO_DEPRECATE
            -D_CRT_SECURE_NO_DEPRECATE_GLOBALS
            -D_CRT_SETERRORMODE_BEEP_SLEEP_NO_DEPRECATE
            -D_CRT_TIME_FUNCTIONS_NO_DEPRECATE
            -D_CRT_VCCLRIT_NO_DEPRECATE
            -D_SCL_SECURE_NO_DEPRECATE
            )
      ENDIF(NOT MINGW)
    ENDIF(NOT CYGWIN)
  ENDIF(NOT BORLAND)
ENDIF(WIN32)

#
# Define EXECUTABLE_OUTPUT_PATH and LIBRARY_OUTPUT_PATH
#
SET(EXECUTABLE_OUTPUT_PATH 
 ${CMAKE_BINARY_DIR}/bin 
 CACHE PATH "Single output directory for building all executables."
)

SET(LIBRARY_OUTPUT_PATH 
 ${CMAKE_BINARY_DIR}/lib 
 CACHE PATH "Single output directory for building all libraries."
)

find_package(ITK REQUIRED)
include(${ITK_USE_FILE})

add_executable(imageSeriesToVolume
imageSeriesToVolume.cxx
)
target_link_libraries(imageSeriesToVolume
${ITK_LIBRARIES}
)
