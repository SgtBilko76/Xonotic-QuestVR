# XonoticQuest native build: DarkPlaces (Xonotic fork, branch quest-vr) + OpenXR VR layer + deps.
cmake_minimum_required(VERSION 3.22)
project(xonoticquest C CXX ASM)

get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(DEPS "${ROOT}/deps")
if(NOT DEFINED DP_SOURCE_DIR)
    get_filename_component(DP_SOURCE_DIR "${ROOT}/../xonotic/darkplaces" ABSOLUTE)
endif()
set(DP "${DP_SOURCE_DIR}")
if(NOT EXISTS "${DP}/makefile.inc")
    message(FATAL_ERROR "DarkPlaces source not found at ${DP} (expected the xonotic/darkplaces clone, branch quest-vr)")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET default)
# Libraries that DarkPlaces dlopen()s at runtime are built shared and packaged by name.
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

# ---------------------------------------------------------------- SDL2 (shared)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI ON CACHE BOOL "" FORCE)
add_subdirectory("${DEPS}/SDL" SDL)

# ---------------------------------------------------------------- zlib (static, LINK_TO_ZLIB)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${DEPS}/zlib" zlib EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------- IJG libjpeg 9 (static, LINK_TO_LIBJPEG)
# upstream jpeg.c refuses to dlopen libjpeg on Android, so it must be linked in.
set(JPEGSRC "${DEPS}/jpeg")
configure_file(${JPEGSRC}/jconfig.txt ${CMAKE_BINARY_DIR}/jpeg/jconfig.h COPYONLY)
# jpeg-9 makes 'boolean' an enum; DarkPlaces' callbacks use int (jboolean) -> keep it an int here.
file(APPEND ${CMAKE_BINARY_DIR}/jpeg/jconfig.h "\n#define HAVE_BOOLEAN 1\ntypedef int boolean;\n#ifndef FALSE\n#define FALSE 0\n#endif\n#ifndef TRUE\n#define TRUE 1\n#endif\n")
add_library(jpeg-static STATIC
    ${JPEGSRC}/jaricom.c ${JPEGSRC}/jcapimin.c ${JPEGSRC}/jcapistd.c ${JPEGSRC}/jcarith.c ${JPEGSRC}/jccoefct.c
    ${JPEGSRC}/jccolor.c ${JPEGSRC}/jcdctmgr.c ${JPEGSRC}/jchuff.c ${JPEGSRC}/jcinit.c ${JPEGSRC}/jcmainct.c
    ${JPEGSRC}/jcmarker.c ${JPEGSRC}/jcmaster.c ${JPEGSRC}/jcomapi.c ${JPEGSRC}/jcparam.c ${JPEGSRC}/jcprepct.c
    ${JPEGSRC}/jcsample.c ${JPEGSRC}/jctrans.c ${JPEGSRC}/jdapimin.c ${JPEGSRC}/jdapistd.c ${JPEGSRC}/jdarith.c
    ${JPEGSRC}/jdatadst.c ${JPEGSRC}/jdatasrc.c ${JPEGSRC}/jdcoefct.c ${JPEGSRC}/jdcolor.c ${JPEGSRC}/jddctmgr.c
    ${JPEGSRC}/jdhuff.c ${JPEGSRC}/jdinput.c ${JPEGSRC}/jdmainct.c ${JPEGSRC}/jdmarker.c ${JPEGSRC}/jdmaster.c
    ${JPEGSRC}/jdmerge.c ${JPEGSRC}/jdpostct.c ${JPEGSRC}/jdsample.c ${JPEGSRC}/jdtrans.c ${JPEGSRC}/jerror.c
    ${JPEGSRC}/jfdctflt.c ${JPEGSRC}/jfdctfst.c ${JPEGSRC}/jfdctint.c ${JPEGSRC}/jidctflt.c ${JPEGSRC}/jidctfst.c
    ${JPEGSRC}/jidctint.c ${JPEGSRC}/jquant1.c ${JPEGSRC}/jquant2.c ${JPEGSRC}/jutils.c ${JPEGSRC}/jmemmgr.c
    ${JPEGSRC}/jmemnobs.c)
