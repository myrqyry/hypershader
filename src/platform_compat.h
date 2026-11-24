#ifndef HYPERSHADER_PLATFORM_COMPAT_H
#define HYPERSHADER_PLATFORM_COMPAT_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>

#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif

#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif

#define hs_strcasecmp _stricmp
#define hs_strtok_r strtok_s

static inline char *hs_realpath(const char *path, char *resolved)
{
	if (!path || !resolved || _access(path, 0) != 0)
		return NULL;
	return _fullpath(resolved, path, PATH_MAX);
}

#else
#include <limits.h>
#include <strings.h>
#include <unistd.h>

#define hs_strcasecmp strcasecmp
#define hs_strtok_r strtok_r

static inline char *hs_realpath(const char *path, char *resolved)
{
	return realpath(path, resolved);
}
#endif

#endif
