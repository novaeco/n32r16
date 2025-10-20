idf_component_register(SRCS "proto/test_proto.c" "util/test_ringbuf.c"
                      INCLUDE_DIRS "proto" "../common/util/include"
                      REQUIRES unity common_proto cjson common_util)
