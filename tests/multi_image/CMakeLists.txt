# One collector for several images: `core` defines SGCL's state, `a` and `b` are shared libraries using it,
# multi_image_test links all three and loads `d` at run time.
foreach(lib core a b)
    add_library(mi_${lib} SHARED ${lib}.cpp)
    set_target_properties(mi_${lib} PROPERTIES OUTPUT_NAME ${lib})
    target_include_directories(mi_${lib} PRIVATE ${CMAKE_SOURCE_DIR})
endforeach()
target_link_libraries(mi_a PRIVATE mi_core)
target_link_libraries(mi_b PRIVATE mi_a mi_core)

# Loaded by multi_image_test at run time from a worker thread, never linked.
add_library(mi_d SHARED d.cpp)
set_target_properties(mi_d PROPERTIES OUTPUT_NAME d)
target_include_directories(mi_d PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(mi_d PRIVATE mi_core)

add_executable(multi_image_test main.cpp)
add_dependencies(multi_image_test mi_d)
target_link_libraries(multi_image_test PRIVATE ${CMAKE_DL_LIBS})
target_include_directories(multi_image_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(multi_image_test PRIVATE mi_b mi_a mi_core)
# The executable's weak typeinfo stays dynamic so the loader merges it with the libraries' copies.
if(APPLE)
    target_link_options(multi_image_test PRIVATE -Wl,-export_dynamic)
elseif(UNIX)
    target_link_options(multi_image_test PRIVATE -Wl,--export-dynamic)
endif()
add_test(NAME multi_image_test COMMAND multi_image_test)
