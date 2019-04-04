add_library(iobuf SHARED
  iobuf.c
)


set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ALL_CFLAGS} ${PTHREAD_CFLAGS} ${OPENSSL_CFLAGS}")

find_library(MEMMGR_LIBRARY
  NAMES libmemmgr memmgr
)

target_link_libraries(iobuf
	xplmem${BIT}
)

install(TARGETS iobuf DESTINATION /usr/lib)
