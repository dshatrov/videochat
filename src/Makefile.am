COMMON_CFLAGS =				\
	-D_POSIX_C_SOURCE=199309L	\
	-D_XOPEN_SOURCE=600		\
	-ggdb -pedantic			\
	-Wno-long-long -Wall -Wextra	\
	$(THIS_CFLAGS)

if PLATFORM_WIN32
    COMMON_CFLAGS += -DPLATFORM_WIN32
else
    COMMON_CFLAGS += -rdynamic -pthread
endif

AM_CFLAGS = -std=c99 $(COMMON_CFLAGS)
AM_CXXFLAGS = -std=c++0x $(COMMON_CFLAGS)

if PLATFORM_WIN32
else
# <fstream> doesn't work in strict ANSI mode with MinGW.
#AM_CXXFLAGS += -std=c++98 -ansi
endif

bin_PROGRAMS = videochatd

videochatd_private_headers =

videochatd_SOURCES =	\
	main.cpp
videochatd_LDADD = $(THIS_LIBS)
videochatd_LDFLAGS = -Wl,-rpath,'$(libdir)'

EXTRA_DIST = $(videchatd_private_headers)

