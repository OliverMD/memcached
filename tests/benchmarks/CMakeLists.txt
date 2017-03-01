INCLUDE_DIRECTORIES(AFTER ${benchmark_source_dir}/include)
ADD_EXECUTABLE(json_validator_benchmarks json_validator_benchmarks.cc
        ${subjson_SOURCE_DIR}/contrib/jsonsl/jsonsl.c)
TARGET_LINK_LIBRARIES(json_validator_benchmarks benchmark JSON_checker
        platform subjson)