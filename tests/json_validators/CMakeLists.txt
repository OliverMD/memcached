ADD_EXECUTABLE(json_validators json_validators.cc ${subjson_SOURCE_DIR}/contrib/jsonsl/jsonsl.c)
TARGET_LINK_LIBRARIES(json_validators platform dirutils JSON_checker subjson)