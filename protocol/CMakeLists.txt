IF (MEMCACHED_USE_GREENSTACK)
    ADD_SUBDIRECTORY(Greenstack)
ENDIF (MEMCACHED_USE_GREENSTACK)
ADD_SUBDIRECTORY(mcbp)
ADD_SUBDIRECTORY(connection)
