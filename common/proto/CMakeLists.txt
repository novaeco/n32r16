idf_component_register(SRCS "messages.c" "proto_crc32.c"
                      INCLUDE_DIRS "."
                      REQUIRES cjson)
if(CONFIG_USE_CBOR)
    idf_component_get_property(TINYCBOR_DIR tinycbor COMPONENT_DIR)
    if(NOT TINYCBOR_DIR)
        message(FATAL_ERROR "CONFIG_USE_CBOR is enabled but tinycbor component is missing")
    endif()
    idf_component_optional_requires(PRIVATE tinycbor)
endif()
