// Android compatibility header for MDBX
// Provides missing functions that are not available in Android

#ifndef ANDROID_COMPAT_H
#define ANDROID_COMPAT_H

#if defined(__ANDROID__) && !defined(_GNU_SOURCE)

// Only define if not already defined
#ifndef _MNTENT_H
#include <stdio.h>
#include <string.h>
#include <errno.h>

// Android-compatible mntent structure (only if not already defined)
struct mntent {
    char *mnt_fsname;   // Device name
    char *mnt_dir;      // Mount point
    char *mnt_type;     // File system type
    char *mnt_opts;     // Mount options
    int mnt_freq;       // Dump frequency
    int mnt_passno;     // Pass number for fsck
};

// Android-compatible setmntent/endmntent implementation
static inline FILE* setmntent(const char* filename, const char* type) {
    // For Android, return NULL to disable mount detection
    (void)filename; (void)type;
    return NULL;
}

static inline int endmntent(FILE* fp) {
    // Safe no-op for Android
    (void)fp;
    return 1;
}

static inline struct mntent* getmntent(FILE* fp) {
    // Return NULL to indicate no mount entries on Android
    (void)fp;
    return NULL;
}

#endif // _MNTENT_H

#endif // __ANDROID__ && !_GNU_SOURCE

#endif // ANDROID_COMPAT_H
