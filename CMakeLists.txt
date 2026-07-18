cmake_minimum_required(VERSION 3.22)
project(drogon_blog LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /EHsc)
endif()

option(BLOG_BUILD_TESTS "Build the Drogon blog tests" ON)
set(BUILD_MD2HTML_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

find_package(Drogon CONFIG REQUIRED)
add_subdirectory(vendor/md4c)

if(WIN32 AND NOT DROGON_CTL_COMMAND)
    find_program(
        DROGON_CTL_COMMAND
        NAMES drogon_ctl
        HINTS "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/tools/drogon"
    )
endif()

set(BLOG_SOURCES
    main.cc
    controllers/PublicController.cc
    repositories/ContentRepository.cc
    services/MarkdownService.cc
)

add_executable(${PROJECT_NAME} ${BLOG_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon md4c-html)

drogon_create_views(
    ${PROJECT_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/views
    ${CMAKE_CURRENT_BINARY_DIR}
)

if(BLOG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