target_include_directories(jpeg-static PUBLIC ${CMAKE_BINARY_DIR}/jpeg ${JPEGSRC})
target_compile_options(jpeg-static PRIVATE -Wno-everything)

# ---------------------------------------------------------------- ogg + vorbis (static, LINK_TO_LIBVORBIS)
set(OGGVORBIS "${DEPS}/oggvorbis")
add_library(ogg STATIC
    ${OGGVORBIS}/libogg/bitwise.c
    ${OGGVORBIS}/libogg/framing.c)
target_include_directories(ogg PUBLIC ${OGGVORBIS}/include)
add_library(vorbis STATIC
    ${OGGVORBIS}/libvorbis/mdct.c ${OGGVORBIS}/libvorbis/smallft.c ${OGGVORBIS}/libvorbis/block.c
    ${OGGVORBIS}/libvorbis/envelope.c ${OGGVORBIS}/libvorbis/window.c ${OGGVORBIS}/libvorbis/lsp.c
    ${OGGVORBIS}/libvorbis/lpc.c ${OGGVORBIS}/libvorbis/analysis.c ${OGGVORBIS}/libvorbis/synthesis.c
    ${OGGVORBIS}/libvorbis/psy.c ${OGGVORBIS}/libvorbis/info.c ${OGGVORBIS}/libvorbis/floor1.c
    ${OGGVORBIS}/libvorbis/floor0.c ${OGGVORBIS}/libvorbis/res0.c ${OGGVORBIS}/libvorbis/mapping0.c
    ${OGGVORBIS}/libvorbis/registry.c ${OGGVORBIS}/libvorbis/codebook.c ${OGGVORBIS}/libvorbis/sharedbook.c
    ${OGGVORBIS}/libvorbis/lookup.c ${OGGVORBIS}/libvorbis/bitrate.c ${OGGVORBIS}/libvorbis/vorbisfile.c
    ${OGGVORBIS}/libvorbis/vorbisenc.c)
target_include_directories(vorbis PUBLIC ${OGGVORBIS}/include PRIVATE ${OGGVORBIS}/libvorbis)
target_compile_options(vorbis PRIVATE -ffast-math -fsigned-char -Wno-everything)
target_link_libraries(vorbis PUBLIC ogg)

# ---------------------------------------------------------------- libpng (shared -> libpng.so, dlopen'd by image_png.c)
set(PNGSRC "${DEPS}/libpng")
configure_file(${PNGSRC}/scripts/pnglibconf.h.prebuilt ${CMAKE_BINARY_DIR}/png/pnglibconf.h COPYONLY)
add_library(png SHARED
    ${PNGSRC}/png.c ${PNGSRC}/pngerror.c ${PNGSRC}/pngget.c ${PNGSRC}/pngmem.c ${PNGSRC}/pngpread.c
    ${PNGSRC}/pngread.c ${PNGSRC}/pngrio.c ${PNGSRC}/pngrtran.c ${PNGSRC}/pngrutil.c ${PNGSRC}/pngset.c
    ${PNGSRC}/pngtrans.c ${PNGSRC}/pngwio.c ${PNGSRC}/pngwrite.c ${PNGSRC}/pngwtran.c ${PNGSRC}/pngwutil.c
    ${PNGSRC}/arm/arm_init.c ${PNGSRC}/arm/filter_neon_intrinsics.c ${PNGSRC}/arm/palette_neon_intrinsics.c)
target_include_directories(png PRIVATE ${CMAKE_BINARY_DIR}/png ${PNGSRC})
target_compile_definitions(png PRIVATE PNG_ARM_NEON_OPT=2)
target_link_libraries(png PRIVATE zlibstatic)
set_target_properties(png PROPERTIES OUTPUT_NAME png)

# ---------------------------------------------------------------- freetype (static: sys.h defines DP_FREETYPE_STATIC on Android)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(DISABLE_FORCE_DEBUG_POSTFIX ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory("${DEPS}/freetype" freetype EXCLUDE_FROM_ALL)
set(BUILD_SHARED_LIBS ON)

