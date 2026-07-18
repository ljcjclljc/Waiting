add_executable(blog_tests
    BlogCoreTest.cc
    ../repositories/ContentRepository.cc
    ../services/MarkdownService.cc
)

target_include_directories(blog_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(blog_tests PRIVATE Drogon::Drogon md4c-html)
if(MSVC)
    target_compile_options(blog_tests PRIVATE /utf-8 /EHsc)
endif()
ParseAndAddDrogonTests(blog_tests)
