/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2017 Couchbase, Inc.
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

/**
 * This file contains tests related to RBAC
 */

#include "testapp.h"
#include "testapp_client_test.h"
#include <protocol/connection/client_greenstack_connection.h>
#include <protocol/connection/client_mcbp_connection.h>

#include <algorithm>
#include <platform/compress.h>

class RbacTest : public TestappClientTest {
public:
};

INSTANTIATE_TEST_CASE_P(TransportProtocols,
                        RbacTest,
                        ::testing::Values(TransportProtocols::McbpPlain),
                        ::testing::PrintToStringParamName());


/**
 * Memcached supports authentication through external systems (like LDAP),
 * but these users may not be defined as a user within Couchbase. Such
 * users should fail with an Access Denied error instead of Authentication
 * Success even if the username password combination is correct.
 */
TEST_P(RbacTest, DontAllowUnknownUsers) {
    auto& conn = getConnection();
    try {
        conn.authenticate("sharon", "sharonpw", "PLAIN");
        FAIL() << "Users without an RBAC profile should not be allowed access";
    } catch (const ConnectionError& error) {
        EXPECT_TRUE(error.isAccessDenied()) << error.what();
    }
}

TEST_P(RbacTest, ReloadRbacData_HaveAccess) {
    auto& conn = reinterpret_cast<MemcachedBinprotConnection&>(getConnection());
    conn.authenticate("_admin", "password", "PLAIN");
    BinprotGenericCommand cmd(PROTOCOL_BINARY_CMD_RBAC_REFRESH, {}, {});
    conn.sendCommand(cmd);

    BinprotResponse resp;
    conn.recvResponse(resp);
    EXPECT_TRUE(resp.isSuccess());
}

TEST_P(RbacTest, ReloadRbacData_NoAccess) {
    auto& conn = reinterpret_cast<MemcachedBinprotConnection&>(getConnection());
    conn.reconnect();
    conn.setXerrorSupport(true);
    BinprotGenericCommand cmd(PROTOCOL_BINARY_CMD_RBAC_REFRESH, {}, {});
    conn.sendCommand(cmd);

    BinprotResponse resp;
    conn.recvResponse(resp);
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EACCESS, resp.getStatus());
}

TEST_P(RbacTest, ReloadSasl_HaveAccess) {
    auto& conn = dynamic_cast<MemcachedBinprotConnection&>(getAdminConnection());
    BinprotGenericCommand cmd(PROTOCOL_BINARY_CMD_ISASL_REFRESH);
    BinprotResponse resp;

    conn.sendCommand(cmd);
    conn.recvResponse(resp);
    EXPECT_TRUE(resp.isSuccess());
}

TEST_P(RbacTest, ReloadSasl_NoAccess) {
    auto& conn = dynamic_cast<MemcachedBinprotConnection&>(getConnection());
    BinprotGenericCommand cmd(PROTOCOL_BINARY_CMD_ISASL_REFRESH);

    conn.sendCommand(cmd);
    BinprotResponse resp;
    conn.recvResponse(resp);
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EACCESS, resp.getStatus());
}

TEST_P(RbacTest, ScrubNoAccess) {
    auto& c = dynamic_cast<MemcachedBinprotConnection&>(getConnection());

    BinprotGenericCommand command(PROTOCOL_BINARY_CMD_SCRUB);
    BinprotResponse response;

    c.sendCommand(command);
    c.recvResponse(response);
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EACCESS, response.getStatus());
}

TEST_P(RbacTest, Scrub) {
    auto& c = dynamic_cast<MemcachedBinprotConnection&>(getAdminConnection());

    c.selectBucket("default");
    BinprotGenericCommand command(PROTOCOL_BINARY_CMD_SCRUB);
    BinprotResponse response;

    do {
        // Retry if scrubber is already running.
        c.sendCommand(command);
        c.recvResponse(response);
    } while (response.getStatus() == PROTOCOL_BINARY_RESPONSE_EBUSY);

    EXPECT_TRUE(response.isSuccess());
}

class RbacRoleTest : public TestappClientTest {
public:
    void SetUp() override {
        auto& conn = getAdminConnection();
        conn.createBucket("rbac_test", "", Greenstack::BucketType::Memcached);

        conn = getConnection();
        smith_holder = conn.clone();
        jones_holder = conn.clone();
        larry_holder = conn.clone();
    }

    void TearDown() override {
        smith_holder.reset();
        jones_holder.reset();
        larry_holder.reset();
        auto& conn = getAdminConnection();
        conn.deleteBucket("rbac_test");
    }

