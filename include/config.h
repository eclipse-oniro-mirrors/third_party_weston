/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */

/* Build the Wayland clients */
/* #undef BUILD_CLIENTS */

/* Build the DRM compositor */
/* #undef BUILD_DRM_COMPOSITOR */

/* Build the fbdev compositor */
#define BUILD_FBDEV_COMPOSITOR 1

/* Build the headless compositor */
/* #undef BUILD_HEADLESS_COMPOSITOR */

/* Build the RDP compositor */
/* #undef BUILD_RDP_COMPOSITOR */

/* Build the vaapi recorder */
/* #undef BUILD_VAAPI_RECORDER */

/* Build the Wayland (nested) compositor */
/* #undef BUILD_WAYLAND_COMPOSITOR */

/* Build the wcap tools */
/* #undef BUILD_WCAP_TOOLS */

/* Build the X11 compositor */
/* #undef BUILD_X11_COMPOSITOR */

/* Build the X server launcher */
/* #undef BUILD_XWAYLAND */

/* Build Weston with EGL support */
/* #define ENABLE_EGL */

/* Build Weston with JUnit output support */
/* #undef ENABLE_JUNIT_XML */

/* Have cairoegl */
/* #undef HAVE_CAIRO_EGL */

/* Build with dbus support */
/* #undef HAVE_DBUS */

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* libdrm supports modifiers */
/* #undef HAVE_DRM_ADDFB2_MODIFIERS */

/* libdrm supports atomic API */
/* #undef HAVE_DRM_ATOMIC */


/* libdrm supports modifier advertisement */
/* #undef HAVE_DRM_FORMATS_BLOB */

/* Define to 1 if you have the <execinfo.h> header file. */
/* #undef HAVE_EXECINFO_H */

/* Define to 1 if you have the <freerdp/version.h> header file. */
/* #undef HAVE_FREERDP_VERSION_H */

/* gbm supports import with modifiers */
/* #undef HAVE_GBM_FD_IMPORT */

/* GBM supports modifiers */
/* #undef HAVE_GBM_MODIFIERS */

/* Define to 1 if you have the `initgroups' function. */
#define HAVE_INITGROUPS 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Have jpeglib */
/* #undef HAVE_JPEG */

/* Have lcms support */
/* #undef HAVE_LCMS */

/* Build etnaviv dmabuf client */
/* #undef HAVE_LIBDRM_ETNAVIV */

/* Build freedreno dmabuf client */
/* #undef HAVE_LIBDRM_FREEDRENO */

/* Build intel dmabuf client */
/* #undef HAVE_LIBDRM_INTEL */

/* Define to 1 if you have the <linux/sync_file.h> header file. */
/* #undef HAVE_LINUX_SYNC_FILE_H */

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `mkostemp' function. */
#define HAVE_MKOSTEMP 1

/* Have pango */
/* #undef HAVE_PANGO */

/* Define to 1 if you have the `posix_fallocate' function. */
/* #undef HAVE_POSIX_FALLOCATE */

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the `strchrnul' function. */
#define HAVE_STRCHRNUL 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* SURFACE_BITS_CMD has bmp field */
/* #undef HAVE_SURFACE_BITS_BMP */

/* Have systemdlogin */
/* #undef HAVE_SYSTEMD_LOGIN */

/* Have systemdlogin >= 209 */
/* #undef HAVE_SYSTEMD_LOGIN_209 */

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Have webp */
/* #undef HAVE_WEBP */

/* libxcb supports XKB protocol */
/* #undef HAVE_XCB_XKB */

/* Define if xkbcommon is 0.5.0 or newer */
/* #undef HAVE_XKBCOMMON_COMPOSE */

/* Define to the subdirectory where libtool stores uninstalled libraries. */
#define LT_OBJDIR ".libs/"

/* Define to 1 if `major', `minor', and `makedev' are declared in <mkdev.h>.
   */
/* #undef MAJOR_IN_MKDEV */

/* Define to 1 if `major', `minor', and `makedev' are declared in
   <sysmacros.h>. */
/* #undef MAJOR_IN_SYSMACROS */

/* Name of package */
#define PACKAGE "weston"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "https://gitlab.freedesktop.org/wayland/weston/issues/"

/* Define to the full name of this package. */
#define PACKAGE_NAME "weston"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "weston 5.0.0"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "weston"

/* Define to the home page for this package. */
#define PACKAGE_URL "https://wayland.freedesktop.org"

/* Define to the version of this package. */
#define PACKAGE_VERSION "5.0.0"

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Build the systemd sd_notify support */
/* #undef SYSTEMD_NOTIFY_SUPPORT */

/* Use the GLESv2 GL cairo backend */
/* #undef USE_CAIRO_GLESV2 */

/* Use resize memory pool as a performance optimization */
#define USE_RESIZE_POOL 1

#define MAJOR_IN_SYSMACROS 1

/* Enable extensions on AIX 3, Interix.  */
#ifndef _ALL_SOURCE
# define _ALL_SOURCE 1
#endif
/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
/* Enable threading extensions on Solaris.  */
#ifndef _POSIX_PTHREAD_SEMANTICS
# define _POSIX_PTHREAD_SEMANTICS 1
#endif
/* Enable extensions on HP NonStop.  */
#ifndef _TANDEM_SOURCE
# define _TANDEM_SOURCE 1
#endif
/* Enable general extensions on Solaris.  */
#ifndef __EXTENSIONS__
# define __EXTENSIONS__ 1
#endif


/* Version number of package */
#define VERSION "9.0.0"

/* The default backend to load, if not wayland nor x11. */
#define WESTON_NATIVE_BACKEND "drm-backend.so"

/* The default desktop shell client to load. */
#define WESTON_SHELL_CLIENT "weston-desktop-shell"

#define BINDIR "/system/bin"

#define LIBEXECDIR "/system/bin"

#define DATADIR "data"
#ifdef __aarch64__
#define LIBWESTON_MODULEDIR "/system/lib64"
#define MODULEDIR "/system/lib64"
#else
#define LIBWESTON_MODULEDIR "/system/lib"
#define MODULEDIR "/system/lib"
#endif

/* Enable large inode numbers on Mac OS X 10.5.  */
#ifndef _DARWIN_USE_64_BIT_INODE
# define _DARWIN_USE_64_BIT_INODE 1
#endif

/* Number of bits in a file offset, on hosts where this is settable. */
/* #undef _FILE_OFFSET_BITS */

/* Define for large files, on AIXstyle hosts. */
/* #undef _LARGE_FILES */

/* Define to 1 if on MINIX. */
/* #undef _MINIX */

/* Define to 2 if the system does not provide POSIX.1 features except with
   this defined. */
/* #undef _POSIX_1_SOURCE */

/* Define to 1 if you need to in order for `stat' and other things to work. */
/* #undef _POSIX_SOURCE */

// #define BUILD_DRM_GBM 1
