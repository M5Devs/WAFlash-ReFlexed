// WAFlash Re:Flexed — vendored gameswf compatibility config
#ifndef COMPATIBILITY_INCLUDE_H
#define COMPATIBILITY_INCLUDE_H

#ifdef _WIN32
  #undef TU_CONFIG_LINK_TO_THREAD
  #define TU_CONFIG_LINK_TO_THREAD 0
#else
  #define TU_CONFIG_LINK_TO_THREAD 2
#endif

#undef TU_USE_OGLES
#undef TU_USE_OPENAL
#undef TU_USE_SDL

#endif // COMPATIBILITY_INCLUDE_H
