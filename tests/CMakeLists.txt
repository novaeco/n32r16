idf_component_register(SRCS "proto/test_proto.c" "util/test_ringbuf.c" "security/test_command_auth.c"
                      INCLUDE_DIRS "proto" "security" "../common/util/include" "../common/security/include"
                      REQUIRES unity common_proto cjson common_util common_security)
