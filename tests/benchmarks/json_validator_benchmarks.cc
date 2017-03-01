//
// Created by Oliver Downard on 24/02/2017.
//

#include <benchmark/benchmark.h>
#include <JSON_checker.h>
#include <subdoc/util.h>
#include <subdoc/validate.h>
#include <random>
#include <algorithm>
#include <rapidjson/document.h>

class JSON_FIXTURE : public benchmark::Fixture{
public:
    void SetUp(const benchmark::State& state) override {
        //generateUniformJson(state.range_y(), 40);
        generateUniformJson(state.range_y(), 40);
    }

    void generateUniformJson(int depth, int length){
        jsonstring = "{";
        for (int i = 0; i < length; ++i){
            jsonstring += "\"" + std::to_string(i) + "\": {";
            for (int j = 0; j < depth; ++j){
                jsonstring += "\"" + std::to_string(j) + "\": {";
            }
            jsonstring += "\"Egg\": 56";
            for (int j = 0; j < depth; ++j){
                jsonstring += "}";
            }
            jsonstring += i == length - 1 ? "}" : "},";
        }
        jsonstring += "}";
    }

    void generateBottomHeavyJSON(int uniformDepth, int maxDepth, int length){
        jsonstring = "{";
        for (int i = 0; i < length - 1; ++i){
            jsonstring += "\"" + std::to_string(i) + "\": {";
            for (int j = 0; j < uniformDepth; ++j){
                jsonstring += "\"" + std::to_string(j) + "\": {";
            }
            jsonstring += "\"Egg\": 56";
            for (int j = 0; j < uniformDepth; ++j){
                jsonstring += "}";
            }
            jsonstring += i == length - 1 ? "}" : "},";
        }
        jsonstring += "\"" + std::to_string(length) + "\": {";
        for (int j = 0; j < maxDepth; ++j){
            jsonstring += "\"" + std::to_string(j) + "\": {";
        }
        jsonstring += "\"Egg\": 56";
        for (int j = 0; j < maxDepth; ++j){
            jsonstring += "}";
        }
        jsonstring += "}}";
    }

    template <typename Distribution>
    void generateJsonFromDist(Distribution distribution){
    }

    std::string jsonstring;
};

static void jsonslArgumentGenerator(benchmark::internal::Benchmark* b){
    //X is the max depth the validator can take
    //Y is the depth of the json doc passed
    for (int i = 1; i <= 1024; i *= 2){
        for (int j = 1; j <= 1024; j *= 2){
            b->ArgPair(i,j);
        }
    }
}

BENCHMARK_DEFINE_F(JSON_FIXTURE, JSON_CHECKER_BASIC)(benchmark::State& state){
    const unsigned char* data = (const unsigned char*) jsonstring.c_str();
    while (state.KeepRunning()){
        if(!checkUTF8JSON(data, jsonstring.size())){
            state.SetLabel("Error");
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(JSON_FIXTURE, JSON_CHECKER_BASIC)->Apply
        (jsonslArgumentGenerator);

//BENCHMARK_DEFINE_F(JSON_FIXTURE, JSONSL_BASIC)(benchmark::State& state){
//    jsonsl_t jsonsl = jsonsl_new(state.range_x() + 4);
//    while (state.KeepRunning()){
//        if(Subdoc::Validator::validate(jsonstring, jsonsl) !=
//                JSONSL_ERROR_SUCCESS){
//            state.SetLabel("Error");
//        }
//    }
//    state.SetItemsProcessed(state.iterations());
//}
//BENCHMARK_REGISTER_F(JSON_FIXTURE, JSONSL_BASIC)->Apply
//        (jsonslArgumentGenerator);

BENCHMARK_DEFINE_F(JSON_FIXTURE, COMBI)(benchmark::State& state){
    jsonsl_t jsonsl = jsonsl_new(state.range_x());
    while (state.KeepRunning()){
        if(Subdoc::Validator::validate(jsonstring, jsonsl) == JSONSL_ERROR_LEVELS_EXCEEDED){
            state.SetLabel("Switch");
            const unsigned char* data = (const unsigned char*) jsonstring.c_str();
            checkUTF8JSON(data, jsonstring.size());
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(JSON_FIXTURE, COMBI)->Apply(jsonslArgumentGenerator);

BENCHMARK_DEFINE_F(JSON_FIXTURE, RAPID)(benchmark::State& state){
    while (state.KeepRunning()){
        rapidjson::Document doc;
        doc.Parse(jsonstring.data());
        if(doc.GetParseError() != rapidjson::ParseErrorCode::kParseErrorNone){
            state.SetLabel("Error");
        }
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(JSON_FIXTURE, RAPID)->Apply(jsonslArgumentGenerator);

int main(int argc, char** argv) {
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
    return 0;
}