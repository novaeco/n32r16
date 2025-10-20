set(_requires cjson common_util)
if(CONFIG_USE_CBOR)
    list(APPEND _requires tinycbor)
endif()

idf_component_register(SRCS "proto_codec.c" "proto_crc32.c"
                      INCLUDE_DIRS "include"
                      REQUIRES ${_requires})
