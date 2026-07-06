/*
 * Copyright 2026 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "conformance/SimControl.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace
{
/**
 * Minimal blocking HTTP/1.0 exchange against the sim: connect to host:port, send
 * @p request, read the whole reply (server closes on HTTP/1.0), return the body.
 * Kept dependency-free (no libcurl) — the sim is a loopback service the harness
 * owns, and this only ever runs inside the software-platform container. Returns
 * an empty string on any failure so callers treat unreachable as "not confirmed".
 */
std::string httpExchange(const std::string &host, const std::string &port, const std::string &request)
{
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0)
    {
        return {};
    }

    int fd = -1;
    for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next)
    {
        fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == -1)
        {
            continue;
        }
        if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
        {
            break;
        }
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(result);
    if (fd == -1)
    {
        return {};
    }

    std::string reply;
    bool ok = true;
    for (size_t sent = 0; sent < request.size();)
    {
        ssize_t n = ::send(fd, request.data() + sent, request.size() - sent, 0);
        if (n <= 0)
        {
            ok = false;
            break;
        }
        sent += static_cast<size_t>(n);
    }
    if (ok)
    {
        char buf[1024];
        ssize_t n = 0;
        while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0)
        {
            reply.append(buf, static_cast<size_t>(n));
        }
    }
    ::close(fd);
    return reply;
}

std::string httpBody(const std::string &reply)
{
    const std::string sep{"\r\n\r\n"};
    const size_t pos = reply.find(sep);
    return pos == std::string::npos ? std::string{} : reply.substr(pos + sep.size());
}
} // namespace

namespace rialto::conformance
{
bool SimControl::fromEnvironment(SimControl &out)
{
    const char *host = std::getenv("RIALTO_CONFORMANCE_SIM_HOST");
    const char *port = std::getenv("RIALTO_CONFORMANCE_SIM_PORT");
    const char *app = std::getenv("RIALTO_CONFORMANCE_APP");
    if (host == nullptr || port == nullptr || app == nullptr || *host == '\0' || *port == '\0' || *app == '\0')
    {
        return false;
    }
    out.m_host = host;
    out.m_port = port;
    out.m_app = app;
    return true;
}

bool SimControl::setState(const std::string &state) const
{
    const std::string request{"POST /SetState/" + m_app + "/" + state + " HTTP/1.0\r\n" + "Host: " + m_host +
                              "\r\nContent-Length: 0\r\n\r\n"};
    // The sim's SetState replies "SetState command succeeded!" in the body when the
    // requested transition was accepted (changeSessionServerState).
    return httpBody(httpExchange(m_host, m_port, request)).find("succeeded") != std::string::npos;
}

std::string SimControl::getState() const
{
    const std::string request{"GET /GetState/" + m_app + " HTTP/1.0\r\n" + "Host: " + m_host + "\r\n\r\n"};
    const std::string body{httpBody(httpExchange(m_host, m_port, request))};
    // GetState answers "... returned: <State>"; extract the trailing token.
    const std::string marker{"returned: "};
    const size_t pos = body.find(marker);
    if (pos == std::string::npos)
    {
        return {};
    }
    size_t begin = pos + marker.size();
    size_t end = begin;
    while (end < body.size() && (std::isalpha(static_cast<unsigned char>(body[end])) != 0))
    {
        ++end;
    }
    return body.substr(begin, end - begin);
}
} // namespace rialto::conformance
