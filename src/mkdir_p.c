/* SPDX-License-Identifier: CC0-1.0 */
#include "mkdir_p.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>

static int ensure_dir(const char *path) {
  if (mkdir(path, 0777) == 0)
    return 0;
  if (errno != EEXIST)
    return -1;
  struct stat st;
  if (stat(path, &st) != 0)
    return -1;
  if (!S_ISDIR(st.st_mode)) {
    errno = ENOTDIR;
    return -1;
  }
  return 0;
}

int mkdir_p(const char *path) {
  if (path == nullptr || path[0] == '\0') {
    errno = EINVAL;
    return -1;
  }

  /* Fast path: parent already exists or directory already exists. */
  if (mkdir(path, 0777) == 0)
    return 0;
  if (errno == EEXIST) {
    struct stat st;
    if (stat(path, &st) != 0)
      return -1;
    if (S_ISDIR(st.st_mode))
      return 0;
    errno = ENOTDIR;
    return -1;
  }
  if (errno != ENOENT)
    return -1;

  const size_t len = strlen(path);
  char _path[PATH_MAX];
  char *p;

  errno = 0;

  if (len > sizeof(_path) - 1) {
    errno = ENAMETOOLONG;
    return -1;
  }
  memcpy(_path, path, len + 1);

  for (p = _path + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (ensure_dir(_path) != 0)
        return -1;
      *p = '/';
    }
  }

  return ensure_dir(_path);
}