    MemcachedBinprotConnection& getROConnection() {
        auto* c = smith_holder.get();
        auto& smith = dynamic_cast<MemcachedBinprotConnection&>(*c);
        smith.authenticate("smith", "smithpassword", "PLAIN");
        return prepare(smith);
    }

    MemcachedBinprotConnection& getWOConnection() {
        auto* c = jones_holder.get();
        auto& jones = dynamic_cast<MemcachedBinprotConnection&>(*c);
        jones.authenticate("jones", "jonespassword", "PLAIN");
        return prepare(jones);
    }

    MemcachedBinprotConnection& getRWConnection() {
        auto* c = larry_holder.get();
        auto& jones = dynamic_cast<MemcachedBinprotConnection&>(*c);
        jones.authenticate("larry", "larrypassword", "PLAIN");
        return prepare(jones);
    }

protected:

    MutationInfo store(MemcachedBinprotConnection& conn,
                       Greenstack::mutation_type_t type) {
        Document document;
        document.info.cas = Greenstack::CAS::Wildcard;
        document.info.compression = Greenstack::Compression::None;
        document.info.datatype = Greenstack::Datatype::Json;
        document.info.flags = 0xcaffee;
        document.info.id = name;
        const std::string content = to_string(memcached_cfg, false);
        std::copy(content.begin(), content.end(),
                  std::back_inserter(document.value));
        return conn.mutate(document, 0, type);
    }

    BinprotResponse createXattr(MemcachedBinprotConnection& conn,
                                const std::string& key,
                                const std::string& value) {
        BinprotSubdocCommand cmd;
        cmd.setOp(PROTOCOL_BINARY_CMD_SUBDOC_DICT_UPSERT);
        cmd.setKey(name);
        cmd.setPath(key);
        cmd.setValue(value);
        cmd.setFlags(SUBDOC_FLAG_XATTR_PATH | SUBDOC_FLAG_MKDIR_P);

        conn.sendCommand(cmd);

        BinprotResponse resp;
        conn.recvResponse(resp);
        return resp;
    }

    BinprotResponse getXattr(MemcachedBinprotConnection& conn,
                             const std::string& key) {
        BinprotSubdocCommand cmd;
        cmd.setOp(PROTOCOL_BINARY_CMD_SUBDOC_GET);
        cmd.setKey(name);
        cmd.setPath(key);
        cmd.setFlags(SUBDOC_FLAG_XATTR_PATH);
        conn.sendCommand(cmd);

        BinprotResponse resp;
        conn.recvResponse(resp);
        return resp;
    }

    MemcachedBinprotConnection& prepare(MemcachedBinprotConnection& c) {
        c.setDatatypeSupport(true);
        c.setMutationSeqnoSupport(true);
        c.setXerrorSupport(true);
        c.setXattrSupport(true);
        c.selectBucket("rbac_test");
        return c;
    }

    std::unique_ptr<MemcachedConnection> smith_holder;
    std::unique_ptr<MemcachedConnection> jones_holder;
    std::unique_ptr<MemcachedConnection> larry_holder;

};

INSTANTIATE_TEST_CASE_P(TransportProtocols,
                        RbacRoleTest,
                        ::testing::Values(TransportProtocols::McbpPlain),
                        ::testing::PrintToStringParamName());


/**
 * An arithmetic operation requires read and write privilege as it returns
 * the value
 */
TEST_P(RbacRoleTest, Arithmetic) {
    auto& ro = getROConnection();
    auto& wo = getWOConnection();
    auto& rw = getRWConnection();

    // Try to increment the key (it doesn't exists, so it should be created
    // with value 0)
    try {
        ro.arithmetic(name, 1, 0);
        FAIL() << "The read-only user should not be allowed to create keys";
    } catch (ConnectionError& error) {
        EXPECT_TRUE(error.isAccessDenied());
    }

    try {
        wo.arithmetic(name, 1, 0);
        FAIL() << "The write-only user should not be allowed to create keys";
    } catch (ConnectionError& error) {
        EXPECT_TRUE(error.isAccessDenied());
    }

    rw.arithmetic(name, 0, 0);

    // The key exists, verify that we can't increment it if it exists
    try {
        ro.arithmetic(name, 1);
        FAIL() << "The read-only user should not be allowed to perform "
               << "arithmetic operations";
    } catch (ConnectionError& error) {
        EXPECT_TRUE(error.isAccessDenied());
    }

    try {
        wo.arithmetic(name, 1);
        FAIL() << "The write-only user should not be allowed to perform "
               << "arithmetic operations";
    } catch (ConnectionError& error) {
        EXPECT_TRUE(error.isAccessDenied());
    }
}

