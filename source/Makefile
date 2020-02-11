SRCS = libcauser.cpp \
       real.cpp \
       causer.cpp \
       watchpoint.cpp \
       xthread.cpp

INCS = real.hh \
       causer.hh \
       watchpoint.hh

DEPS = $(SRCS) $(INCS)

#CC = gcc
#CXX = g++ 
CC = /home/hongyuliu/workspace/clang-3.8/bin/clang
CXX = /home/hongyuliu/workspace/clang-3.8/bin/clang++ 

# the default one is detecting buffer overflow
CFLAGS = -O2 -g -Wall --std=c++11 -fno-omit-frame-pointer -DNDEBUG -DCATCH_SEGV -DNCUSTOMIZED_REPORT -DENABLE_DLADDR_INFO -DPREEMPT_REPLACEMENT -DNRANDOM_SEARCH_WP -DINIT_META_MAPPING -DENABLE_EVIDENCE -DENABLE_EVIDENCE_SCAN_MEMORY
# -Wno-unused-private-field
#-DNSTATISTICS  
CFLAGS1 = -O2 -Wall -fno-omit-frame-pointer -fPIC

LIBS = -lpthread -ldl

INCLUDE_DIRS =

RANDOBJS = obj/arc4random.o obj/arc4random_uniform.o
obj/%.o: %.c
	mkdir -p obj
	$(CC) $(CFLAGS1) -c $< -o $@

TARGETS = libcauser.so

all: $(TARGETS)

$(TARGETS): $(DEPS) $(RANDOBJS)
	$(CXX) $(CFLAGS) $(INCLUDE_DIRS) -shared -fPIC $(SRCS) $(RANDOBJS) -o $(TARGETS) $(LIBS) 

clean:
	rm -f $(TARGETS) $(RANDOBJS)
