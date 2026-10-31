#ifndef VERSION_H
#define VERSION_H

/* Single source of truth for the human-readable game version. Why a header: both build systems
  already inject the real value at compile time (Makefile: -DVERSION_STRING=\"$(VERSION)\"; ... */
#ifndef VERSION_STRING
#define VERSION_STRING "0.10.0"
#endif

#endif  // VERSION_H