TEST_P(RbacRoleTest, MutationTest_ReadOnly) {
    auto& ro = getROConnection();

    try {
        store(ro, Greenstack::MutationType::Add);
        FAIL() << "The read-only user should not be able to add documents";
    } catch (const ConnectionError& error) {
        EXPECT_TRUE(error.isAccessDenied());
    }

    auto& rw = getRWConnection();
    store(rw, Greenstack::MutationType::Add);

    for (const auto& type : {Greenstack::MutationType::Append,
                             Greenstack::MutationType::Prepend,
                             Greenstack::MutationType::Set,
                             Greenstack::MutationType::Replace}) {
        try {
            store(ro, type);
            FAIL() << "The read-only user should not be able modify document with operation: "
                   << type;
        } catch (const ConnectionError& error) {
            EXPECT_TRUE(error.isAccessDenied());
        }
    }
}

TEST_P(RbacRoleTest, MutationTest_WriteOnly) {
    auto& wo = getWOConnection();

    // The Write Only user should be allowed to do all of these ops
    for (const auto& type : {Greenstack::MutationType::Add,
                             Greenstack::MutationType::Append,
                             Greenstack::MutationType::Prepend,
                             Greenstack::MutationType::Set,
                             Greenstack::MutationType::Replace}) {
        store(wo, type);
    }
}

TEST_P(RbacRoleTest, Remove_ReadOnly) {
    auto& rw = getRWConnection();
    store(rw, Greenstack::MutationType::Add);

    try {
        auto& ro = getROConnection();
        ro.remove(name, 0, 0);
        FAIL() << "The read-only user should not be able to remove documents";
    } catch (const ConnectionError& error) {
        EXPECT_TRUE(error.isAccessDenied());
    }
}

TEST_P(RbacRoleTest, Remove_WriteOnly) {
    auto& rw = getRWConnection();
    store(rw, Greenstack::MutationType::Add);
    rw.remove(name, 0, 0);
}

TEST_P(RbacRoleTest, NoAccessToUserXattrs) {
    auto& rw = getRWConnection();
    store(rw, Greenstack::MutationType::Add);

    // The read only user should not have access to create a user xattr
    auto resp = createXattr(getROConnection(), "meta.author", "\"larry\"");
    ASSERT_FALSE(resp.isSuccess());
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EACCESS, resp.getStatus());

    // The write only user should have access to create a user xattr
    resp = createXattr(getWOConnection(), "meta.author", "\"larry\"");
    ASSERT_TRUE(resp.isSuccess());

    // The read only user should be able to read it
    resp = getXattr(getROConnection(), "meta.author");
    ASSERT_TRUE(resp.isSuccess());

    // The write only user should NOT be able to read it
    resp = getXattr(getWOConnection(), "meta.author");
    ASSERT_FALSE(resp.isSuccess());
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EACCESS, resp.getStatus());

    // The rw user only have access to the system xattrs. Read and write
    // user xattrs should fail!
    resp = createXattr(getRWConnection(), "meta.author", "\"larry\"");
    ASSERT_FALSE(resp.isSuccess());
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EACCESS, resp.getStatus());

    resp = getXattr(getRWConnection(), "meta.author");
    ASSERT_FALSE(resp.isSuccess());
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EACCESS, resp.getStatus());
}

TEST_P(RbacRoleTest, NoAccessToSystemXattrs) {
    auto& rw = getRWConnection();
    store(rw, Greenstack::MutationType::Add);

    // The read only user should not have access to create a system xattr
    auto resp = createXattr(getROConnection(), "_meta.author", "\"larry\"");
    ASSERT_FALSE(resp.isSuccess());
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EACCESS, resp.getStatus());

    // The write only user should not have access to create a system xattr
    resp = createXattr(getROConnection(), "_meta.author", "\"larry\"");
    ASSERT_FALSE(resp.isSuccess());
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EACCESS, resp.getStatus());

    // The read-write user should have access to create a system xattr
    resp = createXattr(getRWConnection(), "_meta.author", "\"larry\"");
    ASSERT_TRUE(resp.isSuccess());

    // The read only user should not be able to read it
    resp = getXattr(getROConnection(), "_meta.author");
    ASSERT_FALSE(resp.isSuccess());
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EACCESS, resp.getStatus());

    // The write only user should not be able to read it
    resp = getXattr(getWOConnection(), "_meta.author");
    ASSERT_FALSE(resp.isSuccess());
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EACCESS, resp.getStatus());

    // The read write user should be able to read it
    resp = getXattr(getRWConnection(), "_meta.author");
    ASSERT_TRUE(resp.isSuccess());
}
