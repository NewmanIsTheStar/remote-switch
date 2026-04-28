target_include_directories(remote-switch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(remote-switch PRIVATE
        worker_tasks.c        
        rmtsw_task.c
        rmtsw_web_ui.c
        rmtsw_relay.c
        mqtt_task.c
)

set(WEB_FILES)
file(GLOB WEB_FILES "${CMAKE_CURRENT_SOURCE_DIR}/html_files/*.shtml")
pico_add_library(pico_httpd_content NOFLAG)
pico_set_lwip_httpd_content(pico_httpd_content INTERFACE ${WEB_FILES})
