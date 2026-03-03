add_library(usermod_epdiy INTERFACE)

target_sources(usermod_epdiy INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/epdiy_module.c
)

get_filename_component(_EPDIY_SRC_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../epdiy/src" ABSOLUTE)

target_include_directories(usermod_epdiy INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
    ${_EPDIY_SRC_DIR}
)

target_link_libraries(usermod INTERFACE usermod_epdiy)
