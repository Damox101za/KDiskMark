# Unit tests, built with -DBUILD_TESTS=ON and run with `ctest`.
if(USE_QT5)
    find_package(Qt5 COMPONENTS Test REQUIRED)
    set(QT_TEST_LIB Qt5::Test)
else()
    find_package(Qt6 COMPONENTS Test REQUIRED)
    set(QT_TEST_LIB Qt6::Test)
endif()

function(kdiskmark_add_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(${name} PRIVATE ${QT_TEST_LIB})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

kdiskmark_add_test(tst_reportdata ${CMAKE_SOURCE_DIR}/src/reportdata.cpp)
