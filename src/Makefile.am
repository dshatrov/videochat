COMMON_CFLAGS =				\
	-ggdb				\
	-Wno-long-long -Wall -Wextra	\
	$(THIS_CFLAGS)

if PLATFORM_WIN32
    COMMON_CFLAGS += -DPLATFORM_WIN32
else
    COMMON_CFLAGS += -rdynamic -pthread
endif

AM_CFLAGS = -std=gnu99 $(COMMON_CFLAGS)
AM_CXXFLAGS = -std=gnu++0x $(COMMON_CFLAGS)

bin_PROGRAMS = videochatd

videochatd_private_headers =

videochatd_SOURCES =	\
	main.cpp
videochatd_LDADD = $(THIS_LIBS)
videochatd_LDFLAGS = -Wl,-rpath,'$(libdir)'

EXTRA_DIST = $(videchatd_private_headers)

