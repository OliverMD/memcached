//
// Created by Oliver Downard on 28/02/2017.
//

#include <iostream>
#include <JSON_checker.h>
#include <subdoc/util.h>
#include <subdoc/validate.h>
#include <platform/dirutils.h>
#include <fstream>
#include <rapidjson/document.h>
#include <subdoc/jsonsl_header.h>

enum class Case {
    Pass,
    Fail
};

std::string to_string(Case casetype){
    return casetype == Case::Pass ? "Pass" : "Fail";
}

using ValidatorFunc = bool (*)(const std::string& json);

static int testCases(std::vector<std::string> testcases, Case casetype,
                     ValidatorFunc func){
    for (const auto& file : testcases){
        std::ifstream stream(file, std::ios::binary | std::ios::ate);
        if (!stream.is_open()){
            std::cout << "Failed to open " + file << std::endl;
            return 1;
        }
        auto size = stream.tellg();
        std::string str(size, '\0');
        stream.seekg(0);
        stream.read(&str[0], size);
        bool valid = func(str);
        if ((valid && casetype == Case::Fail)
            || (!valid && casetype == Case::Pass)){
            std::cout << file << ": INCORRECT - Should " << to_string(casetype)
                      << " - " << str << "\n";
        } else {
            std::cout << file << ": CORRECT \n";
        }
    }
    return 0;
}

bool JSON_checker_validator(const std::string& json){
    return checkUTF8JSON((const unsigned char*) json.data(), json.size());
}
typedef struct {
    int err;
    int rootcount;
    int flags;
    int maxdepth;
} validate_ctx;

static int
validate_err_callback(jsonsl_t jsn,
                      jsonsl_error_t err, jsonsl_state_st *, jsonsl_char_t *)
{
    validate_ctx *ctx = (validate_ctx *)jsn->data;
    ctx->err = err;
    //printf("ERROR: AT=%s\n", jsn->base);
    return 0;
}

bool JSONSL_validator(const std::string& json){
    jsonsl_t jsonsl = jsonsl_new(32);
    return Subdoc::Validator::validate(json, jsonsl, -1,
                                Subdoc::Validator::Options::PARENT_ARRAY |
                                        Subdoc::Validator::Options::VALUE_SINGLE) == JSONSL_ERROR_SUCCESS;
}

bool RapidJSON_validator(const std::string& json){
    rapidjson::Document doc;
    doc.Parse(json.data());
    return doc.GetParseError() == rapidjson::ParseErrorCode::kParseErrorNone;
}

static void runTest(std::vector<std::string> passFiles,
                    std::vector<std::string> failFiles, ValidatorFunc func,
                    std::string name) {
    std::cout << "Testing " << name << ":\n";

    std::cout << "Testing pass cases: " + std::to_string(passFiles.size()) +
                 " cases found" << std::endl;
    testCases(passFiles, Case::Pass, func);

    std::cout << "Testing fail cases: " + std::to_string(failFiles.size()) +
                 " cases found" << std::endl;
    testCases(failFiles, Case::Fail, func);


}

int main(int argc, char **argv){
    if (argc < 2){
        std::cout << "Usage: json_validators path_to_tests" << std::endl;
        return 1;
    }

    std::string basePath = argv[1];
    auto passFiles = cb::io::findFilesWithPrefix(basePath, "pass");
    auto failFiles = cb::io::findFilesWithPrefix(basePath, "fail");

    runTest(passFiles, failFiles, JSON_checker_validator, "JSON_checker");

    runTest(passFiles, failFiles, JSONSL_validator, "JSONSL");

    //runTest(passFiles, failFiles, RapidJSON_validator, "RapidJSON");

    return 0;
}