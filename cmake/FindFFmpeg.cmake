include(FindPackageHandleStandardArgs)

set(FFMPEG_ROOT "" CACHE PATH "Root directory of the FFmpeg installation")
set(FFMPEG_INCLUDE_DIR "" CACHE PATH "Directory containing FFmpeg headers")
set(FFMPEG_AVFORMAT_LIBRARY "" CACHE FILEPATH "Path to the avformat library")
set(FFMPEG_AVCODEC_LIBRARY "" CACHE FILEPATH "Path to the avcodec library")
set(FFMPEG_AVUTIL_LIBRARY "" CACHE FILEPATH "Path to the avutil library")
set(FFMPEG_SWRESAMPLE_LIBRARY "" CACHE FILEPATH "Path to the swresample library")
set(FFMPEG_SWSCALE_LIBRARY "" CACHE FILEPATH "Path to the swscale library")

set(_ffmpeg_root_hints)
if(FFMPEG_ROOT)
    list(APPEND _ffmpeg_root_hints "${FFMPEG_ROOT}")
endif()
if(DEFINED ENV{FFMPEG_ROOT} AND NOT "$ENV{FFMPEG_ROOT}" STREQUAL "")
    list(APPEND _ffmpeg_root_hints "$ENV{FFMPEG_ROOT}")
endif()

if(NOT FFMPEG_INCLUDE_DIR)
    find_path(FFMPEG_INCLUDE_DIR
        NAMES libavformat/avformat.h
        HINTS ${_ffmpeg_root_hints}
        PATH_SUFFIXES include
    )
endif()

set(_ffmpeg_components avformat avcodec avutil swresample swscale)

foreach(_component IN LISTS _ffmpeg_components)
    string(TOUPPER "${_component}" _component_upper)
    set(_library_var "FFMPEG_${_component_upper}_LIBRARY")

    if(NOT DEFINED ${_library_var} OR "${${_library_var}}" STREQUAL "")
        find_library(${_library_var}
            NAMES ${_component} lib${_component}
            HINTS ${_ffmpeg_root_hints}
            PATH_SUFFIXES lib bin
        )
    endif()

    if(DEFINED ${_library_var}
       AND NOT "${${_library_var}}" STREQUAL ""
       AND NOT "${${_library_var}}" MATCHES "-NOTFOUND$"
       AND NOT TARGET FFmpeg::${_component})
        add_library(FFmpeg::${_component} UNKNOWN IMPORTED)
        set_target_properties(FFmpeg::${_component} PROPERTIES
            IMPORTED_LOCATION "${${_library_var}}"
            INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
        )
    endif()

    if(DEFINED ${_library_var}
       AND NOT "${${_library_var}}" STREQUAL ""
       AND NOT "${${_library_var}}" MATCHES "-NOTFOUND$")
        set(FFmpeg_${_component}_FOUND TRUE)
    else()
        set(FFmpeg_${_component}_FOUND FALSE)
    endif()
endforeach()

find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS
        FFMPEG_INCLUDE_DIR
        FFMPEG_AVFORMAT_LIBRARY
        FFMPEG_AVCODEC_LIBRARY
        FFMPEG_AVUTIL_LIBRARY
        FFMPEG_SWRESAMPLE_LIBRARY
        FFMPEG_SWSCALE_LIBRARY
    HANDLE_COMPONENTS
)

if(FFmpeg_FOUND)
    set(FFmpeg_INCLUDE_DIRS "${FFMPEG_INCLUDE_DIR}")
    set(FFmpeg_LIBRARIES
        FFmpeg::avformat
        FFmpeg::avcodec
        FFmpeg::avutil
        FFmpeg::swresample
        FFmpeg::swscale
    )
endif()
