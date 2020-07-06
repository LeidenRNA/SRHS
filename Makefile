all: rna

RNA_OPTIONS=-DJS_NONE=0 -DJS_TORQUE=1 -DJS_SLURM=2 -DJS_JOBSCHED_TYPE=JS_TORQUE

# USE CLANG
CC=clang
OPTIMIZATION_FLAGS=-Ofast -funsafe-math-optimizations -freciprocal-math -ftree-vectorize -flto -fshort-enums -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free -fno-builtin-memset -march=native # -fno-builtin-* required for 3rd party memory allocator (jemalloc)
#OPTIMIZATION_FLAGS=-fsanitize=address -fsanitize-recover=all -fno-omit-frame-pointer -fno-builtin-memset -O1 -g  # google sanitizers need standard (glibc) allocator - comment out -ljemalloc in $LIBRARIES

# USE GCC
#CC=gcc
#OPTIMIZATION_FLAGS=-Ofast -funsafe-math-optimizations -fsingle-precision-constant -freciprocal-math -ftree-vectorize -flto -fshort-enums -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free
# -march=native # don't compile to native architecture - worker nodes might have different archs
#OPTIMIZATION_FLAGS=-O0 -g

# compiler options common to gcc and clang
COMPILER_OPTIONS=-Wall --std=gnu11
INCLUDE_PATHS=-I/usr/lib/x86_64-linux-gnu/openmpi/include/ -I/usr/local/include/libmongoc-1.0 -I/usr/local/include/libbson-1.0 \
	      -I$(RNA_ROOT)	 # required for test case include
LIBRARY_PATHS=-L/usr/local/lib/  # required for libjemalloc

LIBRARIES=-lrt -lm -lmongoc-1.0 -lbson-1.0 -lmicrohttpd -lorcania -lulfius -ljansson -ltorque -lmpi -lcrypto -pthread -ljemalloc

OBJECTS=build/m_list.o build/m_analyse.o build/m_optimize.o build/m_seq_bp.o build/m_build.o build/m_search.o \
	build/sequence.o build/simclist.o build/crc32.o build/util.o build/tests.o build/interface.o \
	build/mfe.o build/filter.o build/datastore.o \
	build/binn.o build/allocate.o \
	build/frontend.o build/distribute.o \
	build/c_jobsched_server.o build/c_jobsched_client.o \
	build/rna.o

LINK_TARGET=rna

build/m_list.o:	    	    src/m_list.c src/m_list.h
build/m_analyse.o:	    src/m_analyse.c src/m_analyse.h
build/m_optimize.o:	    src/m_optimize.c src/m_optimize.h
build/m_build.o:	    src/m_build.c src/m_build.h
build/m_seq_bp.o:	    src/m_seq_bp.c src/m_seq_bp.h
build/m_search.o:	    src/m_search.c src/m_search.h
build/sequence.o:           src/sequence.c src/sequence.h src/m_model.h src/util.h
build/simclist.o:           src/simclist.c src/simclist.h
build/crc32.o:              src/crc32.c src/crc32.h
build/util.o:               src/util.c src/util.h src/simclist.h
build/tests.o:              src/tests.c src/m_model.h src/simclist.h src/mfe.h tests.out
build/interface.o:          src/interface.c src/interface.h src/m_model.h src/util.h
build/mfe.o:                src/mfe.c src/mfe.h
build/filter.o:             src/filter.c src/filter.h src/util.h src/distribute.h src/sequence.h
build/datastore.o:          src/datastore.c src/datastore.h src/util.h src/jsmn.h
build/distribute.o:         src/distribute.c src/distribute.h src/filter.h src/datastore.h src/allocate.h src/interface.h src/c_jobsched_server.h
build/frontend.o:           src/frontend.c src/frontend.h src/filter.h src/datastore.h src/m_model.h src/interface.h src/util.h
build/c_jobsched_server.o:  src/c_jobsched_server.c src/c_jobsched_server.h src/binn.h src/rna.h
build/c_jobsched_client.o:  src/c_jobsched_client.c src/c_jobsched_client.h src/c_jobsched_server.h src/binn.h
build/binn.o:               src/binn.c src/binn.h
build/allocate.o:           src/allocate.c src/allocate.h src/c_jobsched_client.h
build/rna.o:                src/rna.c src/rna.h src/m_model.h src/util.h src/simclist.h src/tests.h src/interface.h src/mfe.h src/filter.h src/datastore.h src/distribute.h src/frontend.h src/ketopt.h

$(OBJECTS):
	$(CC) $(COMPILER_OPTIONS) $(OPTIMIZATION_FLAGS) $(RNA_OPTIONS) $(INCLUDE_PATHS) -c $< -o $@

$(LINK_TARGET): $(OBJECTS)
	$(CC) $(OPTIMIZATION_FLAGS) $(LIBRARY_PATHS) $(OBJECTS) -o $(LINK_TARGET) $(LIBRARIES)
	sudo setcap cap_net_bind_service=ep $(LINK_TARGET)  # uncomment to allow rna to bind to ports < 1024; note that this conflicts with google address sanitizer

clean: 
	rm -f $(OBJECTS) $(LINK_TARGET)
