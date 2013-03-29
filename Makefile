CXXFLAGS += -Wall -W -g -O2
CPPFLAGS += -Ilibcuefile/include
LDFLAGS += -L/usr/local/lib
LIBS += -lFLAC -lFLAC++ -lboost_program_options-mt -licuuc -lsox

OBJS = \
	decode.o \
	encode.o \
	errors.o \
	main.o \
	replaygain.o \
	sanitize.o \
	transcode.o \
	libcuefile.a \
	#

all: recursive-all flacsplit

recursive-all:
	@if [ ! -f libcuefile/Makefile ]; then (cd libcuefile && cmake .); fi
	@make -C libcuefile

libcuefile.a: recursive-all
	ln -sf libcuefile/src/libcuefile.a

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
	sanitize.hpp \
	transcode.hpp

replaygain.o: replaygain.cpp \
	replaygain.hpp \
	errors.hpp 

sanitize.o: sanitize.cpp \
	sanitize.hpp

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
