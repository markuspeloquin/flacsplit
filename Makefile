CXXFLAGS += -Wall -W -g -O2
CPPFLAGS += -Ilibcuefile/include
LDFLAGS +=
LIBS += -lFLAC++ -lboost_program_options -licuuc -lsox

OBJS = \
       decode.o \
       encode.o \
       errors.o \
       main.o \
       replaygain.o \
       transcode.o \
       libcuefile.a \
#

all: recursive-all flacsplit

recursive-all:
	@if [ ! -f libcuefile/Makefile ]; then (cd libcuefile && cmake .); fi
	@make -C libcuefile

libcuefile.a:
	ln -s libcuefile/src/libcuefile.a

flacsplit: $(OBJS)
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@

decode.o: decode.cpp \
	decode.hpp \
	errors.hpp \
	transcode.hpp

encode.o: encode.cpp \
	encode.hpp \
	errors.hpp \
	transcode.hpp

errors.o: errors.cpp \
	errors.hpp

main.o: main.cpp \
	decode.hpp \
	encode.hpp \
    	errors.hpp \
	replaygain.hpp \
	transcode.hpp

replaygain.o: replaygain.cpp \
	replaygain.hpp \
	errors.hpp 

transcode.o: transcode.cpp \
	transcode.hpp

clean:
	rm -f $(OBJS) flacsplit

distclean: clean
	@if [ -f libcuefile/Makefile ]; then make clean -C libcuefile; fi
	find libcuefile \
	-name CMakeFiles -o \
	-name CMakeCache.txt -o \
	-name Makefile -o \
	-name cmake_install.cmake | \
    xargs rm -rf
