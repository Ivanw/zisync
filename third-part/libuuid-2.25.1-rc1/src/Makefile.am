lib_LTLIBRARIES = libuuid.la
uuid_includedir = $(includedir)/uuid
uuid_include_HEADERS = uuid.h

libuuid_la_SOURCES = uuid.h uuidP.h uuidd.h clear.c \
compare.c copy.c gen_uuid.c isnull.c pack.c parse.c \
unpack.c unparse.c uuid_time.c randutils.c

libuuid_la_DEPENDENCIES = uuid.sym

libuuid_la_LDFLAGS = \
	-Wl,--version-script=uuid.sym 
