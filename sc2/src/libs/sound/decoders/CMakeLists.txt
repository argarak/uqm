add_library(uqm_sound_decoders aiffaud.c aiffaud.h
                               decoder.c decoder.h
                               dukaud.c dukaud.h
                               modaud.c modaud.h
                               wav.c wav.h)
if(OGGVORBIS)
    target_sources(uqm_sound_decoders PRIVATE oggaud.c oggaud.h)
endif()
target_link_libraries(uqm_sound_decoders PRIVATE uqm_options uqm_lib_vorbis)

if(USE_INTERNAL_MIKMOD)
    target_link_libraries(uqm_sound_decoders PRIVATE uqm_mikmod)
else()
    message(FATAL_ERROR "TODO")
endif()