# ---------------------------------------------------------------- curl (shared -> libcurl.so, dlopen'd by libcurl.c) HTTP only
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
set(CURL_ENABLE_SSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CMAKE_DEBUG_POSTFIX "")   # DP dlopen()s exactly "libcurl.so"
add_subdirectory("${DEPS}/curl" curl)

# ---------------------------------------------------------------- DarkPlaces
# Source list is parsed from upstream makefile.inc so it never drifts from the engine repo.
file(STRINGS "${DP}/makefile.inc" DP_MKINC_LINES)
function(dp_collect_objs outvar varname)
    set(_in 0)
    set(_out "")
    foreach(_line IN LISTS DP_MKINC_LINES)
        if(NOT _in)
            if(_line MATCHES "^${varname}=")
                set(_in 1)
                string(REGEX REPLACE "^${varname}=" "" _line "${_line}")
            else()
                continue()
            endif()
        endif()
        string(REGEX MATCHALL "[A-Za-z0-9_]+\\.o" _objs "${_line}")
        foreach(_o ${_objs})
            string(REGEX REPLACE "\\.o$" ".c" _c "${_o}")
            list(APPEND _out "${DP}/${_c}")
        endforeach()
        if(NOT _line MATCHES "\\\\[ \t]*$")
            break()
        endif()
    endforeach()
    set(${outvar} "${_out}" PARENT_SCOPE)
endfunction()
dp_collect_objs(DP_SRC_COMMON OBJ_COMMON)
dp_collect_objs(DP_SRC_MENU OBJ_MENU)
dp_collect_objs(DP_SRC_SND OBJ_SND_COMMON)
list(LENGTH DP_SRC_COMMON _n)
if(_n LESS 50)
    message(FATAL_ERROR "makefile.inc parse failed: only ${_n} OBJ_COMMON sources found")
endif()
set(DP_SRC_SDL
    ${DP}/sys_sdl.c ${DP}/vid_sdl.c ${DP}/thread_sdl.c ${DP}/snd_sdl.c ${DP}/builddate.c)
file(GLOB DP_SRC_VR CONFIGURE_DEPENDS "${DP}/vr/*.c")

execute_process(COMMAND git -C "${DP}" describe --always --dirty=~
    OUTPUT_VARIABLE DP_VCREVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT DP_VCREVISION)
    set(DP_VCREVISION "quest-vr")
endif()

add_library(xonotic SHARED ${DP_SRC_COMMON} ${DP_SRC_MENU} ${DP_SRC_SND} ${DP_SRC_SDL} ${DP_SRC_VR})
target_include_directories(xonotic PRIVATE
    ${DP} ${DP}/vr
    ${DEPS}/OpenXR-SDK/include)
target_compile_definitions(xonotic PRIVATE
    CONFIG_MENU
    USE_GLES2
    VR_QUEST
    LINK_TO_ZLIB LINK_TO_LIBJPEG LINK_TO_LIBVORBIS
    XR_USE_PLATFORM_ANDROID XR_USE_GRAPHICS_API_OPENGL_ES
    _FILE_OFFSET_BITS=64 __KERNEL_STRICT_NAMES
    VCREVISION=${DP_VCREVISION} BUILDTYPE=release)
target_compile_options(xonotic PRIVATE
    -O3 -fno-math-errno -fno-trapping-math -fno-strict-aliasing
    -Wall -Wno-unused-but-set-variable -Wno-unused-variable -Wno-sign-compare -Wno-deprecated-declarations
    -Wno-missing-braces -Wno-format -Wno-parentheses -Wno-switch
    -Wno-incompatible-function-pointer-types)
target_link_libraries(xonotic PRIVATE
    SDL2 zlibstatic jpeg-static vorbis ogg freetype
    ${DEPS}/OpenXR-SDK/lib/arm64-v8a/libopenxr_loader.so
    EGL GLESv3 android log m dl)
