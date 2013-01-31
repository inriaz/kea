// Copyright (C) 2009  Internet Systems Consortium, Inc. ("ISC")
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
// REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
// LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
// OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

#include <config.h>

// for some IPC/network system calls in asio/detail/pipe_select_interrupter.hpp 
#include <unistd.h>
// XXX: the ASIO header must be included before others.  See session.cc.
#include <asio.hpp>

#include <cc/session.h>
#include <cc/data.h>
#include <cc/tests/session_unittests_config.h>

#include <gtest/gtest.h>
#include <boost/bind.hpp>

#include <exceptions/exceptions.h>

#include <utility>
#include <vector>
#include <string>
#include <iostream>

using namespace isc::cc;
using std::pair;
using std::vector;
using std::string;
using isc::data::ConstElementPtr;
using isc::data::Element;

namespace {

TEST(AsioSession, establish) {
    asio::io_service io_service_;
    Session sess(io_service_);

    // can't return socket desciptor before session is established
    EXPECT_THROW(sess.getSocketDesc(), isc::InvalidOperation);

    EXPECT_THROW(
        sess.establish("/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/"
                       "/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/"
                       "/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/"
                       "/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/"
                       "/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/"
                       "/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/"
                       "/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/"
                       "/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/"
                       "/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/"
                       "/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/aaaaaaaaaa/"
                  ), isc::cc::SessionError
    );
}

/// \brief Pair holding header and data of a message sent over the wire.
typedef pair<ConstElementPtr, ConstElementPtr> SentMessage;

// This class sets up a domain socket for the session to connect to
// it will impersonate the msgq a tiny bit (if setSendLname() has
// been called, it will send an 'answer' to the lname query that is
// sent in the initialization of Session objects)
class TestDomainSocket {

public:
    TestDomainSocket(asio::io_service& io_service, const char* file) :
        io_service_(io_service),
        ep_(file),
        acceptor_(io_service_, ep_),
        socket_(io_service_)
    {
        acceptor_.async_accept(socket_,
                               boost::bind(&TestDomainSocket::acceptHandler,
                                           this, _1));
    }

    ~TestDomainSocket() {
        socket_.close();
        unlink(BIND10_TEST_SOCKET_FILE);
    }

    void acceptHandler(const asio::error_code&) const {
    }

    void sendmsg(isc::data::ElementPtr& env, isc::data::ElementPtr& msg) {
        const std::string header_wire = env->toWire();
        const std::string body_wire = msg->toWire();
        const unsigned int length = 2 + header_wire.length() +
            body_wire.length();
        const unsigned int length_net = htonl(length);
        const unsigned short header_length = header_wire.length();
        const unsigned short header_length_net = htons(header_length);

        socket_.send(asio::buffer(&length_net, sizeof(length_net)));
        socket_.send(asio::buffer(&header_length_net,
                                  sizeof(header_length_net)));
        socket_.send(asio::buffer(header_wire.data(), header_length));
        socket_.send(asio::buffer(body_wire.data(), body_wire.length()));
    }

    /// \brief Read a message from the socket
    ///
    /// Read a message from the socket and parse it. Block until it is
    /// read or error happens. If error happens, it asio::system_error.
    ///
    /// This method would block for ever if the sender is not sending.
    /// But the whole test has a timeout of 10 seconds (see the
    /// SessionTest::SetUp and SessionTest::TearDown).
    ///
    /// \note The method assumes the wire data are correct and does not check
    ///    it. Strange things might happen if it is not the case, but the
    ///    test would likely fail as a result, so we prefer simplicity here.
    ///
    /// \return Pair containing the header and body elements (in this order).
    SentMessage readmsg() {
        // The format is:
        // <uint32_t in net order = total length>
        // <uint16_t in net order = header length>
        // <char * header length = the header>
        // <char * the rest of the total length = the data>

        // Read and convert the lengths first.
        uint32_t total_len_data;
        uint16_t header_len_data;
        vector<asio::mutable_buffer> len_buffers;
        len_buffers.push_back(asio::buffer(&total_len_data,
                                           sizeof total_len_data));
        len_buffers.push_back(asio::buffer(&header_len_data,
                                           sizeof header_len_data));
        asio::read(socket_, len_buffers);
        const uint32_t total_len = ntohl(total_len_data);
        const uint16_t header_len = ntohs(header_len_data);
        string header, msg;
        header.resize(header_len);
        msg.resize(total_len - header_len - sizeof header_len_data);
        vector<asio::mutable_buffer> data_buffers;
        data_buffers.push_back(asio::buffer(&header[0], header.size()));
        data_buffers.push_back(asio::buffer(&msg[0], msg.size()));
        asio::read(socket_, data_buffers);
        if (msg == "") { // The case of no msg present, for control messages
            msg = "null";
        }
        // Extract the right data into each string and convert.
        return (SentMessage(Element::fromWire(header),
                            Element::fromWire(msg)));
    }

