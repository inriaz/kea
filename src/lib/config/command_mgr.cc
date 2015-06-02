// Copyright (C) 2015 Internet Systems Consortium, Inc. ("ISC")
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

#include <config/command_mgr.h>
#include <config/command_socket_factory.h>
#include <cc/data.h>
#include <dhcp/iface_mgr.h>
#include <config/config_log.h>
#include <boost/bind.hpp>

using namespace isc::data;

namespace isc {
namespace config {

CommandMgr::CommandMgr() {
    registerCommand("list-commands",
        boost::bind(&CommandMgr::listCommandsHandler, this, _1, _2));
}

int CommandMgr::openCtrlSocket(const isc::data::ConstElementPtr& socket_info) {
    if (socket_info_) {
        isc_throw(SocketError, "There is already a control socket open");
    }

    socket_ = CommandSocketFactory::create(socket_info);
    socket_info_ = socket_info;

    // Install this socket in Interface Manager.
    isc::dhcp::IfaceMgr::instance().addExternalSocket(socket_,
        boost::bind(&isc::config::CommandMgr::connectionAcceptor, socket_));

    return (socket_);
}

void CommandMgr::closeCtrlSocket() {
    if (socket_info_) {

        isc::dhcp::IfaceMgr::instance().deleteExternalSocket(socket_);

        CommandSocketFactory::close(socket_, socket_info_);
        socket_ = 0;
        socket_info_.reset();
    }
}

CommandMgr&
CommandMgr::instance() {
    static CommandMgr cmd_mgr;
    return (cmd_mgr);
}

void CommandMgr::registerCommand(const std::string& cmd, CommandHandler handler) {

    if (!handler) {
        isc_throw(InvalidCommandHandler, "Specified command handler is NULL");
    }

    HandlerContainer::const_iterator it = handlers_.find(cmd);
    if (it != handlers_.end()) {
        isc_throw(InvalidCommandName, "Handler for command '" << cmd
                  << "' is already installed.");
    }

    handlers_.insert(make_pair(cmd, handler));

    LOG_DEBUG(command_logger, DBG_COMMAND, COMMAND_REGISTERED).arg(cmd);
}

void CommandMgr::deregisterCommand(const std::string& cmd) {
    if (cmd == "list-commands") {
        isc_throw(InvalidCommandName,
                  "Can't uninstall internal command 'list-commands'");
    }

    HandlerContainer::iterator it = handlers_.find(cmd);
    if (it == handlers_.end()) {
        isc_throw(InvalidCommandName, "Handler for command '" << cmd
                  << "' not found.");
    }
    handlers_.erase(it);

    LOG_DEBUG(command_logger, DBG_COMMAND, COMMAND_DEREGISTERED).arg(cmd);
}

void CommandMgr::deregisterAll() {

    // No need to log anything here. deregisterAll is not used in production
    // code, just in tests.
    handlers_.clear();
    registerCommand("list-commands",
        boost::bind(&CommandMgr::listCommandsHandler, this, _1, _2));
}

void
CommandMgr::connectionAcceptor(int sockfd) {

    /// @todo: Either make this generic or rename this method
    /// to CommandSocketFactory::unixConnectionAcceptor
    struct sockaddr_un client_addr;
    socklen_t client_addr_len;
    client_addr_len = sizeof(client_addr);

    // Accept incoming connection. This will create a separate socket for
    // handling this specific connection.
    int fd2 = accept(sockfd, (struct sockaddr*) &client_addr, &client_addr_len);

    // Not sure if this is really needed, but let's set it to non-blocking mode.
    fcntl(fd2, F_SETFL, O_NONBLOCK);

    // Install commandReader callback. When there's any data incoming on this
    // socket, commandReader will be called and process it. It may also
    // eventually close this socket.
    isc::dhcp::IfaceMgr::instance().addExternalSocket(fd2,
        boost::bind(&isc::config::CommandMgr::commandReader, fd2));

    LOG_INFO(command_logger, COMMAND_SOCKET_CONNECTION_OPENED).arg(fd2).arg(sockfd);
}

void
CommandMgr::commandReader(int sockfd) {

    // We should not expect commands bigger than 64K.
    char buf[65536];
    memset(buf, 0, sizeof(buf));
    ConstElementPtr cmd, rsp;

    // Read incoming data.
    int rval = read(sockfd, buf, sizeof(buf));
    if (rval < 0) {
        // Read failed
        LOG_WARN(command_logger, COMMAND_SOCKET_READ_FAIL).arg(rval).arg(sockfd);
        return;
    } else if (rval == 0) {

        // read of 0 bytes means end-of-file. In other words the connection is
        // being closed.

        LOG_INFO(command_logger, COMMAND_SOCKET_CONNECTION_CLOSED).arg(sockfd);

        // Unregister this callback
        isc::dhcp::IfaceMgr::instance().deleteExternalSocket(sockfd);

        // Close the socket.
        close(sockfd);
        return;
    }

    LOG_DEBUG(command_logger, DBG_COMMAND, COMMAND_SOCKET_READ).arg(rval).arg(sockfd);

    // Ok, we received something. Let's see if we can make any sense of it.
    try {

        // Try to interpret it as JSON.
        cmd = Element::fromJSON(std::string(buf), true);

        // If successful, then process it as a command.
        rsp = CommandMgr::instance().processCommand(cmd);
    } catch (const Exception& ex) {
        LOG_WARN(command_logger, COMMAND_PROCESS_ERROR).arg(ex.what());
        rsp = createAnswer(CONTROL_RESULT_ERROR, std::string(ex.what()));
    }

    if (!rsp) {
        LOG_WARN(command_logger, COMMAND_RESPONSE_ERROR);
        return;
    }

    // Let's convert JSON response to text. Note that at this stage
    // the rsp pointer is always set.
    std::string txt = rsp->str();
    size_t len = txt.length();
    if (len > 65535) {
        // Hmm, our response is too large. Let's send the first
        // 64KB and hope for the best.
        len = 65535;
    }

    // Send the data back over socket.
    rval = write(sockfd, txt.c_str(), len);

    LOG_DEBUG(command_logger, DBG_COMMAND, COMMAND_SOCKET_WRITE).arg(len).arg(sockfd);

    if (rval < 0) {
        // Response transmission failed. Since the response failed, it doesn't
        // make sense to send any status codes. Let's log it and be done with
        // it.
        LOG_WARN(command_logger, COMMAND_SOCKET_WRITE_FAIL).arg(len).arg(sockfd);
    }
}

isc::data::ConstElementPtr
CommandMgr::processCommand(const isc::data::ConstElementPtr& cmd) {
    if (!cmd) {
        return (createAnswer(CONTROL_RESULT_ERROR,
                             "Command processing failed: NULL command parameter"));
    }

    try {
        ConstElementPtr arg;
        std::string name = parseCommand(arg, cmd);

        LOG_INFO(command_logger, COMMAND_RECEIVED).arg(name);

        HandlerContainer::const_iterator it = handlers_.find(name);
        if (it == handlers_.end()) {
            // Ok, there's no such command.
            return (createAnswer(CONTROL_RESULT_ERROR,
                                 "'" + name + "' command not supported."));
        }

        // Call the actual handler and return whatever it returned
        return (it->second(name, arg));

    } catch (const Exception& e) {
        return (createAnswer(CONTROL_RESULT_ERROR,
                             std::string("Error during command processing:")
                             + e.what()));
    }
}

isc::data::ConstElementPtr
CommandMgr::listCommandsHandler(const std::string& name,
                                const isc::data::ConstElementPtr& params) {
    using namespace isc::data;
    ElementPtr commands = Element::createList();
    for (HandlerContainer::const_iterator it = handlers_.begin();
         it != handlers_.end(); ++it) {
        commands->add(Element::create(it->first));
    }
    return (createAnswer(CONTROL_RESULT_SUCCESS, commands));
}

}; // end of isc::config
}; // end of isc
