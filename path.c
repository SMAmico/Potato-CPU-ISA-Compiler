// Copyright 2014 Rui Ueyama. Released under the MIT license.

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include "8cc.h"

// Returns the shortest path for the given full path to a file.
static char *clean(char *p) {
    // Normalize Windows backslashes to forward slashes
    for (char *s = p; *s; s++)
        if (*s == '\\')
            *s = '/';

    char buf[PATH_MAX];
    char *q = buf;

    if (*p == '/') {
        *q++ = '/';
        p++;
    } else if (p[1] == ':' && p[2] == '/') {
        *q++ = p[0];
        *q++ = ':';
        *q++ = '/';
        p += 3;
    } else {
        assert(*p == '/');
    }

    for (;;) {
        if (*p == '/') {
            p++;
            continue;
        }
        if (!memcmp("./", p, 2)) {
            p += 2;
            continue;
        }
        if (!memcmp("../", p, 3)) {
            p += 3;
            if (q == buf + 1)
                continue;
            for (q--; q[-1] != '/'; q--);
            continue;
        }
        while (*p != '/' && *p != '\0')
            *q++ = *p++;
        if (*p == '/') {
            *q++ = *p++;
            continue;
        }
        *q = '\0';
        return strdup(buf);
    }
}

// Returns the shortest absolute path for the given path.
char *fullpath(char *path) {
    static char cwd[PATH_MAX];
    if (path[0] == '/')
        return clean(path);
    if (*cwd == '\0' && !getcwd(cwd, PATH_MAX))
        error("getcwd failed: %s", strerror(errno));
    return clean(format("%s/%s", cwd, path));
}
