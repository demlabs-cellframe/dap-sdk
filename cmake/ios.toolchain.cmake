
# Fix for PThread library not in path
set(CMAKE_THREAD_LIBS_INIT "-lpthread")
set(CMAKE_HAVE_THREADS_LIBRARY 1)
set(CMAKE_USE_WIN32_THREADS_INIT 0)
set(CMAKE_USE_PTHREADS_INIT 1)

# Cache what generator is used
set(USED_CMAKE_GENERATOR "${CMAKE_GENERATOR}" CACHE STRING "Expose CMAKE_GENERATOR" FORCE)

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.14")
  set(MODERN_CMAKE YES)
endif()

# Get the Xcode version being used.
execute_process(COMMAND xcodebuild -version
  OUTPUT_VARIABLE XCODE_VERSION
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX MATCH "Xcode [0-9\\.]+" XCODE_VERSION "${XCODE_VERSION}")
string(REGEX REPLACE "Xcode ([0-9\\.]+)" "\\1" XCODE_VERSION "${XCODE_VERSION}")

if(NOT DEFINED PLATFORM)
  if (CMAKE_OSX_ARCHITECTURES)
    if(CMAKE_OSX_ARCHITECTURES MATCHES ".*arm.*" AND CMAKE_OSX_SYSROOT MATCHES ".*iphoneos.*")
      set(PLATFORM "OS")
    elseif(CMAKE_OSX_ARCHITECTURES MATCHES "i386" AND CMAKE_OSX_SYSROOT MATCHES ".*iphonesimulator.*")
      set(PLATFORM "SIMULATOR")
    elseif(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64" AND CMAKE_OSX_SYSROOT MATCHES ".*iphonesimulator.*")
      set(PLATFORM "SIMULATOR64")
    endif()
  endif()
  if (NOT PLATFORM)
    set(PLATFORM "OS")
  endif()
endif()

set(PLATFORM_INT "${PLATFORM}" CACHE STRING "Type of platform for which the build targets.")

if(PLATFORM_INT STREQUAL "OS")
  set(SDK_NAME iphoneos)
  if(NOT ARCHS)
    set(ARCHS armv7 armv7s arm64)
    set(APPLE_TARGET_TRIPLE_INT arm-apple-ios)
  endif()
elseif(PLATFORM_INT STREQUAL "OS64")
  set(SDK_NAME iphoneos)
  if(NOT ARCHS)
    if (XCODE_VERSION VERSION_GREATER 10.0)
      set(ARCHS arm64) # Add arm64e when Apple have fixed the integration issues with it, libarclite_iphoneos.a is currently missung bitcode markers for example
    else()
      set(ARCHS arm64)
    endif()
    set(APPLE_TARGET_TRIPLE_INT aarch64-apple-ios)
  endif()
elseif(PLATFORM_INT STREQUAL "OS64COMBINED")
  set(SDK_NAME iphoneos)
  if(MODERN_CMAKE)
    if(NOT ARCHS)
      if (XCODE_VERSION VERSION_GREATER 10.0)
        set(ARCHS arm64 x86_64) # Add arm64e when Apple have fixed the integration issues with it, libarclite_iphoneos.a is currently missung bitcode markers for example
      else()
        set(ARCHS arm64 x86_64)
      endif()
      set(APPLE_TARGET_TRIPLE_INT aarch64-x86_64-apple-ios)
    endif()
  else()
    message(FATAL_ERROR "Please make sure that you are running CMake 3.14+ to make the OS64COMBINED setting work")
  endif()
elseif(PLATFORM_INT STREQUAL "SIMULATOR")
  set(SDK_NAME iphonesimulator)
  if(NOT ARCHS)
    set(ARCHS i386)
    set(APPLE_TARGET_TRIPLE_INT i386-apple-ios)
  endif()
  message(DEPRECATION "SIMULATOR IS DEPRECATED. Consider using SIMULATOR64 instead.")
elseif(PLATFORM_INT STREQUAL "SIMULATOR64")
  set(SDK_NAME iphonesimulator)
  if(NOT ARCHS)
    set(ARCHS x86_64)
    set(APPLE_TARGET_TRIPLE_INT x86_64-apple-ios)
  endif()
  else()
  message(FATAL_ERROR "Invalid PLATFORM: ${PLATFORM_INT}")
endif()

if(MODERN_CMAKE AND PLATFORM_INT MATCHES ".*COMBINED" AND NOT USED_CMAKE_GENERATOR MATCHES "Xcode")
  message(FATAL_ERROR "The COMBINED options only work with Xcode generator, -G Xcode")
endif()

# If user did not specify the SDK root to use, then query xcodebuild for it.
execute_process(COMMAND xcodebuild -version -sdk ${SDK_NAME} Path
    OUTPUT_VARIABLE CMAKE_OSX_SYSROOT_INT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT DEFINED CMAKE_OSX_SYSROOT_INT AND NOT DEFINED CMAKE_OSX_SYSROOT)
  message(SEND_ERROR "Please make sure that Xcode is installed and that the toolchain"
  "is pointing to the correct path. Please run:"
  "sudo xcode-select -s /Applications/Xcode.app/Contents/Developer"
  "and see if that fixes the problem for you.")
  message(FATAL_ERROR "Invalid CMAKE_OSX_SYSROOT: ${CMAKE_OSX_SYSROOT} "
  "does not exist.")
elseif(DEFINED CMAKE_OSX_SYSROOT_INT)
   set(CMAKE_OSX_SYSROOT "${CMAKE_OSX_SYSROOT_INT}" CACHE INTERNAL "")
endif()

set(CMAKE_SYSTEM_VERSION ${SDK_VERSION} CACHE INTERNAL "")
set(UNIX TRUE CACHE BOOL "")
set(APPLE TRUE CACHE BOOL "")
set(IOS TRUE CACHE BOOL "")
set(CMAKE_AR ar CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB ranlib CACHE FILEPATH "" FORCE)
set(CMAKE_STRIP strip CACHE FILEPATH "" FORCE)

set(CMAKE_OSX_ARCHITECTURES ${ARCHS} CACHE STRING "Build architecture for iOS")


set(CMAKE_MACOSX_BUNDLE YES)
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO")
set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".dylib")
set(CMAKE_SHARED_MODULE_PREFIX "lib")
set(CMAKE_SHARED_MODULE_SUFFIX ".so")
set(CMAKE_C_COMPILER_ABI ELF)
set(CMAKE_CXX_COMPILER_ABI ELF)
set(CMAKE_C_HAS_ISYSROOT 1)
set(CMAKE_CXX_HAS_ISYSROOT 1)
set(CMAKE_MODULE_EXISTS 1)
set(CMAKE_DL_LIBS "")
set(CMAKE_C_OSX_COMPATIBILITY_VERSION_FLAG "-compatibility_version ")
set(CMAKE_C_OSX_CURRENT_VERSION_FLAG "-current_version ")
set(CMAKE_CXX_OSX_COMPATIBILITY_VERSION_FLAG "${CMAKE_C_OSX_COMPATIBILITY_VERSION_FLAG}")
set(CMAKE_CXX_OSX_CURRENT_VERSION_FLAG "${CMAKE_C_OSX_CURRENT_VERSION_FLAG}")


if(ARCHS MATCHES "((^|;|, )(arm64|arm64e|x86_64))+")
  set(CMAKE_C_SIZEOF_DATA_PTR 8)
  set(CMAKE_CXX_SIZEOF_DATA_PTR 8)
  if(ARCHS MATCHES "((^|;|, )(arm64|arm64e))+")
    set(CMAKE_SYSTEM_PROCESSOR "aarch64")
  else()
    set(CMAKE_SYSTEM_PROCESSOR "x86_64")
  endif()
else()
  set(CMAKE_C_SIZEOF_DATA_PTR 4)
  set(CMAKE_CXX_SIZEOF_DATA_PTR 4)
  set(CMAKE_SYSTEM_PROCESSOR "arm")
endif()
