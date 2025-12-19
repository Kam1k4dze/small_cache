cmake_minimum_required(VERSION 3.15...3.26)

project(small_cache LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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
        COMPONENTS flat_hash_map hash
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
        1.89.0
        EXACT # Minimum or EXACT version e.g. 1.86.0
        REQUIRED # Fail with error if Boost is not found
        COMPONENTS flyweight
)
include_directories(src/lib)

if (NOT SKBUILD)
    message(STATUS "Building native executable for testing")
    FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.17.0
            GIT_SHALLOW TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    add_executable(small_cache_native_test src/lib/SmallCache.cpp src/native/test_SmallCache.cpp)
    target_link_libraries(
            small_cache_native_test
            PRIVATE
            glaze::glaze
            tsl::sparse_map
            absl::flat_hash_map
            absl::hash
            Boost::flyweight
            GTest::gtest_main
    )

    # Add native executable only if src/native/main.cpp exists
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/native/main.cpp")
        add_executable(small_cache_native src/lib/SmallCache.cpp src/native/main.cpp)
        target_link_libraries(
                small_cache_native
                PRIVATE
                glaze::glaze
                tsl::sparse_map
                absl::flat_hash_map
                absl::hash
                Boost::flyweight
        )
    else ()
        message(STATUS "Skipping creation of small_cache_native: src/native/main.cpp not found")
    endif ()


endif ()
if (SKBUILD)
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
            src/lib/SmallCache.cpp
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

endif ()

