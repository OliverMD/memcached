/*
 *     Copyright 2016 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */
#include "plain.h"

#include "cbsasl/pwfile.h"
#include "check_password.h"

#include <cstring>

#ifdef WIN32
#include <stdexcept>
static cbsasl_error_t check(cbsasl_conn_t* conn,
                            const std::string& username,
                            const std::string &passwd) {
    throw std::runtime_error("check: saslauthd is not supported on Windows");
}
#else
#include "saslauthd.h"
static cbsasl_error_t check(cbsasl_conn_t* conn,
                            const std::string& username,
                            const std::string &passwd) {
    try {
        static const char* socketfile = getenv("CBAUTH_SOCKPATH");
        Saslauthd saslauthd(socketfile);
        return saslauthd.check(username, passwd);
    } catch (const std::exception& e) {
        cbsasl_log(conn, cbsasl_loglevel_t::Error,
                   "Failed to validate [" + username + "] through saslauthd: " +
                   e.what());
        return CBSASL_FAIL;
    }
}
#endif

cbsasl_error_t PlainServerBackend::start(cbsasl_conn_t* conn,
                                         const char* input,
                                         unsigned inputlen,
                                         const char** output,
                                         unsigned* outputlen) {
    if (inputlen == 0 || input == nullptr) {
        return CBSASL_BADPARAM;
    }

    if (output != nullptr) {
        *output = nullptr;
    }

    if (outputlen != nullptr) {
        *outputlen = 0;
    }

    size_t inputpos = 0;
    while (inputpos < inputlen && input[inputpos] != '\0') {
        inputpos++;
    }
    inputpos++;

    if (inputpos >= inputlen) {
        return CBSASL_BADPARAM;
    }

    size_t pwlen = 0;
    const char* username = input + inputpos;
    const char* password = nullptr;
    while (inputpos < inputlen && input[inputpos] != '\0') {
        inputpos++;
    }
    inputpos++;

    if (inputpos > inputlen) {
        return CBSASL_BADPARAM;
    } else if (inputpos != inputlen) {
        password = input + inputpos;
        while (inputpos < inputlen && input[inputpos] != '\0') {
            inputpos++;
            pwlen++;
        }
    }

    conn->server->username.assign(username);
    const std::string userpw(password, pwlen);
    cb::sasl::User user;
    if (!find_user(username, user)) {
        if (cbsasl_use_saslauthd()) {
            auto ret = check(conn, username, userpw);
            if (ret == CBSASL_OK) {
                conn->server->domain = cb::sasl::Domain::Saslauthd;
            }
            return ret;
        }

        return CBSASL_NOUSER;
    }

    return cb::sasl::plain::check_password(user, userpw);
}

cbsasl_error_t PlainClientBackend::start(cbsasl_conn_t* conn, const char* input,
                                         unsigned inputlen, const char** output,
                                         unsigned* outputlen) {

    auto* client = conn->client.get();

    const char* usernm = nullptr;
    unsigned int usernmlen;

    if (cbsasl_get_username(client->get_username, client->get_username_ctx,
                            &usernm, &usernmlen) != CBSASL_OK) {
        return CBSASL_FAIL;
    }

    cbsasl_secret_t* pass;
    if (cbsasl_get_password(client->get_password, conn,
                            client->get_password_ctx, &pass) != CBSASL_OK) {
        return CBSASL_FAIL;
    }

    try {
        buffer.resize(usernmlen + 1 + pass->len + 1);
    } catch (std::bad_alloc&) {
        return CBSASL_NOMEM;
    }

    std::memcpy(buffer.data() + 1, usernm, usernmlen);
    std::memcpy(buffer.data() + usernmlen + 2, pass->data, pass->len);

    *output = buffer.data();
    *outputlen = unsigned(buffer.size());
    return CBSASL_OK;
}
