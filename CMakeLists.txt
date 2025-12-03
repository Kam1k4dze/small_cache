cmake_minimum_required(VERSION 3.15...3.26)

project(small_cache LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CXX_STANDARD_REQUIRED On)
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (NOT SKBUILD)
    message(WARNING "\
  This CMake file is meant to be executed using 'scikit-build'. Running
  it directly will almost certainly not produce the desired result. If
  you are a user trying to install this package, please use the command
  below, which will install all necessary build dependencies, compile
  the package in an isolated environment, and then install it.
  =====================================================================
   $ pip install .
  =====================================================================
  If you are a software developer, and this is your own package, then
  it is usually much more efficient to install the build dependencies
  in your environment once and use the following command that avoids
  a costly creation of a new virtual environment at every compilation:
  =====================================================================
   $ pip install nanobind scikit-build-core[pyproject]
   $ pip install --no-build-isolation -ve .
  =====================================================================
  You may optionally add -Ceditable.rebuild=true to auto-rebuild when
  the package is imported. Otherwise, you need to re-run the above
  after editing C++ files.")
endif ()

# Try to import all Python components potentially needed by nanobind
find_package(Python 3.11
        REQUIRED COMPONENTS Interpreter Development.Module
        OPTIONAL_COMPONENTS Development.SABIModule)

# Import nanobind through CMake's find_package mechanism
find_package(nanobind CONFIG REQUIRED)

# We are now ready to compile the actual extension module
nanobind_add_module(
        # Name of the extension
        _small_cache_impl

        # Target the stable ABI for Python 3.12+, which reduces
        # the number of binary wheels that must be built. This
        # does nothing on older Python versions
        STABLE_ABI

        # Build libnanobind statically and merge it into the
        # extension (which itself remains a shared library)
        #
        # If your project builds multiple extensions, you can
        # replace this flag by NB_SHARED to conserve space by
        # reusing a shared libnanobind across libraries
        NB_STATIC

        # Source code goes here
        src/small_cache.cpp
)

include(FetchContent)
FetchContent_Declare(
        glaze
        GIT_REPOSITORY https://github.com/stephenberry/glaze.git
        GIT_TAG v6.1.0
        GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glaze)

FetchContent_Declare(
        tsl_sparse_map
        GIT_REPOSITORY https://github.com/Tessil/sparse-map
        GIT_TAG v0.7.0
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tsl_sparse_map)

FetchContent_Declare(
        absl
        GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
        GIT_TAG 20250814.1
        GIT_SHALLOW TRUE
        OVERRIDE_FIND_PACKAGE TRUE
        EXCLUDE_FROM_ALL
)

find_package(
        absl
        REQUIRED
        COMPONENTS flat_hash_map flat_hash_set hash
)

Set(FETCHCONTENT_QUIET FALSE)
FetchContent_Declare(
        Boost
        URL https://github.com/boostorg/boost/releases/download/boost-1.88.0/boost-1.88.0-cmake.7z
        GIT_TAG "boost-1.89.0"
        USES_TERMINAL_DOWNLOAD TRUE
        GIT_PROGRESS TRUE
        DOWNLOAD_NO_EXTRACT FALSE
        OVERRIDE_FIND_PACKAGE TRUE
        EXCLUDE_FROM_ALL
)
Set(FETCHCONTENT_QUIET TRUE)

find_package(
        Boost
        1.88.0
        EXACT # Minimum or EXACT version e.g. 1.86.0
        REQUIRED # Fail with error if Boost is not found
        COMPONENTS flyweight
)

target_link_libraries(
        _small_cache_impl
        PRIVATE
        glaze::glaze
        tsl::sparse_map
        absl::flat_hash_map
        absl::hash
        Boost::flyweight
)


# Install directive for scikit-build-core
install(TARGETS _small_cache_impl LIBRARY DESTINATION small_cache)
