
add_executable(media_ingest
    main.c
    status_pipe.c
    usb_watcher.c 
    ffmpeg_runner.c
    mounter.c
)
target_include_directories(media_ingest PRIVATE ${CMAKE_SOURCE_DIR}/src_indicator)