    void sendLname() {
        isc::data::ElementPtr lname_answer1 =
            isc::data::Element::fromJSON("{ \"type\": \"lname\" }");
        isc::data::ElementPtr lname_answer2 =
            isc::data::Element::fromJSON("{ \"lname\": \"foobar\" }");
        sendmsg(lname_answer1, lname_answer2);
    }

    void setSendLname() {
        // ignore whatever data we get, send back an lname
        asio::async_read(socket_,  asio::buffer(data_buf, 0),
                         boost::bind(&TestDomainSocket::sendLname, this));
    }

private:
    asio::io_service& io_service_;
    asio::local::stream_protocol::endpoint ep_;
    asio::local::stream_protocol::acceptor acceptor_;
    asio::local::stream_protocol::socket socket_;
    char data_buf[1024];
};

class SessionTest : public ::testing::Test {
protected:
    SessionTest() : sess(my_io_service), work(my_io_service) {
        // The TestDomainSocket is held as a 'new'-ed pointer,
        // so we can call unlink() first.
        unlink(BIND10_TEST_SOCKET_FILE);
        tds = new TestDomainSocket(my_io_service, BIND10_TEST_SOCKET_FILE);
    }

    ~SessionTest() {
        delete tds;
    }

    void SetUp() {
        // There are blocking reads in some tests. We want to have a safety
        // catch in case the sender didn't write enough. We set a timeout of
        // 10 seconds per one test (which should really be enough even on
        // slow machines). If the timeout happens, it kills the test and
        // the whole test fails.
        //alarm(10);
    }

    void TearDown() {
        // Cancel the timeout scheduled in SetUp. We don't want to kill any
        // of the other tests by it by accident.
        alarm(0);
    }

    // Check two elements are equal
    void elementsEqual(const ConstElementPtr& expected,
                       const ConstElementPtr& actual)
    {
        EXPECT_TRUE(expected->equals(*actual)) <<
            "Elements differ, expected: " << expected->toWire() <<
            ", got: " << actual->toWire();
    }

    // The same, but with one specified as string
    void elementsEqual(const string& expected,
                       const ConstElementPtr& actual)
    {
        const ConstElementPtr expected_el(Element::fromJSON(expected));
        elementsEqual(expected_el, actual);
    }

