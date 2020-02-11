#include "real.hh"

#define DEFINE_WRAPPER(name) decltype(::name) * name;

#define INIT_WRAPPER(name, handle) name = (decltype(::name)*)dlsym(handle, #name);

namespace Real {

DEFINE_WRAPPER(free);
DEFINE_WRAPPER(malloc);
DEFINE_WRAPPER(malloc_usable_size);
DEFINE_WRAPPER(memalign);

DEFINE_WRAPPER(pthread_create);

DEFINE_WRAPPER(fork);

DEFINE_WRAPPER(fopen);
DEFINE_WRAPPER(fopen64);
DEFINE_WRAPPER(fread);
DEFINE_WRAPPER(open);
DEFINE_WRAPPER(open64);
DEFINE_WRAPPER(gethostbyname);
DEFINE_WRAPPER(getservbyname);
DEFINE_WRAPPER(setlocale);
DEFINE_WRAPPER(unlink);
DEFINE_WRAPPER(backtrace);

void initializer() {
  INIT_WRAPPER(free, RTLD_NEXT);
  INIT_WRAPPER(malloc, RTLD_NEXT);
  INIT_WRAPPER(malloc_usable_size, RTLD_NEXT);
  INIT_WRAPPER(memalign, RTLD_NEXT);

  INIT_WRAPPER(fork, RTLD_NEXT);

  INIT_WRAPPER(fopen, RTLD_NEXT);
  INIT_WRAPPER(fopen64, RTLD_NEXT);
  INIT_WRAPPER(fread, RTLD_NEXT);
  INIT_WRAPPER(open, RTLD_NEXT);
  INIT_WRAPPER(open64, RTLD_NEXT);
  INIT_WRAPPER(gethostbyname, RTLD_NEXT);
  INIT_WRAPPER(getservbyname, RTLD_NEXT);
  INIT_WRAPPER(setlocale, RTLD_NEXT);
  INIT_WRAPPER(unlink, RTLD_NEXT);
  INIT_WRAPPER(backtrace, RTLD_NEXT);

//  INIT_WRAPPER(pthread_create, RTLD_NEXT);
  void* pthread_handle = dlopen("libpthread.so.0", RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
  INIT_WRAPPER(pthread_create, pthread_handle);
}

}
