#if !defined(_REAL_H)
#define _REAL_H

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <netdb.h>
#include <locale.h>
#include <execinfo.h>

#define DECLARE_WRAPPER(name) extern decltype(::name) * name;

namespace Real {
void initializer();

DECLARE_WRAPPER(free);
DECLARE_WRAPPER(malloc);
DECLARE_WRAPPER(malloc_usable_size);
DECLARE_WRAPPER(memalign);

DECLARE_WRAPPER(pthread_create);

DECLARE_WRAPPER(fork);

DECLARE_WRAPPER(fopen);
DECLARE_WRAPPER(fopen64);
DECLARE_WRAPPER(fread);
DECLARE_WRAPPER(open);
DECLARE_WRAPPER(open64);
DECLARE_WRAPPER(gethostbyname);
DECLARE_WRAPPER(getservbyname);
DECLARE_WRAPPER(setlocale);
DECLARE_WRAPPER(unlink);
DECLARE_WRAPPER(backtrace);
};

#endif
