// Android compatibility header for MDBX
// Provides missing functions that are not available in Android

#ifndef ANDROID_COMPAT_H
#define ANDROID_COMPAT_H

// Enable stubs for old Android API levels where mntent functions are unavailable
#if defined(__ANDROID__) && (!defined(__ANDROID_API__) || (__ANDROID_API__ < 26))

// Predefine common mntent header guards to prevent redefinition from system <mntent.h>
#ifndef _MNTENT_H
#define _MNTENT_H 1
#endif
#ifndef _MNTENT_H_
#define _MNTENT_H_ 1
#endif

#ifndef ANDROID_MNTENT_STUBS
#define ANDROID_MNTENT_STUBS 1

#include <stdio.h>
#include <string.h>
#include <errno.h>

// Android-compatible mntent structure and stub functions
struct mntent {
    char *mnt_fsname;   // Device name
    char *mnt_dir;      // Mount point
    char *mnt_type;     // File system type
    char *mnt_opts;     // Mount options
    int mnt_freq;       // Dump frequency
    int mnt_passno;     // Pass number for fsck
};

static inline FILE* setmntent(const char* filename, const char* type) {
    (void)filename; (void)type;
    return NULL;
}

static inline int endmntent(FILE* fp) {
    (void)fp;
    return 1;
}

static inline struct mntent* getmntent(FILE* fp) {
    (void)fp;
    return NULL;
}

#endif // ANDROID_MNTENT_STUBS

#endif // __ANDROID__ && (API < 26)

#endif // ANDROID_COMPAT_H