    // Check the session sent a message with the given header. The
    // message is hardcoded.
    void checkSentMessage(const string& expected_hdr,
                          const char* description)
    {
        SCOPED_TRACE(description);
        const SentMessage msg(tds->readmsg());
        elementsEqual(expected_hdr, msg.first);
        elementsEqual("{\"test\": 42}", msg.second);
    }

public:
    // used in the handler test
    // This handler first reads (and ignores) whatever message caused
    // it to be invoked. Then it calls group_recv for a second message.
    // If this message is { "command": "stop" } it'll tell the
    // io_service it is done. Otherwise it'll re-register this handler
    void someHandler() {
        isc::data::ConstElementPtr env, msg;
        sess.group_recvmsg(env, msg, false, -1);

        sess.group_recvmsg(env, msg, false, -1);
        if (msg && msg->contains("command") &&
            msg->get("command")->stringValue() == "stop") {
            my_io_service.stop();
        } else {
            sess.startRead(boost::bind(&SessionTest::someHandler, this));
        }
    }

protected:
    asio::io_service my_io_service;
    TestDomainSocket* tds;
    Session sess;
    // Keep run() from stopping right away by informing it it has work to do
    asio::io_service::work work;
};

TEST_F(SessionTest, timeout_on_connect) {
    // set to a short timeout so the test doesn't take too long
    EXPECT_EQ(4000, sess.getTimeout());
    sess.setTimeout(100);
    EXPECT_EQ(100, sess.getTimeout());
    // no answer, should timeout
    EXPECT_THROW(sess.establish(BIND10_TEST_SOCKET_FILE), SessionTimeout);
}

TEST_F(SessionTest, connect_ok) {
    tds->setSendLname();
    sess.establish(BIND10_TEST_SOCKET_FILE);
}

TEST_F(SessionTest, connect_ok_no_timeout) {
    tds->setSendLname();

    sess.setTimeout(0);
    sess.establish(BIND10_TEST_SOCKET_FILE);
}

TEST_F(SessionTest, connect_ok_connection_reset) {
    tds->setSendLname();

    sess.establish(BIND10_TEST_SOCKET_FILE);
    // Close the session again, so the next recv() should throw
    sess.disconnect();

    isc::data::ConstElementPtr env, msg;
    EXPECT_THROW(sess.group_recvmsg(env, msg, false, -1), SessionError);
}

TEST_F(SessionTest, run_with_handler) {
    tds->setSendLname();

    sess.establish(BIND10_TEST_SOCKET_FILE);
    sess.startRead(boost::bind(&SessionTest::someHandler, this));

    isc::data::ElementPtr env = isc::data::Element::fromJSON("{ \"to\": \"me\" }");
    isc::data::ElementPtr msg = isc::data::Element::fromJSON("{ \"some\": \"message\" }");
    tds->sendmsg(env, msg);

    msg = isc::data::Element::fromJSON("{ \"another\": \"message\" }");
    tds->sendmsg(env, msg);

    msg = isc::data::Element::fromJSON("{ \"a third\": \"message\" }");
    tds->sendmsg(env, msg);

    msg = isc::data::Element::fromJSON("{ \"command\": \"stop\" }");
    tds->sendmsg(env, msg);


    size_t count = my_io_service.run();
    ASSERT_EQ(2, count);
}

TEST_F(SessionTest, run_with_handler_timeout) {
    tds->setSendLname();

    sess.establish(BIND10_TEST_SOCKET_FILE);
    sess.startRead(boost::bind(&SessionTest::someHandler, this));
    sess.setTimeout(100);

    isc::data::ElementPtr env = isc::data::Element::fromJSON("{ \"to\": \"me\" }");
    isc::data::ElementPtr msg = isc::data::Element::fromJSON("{ \"some\": \"message\" }");
    tds->sendmsg(env, msg);

    msg = isc::data::Element::fromJSON("{ \"another\": \"message\" }");
    tds->sendmsg(env, msg);

    msg = isc::data::Element::fromJSON("{ \"a third\": \"message\" }");
    tds->sendmsg(env, msg);

    // No followup message, should time out.
    ASSERT_THROW(my_io_service.run(), SessionTimeout);
}

TEST_F(SessionTest, get_socket_descr) {
    tds->setSendLname();
    sess.establish(BIND10_TEST_SOCKET_FILE);

    int socket = 0;
    // session is established, so getSocketDesc() should work
    EXPECT_NO_THROW(socket = sess.getSocketDesc());

    // expect actual socket handle to be returned, not 0
    EXPECT_LT(0, socket);
}

// Test the group_sendmsg sends the correct data.
TEST_F(SessionTest, group_sendmsg) {
    // Connect
    tds->setSendLname();
    sess.establish(BIND10_TEST_SOCKET_FILE);
    elementsEqual("{\"type\": \"getlname\"}", tds->readmsg().first);

    const ConstElementPtr msg(Element::fromJSON("{\"test\": 42}"));
    sess.group_sendmsg(msg, "group");
    checkSentMessage("{"
                     "   \"from\": \"foobar\","
                     "   \"group\": \"group\","
                     "   \"instance\": \"*\","
                     "   \"seq\": 0,"
                     "   \"to\": \"*\","
                     "   \"type\": \"send\","
                     "   \"want_answer\": False"
                     "}", "No instance");
    sess.group_sendmsg(msg, "group", "instance", "recipient");
    checkSentMessage("{"
                     "   \"from\": \"foobar\","
                     "   \"group\": \"group\","
                     "   \"instance\": \"instance\","
                     "   \"seq\": 1,"
                     "   \"to\": \"recipient\","
                     "   \"type\": \"send\","
                     "   \"want_answer\": False"
                     "}", "With instance");
    sess.group_sendmsg(msg, "group", "*", "*", true);
    checkSentMessage("{"
                     "   \"from\": \"foobar\","
                     "   \"group\": \"group\","
                     "   \"instance\": \"*\","
                     "   \"seq\": 2,"
                     "   \"to\": \"*\","
                     "   \"type\": \"send\","
                     "   \"want_answer\": True"
                     "}", "Want answer");
    sess.group_sendmsg(msg, "group", "*", "*", false);
    checkSentMessage("{"
                     "   \"from\": \"foobar\","
                     "   \"group\": \"group\","
                     "   \"instance\": \"*\","
                     "   \"seq\": 3,"
                     "   \"to\": \"*\","
                     "   \"type\": \"send\","
                     "   \"want_answer\": False"
                     "}", "Doesn't want answer");
}

}
