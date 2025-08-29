// Android compatibility header for MDBX
// Provides missing functions that are not available in Android

#ifndef ANDROID_COMPAT_H
#define ANDROID_COMPAT_H

#ifdef __ANDROID__

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

// Android-compatible mntent structure
struct mntent {
    char *mnt_fsname;   // Device name
    char *mnt_dir;      // Mount point
    char *mnt_type;     // File system type
    char *mnt_opts;     // Mount options
    int mnt_freq;       // Dump frequency
    int mnt_passno;     // Pass number for fsck
};

// Android-compatible setmntent/endmntent implementation
static FILE* android_setmntent(const char* filename, const char* type) {
    // For Android, we'll return a dummy FILE* that won't be used
    // since mount detection is not critical for mobile apps
    return fopen("/dev/null", "r");
}

static int android_endmntent(FILE* fp) {
    if (fp) {
        return fclose(fp);
    }
    return 1;
}

static struct mntent* android_getmntent(FILE* fp) {
    // Return NULL to indicate no mount entries
    // This effectively disables mount detection on Android
    return NULL;
}

// Override the functions
#define setmntent android_setmntent
#define endmntent android_endmntent  
#define getmntent android_getmntent

#endif // __ANDROID__

#endif // ANDROID_COMPAT_H
