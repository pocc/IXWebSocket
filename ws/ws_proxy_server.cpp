/*
 *  ws_proxy_server.cpp
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2018 Machine Zone, Inc. All rights reserved.
 */

#include <iostream>
#include <ixwebsocket/IXWebSocketServer.h>
#include <sstream>

namespace ix
{
    class ProxyConnectionState : public ix::ConnectionState
    {
    public:
        void setRemoteHost(const std::string& host)
        {
            _remoteHost = host;
        }

        void setRemotePort(int port)
        {
            _remotePort = port;
        }

        ix::WebSocket& webSocket()
        {
            return _serverWebSocket;
        }

    private:
        std::string _remoteHost;
        int _remotePort;

        ix::WebSocket _serverWebSocket;
    };

    int ws_proxy_server_main(int port,
                             const std::string& hostname,
                             const ix::SocketTLSOptions& tlsOptions,
                             const std::string& remoteHost,
                             int remotePort)
    {
        std::cout << "Listening on " << hostname << ":" << port << std::endl;

        ix::WebSocketServer server(port, hostname);
        server.setTLSOptions(tlsOptions);

        auto factory = []() -> std::shared_ptr<ix::ConnectionState> {
            return std::make_shared<ProxyConnectionState>();
        };
        server.setConnectionStateFactory(factory);

        server.setOnConnectionCallback(
            [remoteHost, remotePort](std::shared_ptr<ix::WebSocket> webSocket,
                                     std::shared_ptr<ConnectionState> connectionState) {
                auto state = std::dynamic_pointer_cast<ProxyConnectionState>(connectionState);
                state->setRemoteHost(remoteHost);
                state->setRemotePort(remotePort);

                // Server connection
                state->webSocket().setOnMessageCallback(
                    [webSocket, state](const WebSocketMessagePtr& msg) {
                        if (msg->type == ix::WebSocketMessageType::Open)
                        {
                            std::cerr << "New connection" << std::endl;
                            std::cerr << "id: " << state->getId() << std::endl;
                            std::cerr << "Uri: " << msg->openInfo.uri << std::endl;
                            std::cerr << "Headers:" << std::endl;
                            for (auto it : msg->openInfo.headers)
                            {
                                std::cerr << it.first << ": " << it.second << std::endl;
                            }
                        }
                        else if (msg->type == ix::WebSocketMessageType::Close)
                        {
                            std::cerr << "Closed connection"
                                      << " code " << msg->closeInfo.code << " reason "
                                      << msg->closeInfo.reason << std::endl;
                        }
                        else if (msg->type == ix::WebSocketMessageType::Error)
                        {
                            std::stringstream ss;
                            ss << "Connection error: " << msg->errorInfo.reason << std::endl;
                            ss << "#retries: " << msg->errorInfo.retries << std::endl;
                            ss << "Wait time(ms): " << msg->errorInfo.wait_time << std::endl;
                            ss << "HTTP Status: " << msg->errorInfo.http_status << std::endl;
                            std::cerr << ss.str();
                            webSocket->close(msg->closeInfo.code, msg->closeInfo.reason);
                        }
                        else if (msg->type == ix::WebSocketMessageType::Message)
                        {
                            std::cerr << "Received " << msg->wireSize << " bytes from server" << std::endl;
                            webSocket->send(msg->str, msg->binary);
                        }
                    });

                // Client connection
                webSocket->setOnMessageCallback(
                    [state, remoteHost](const WebSocketMessagePtr& msg) {
                        if (msg->type == ix::WebSocketMessageType::Open)
                        {
                            std::cerr << "New connection" << std::endl;
                            std::cerr << "id: " << state->getId() << std::endl;
                            std::cerr << "Uri: " << msg->openInfo.uri << std::endl;
                            std::cerr << "Headers:" << std::endl;
                            for (auto it : msg->openInfo.headers)
                            {
                                std::cerr << it.first << ": " << it.second << std::endl;
                            }

                            // Connect to the 'real' server
                            std::string url(remoteHost);
                            url += msg->openInfo.uri;
                            state->webSocket().setUrl(url);
                            state->webSocket().start();
                        }
                        else if (msg->type == ix::WebSocketMessageType::Close)
                        {
                            std::cerr << "Closed connection"
                                      << " code " << msg->closeInfo.code << " reason "
                                      << msg->closeInfo.reason << std::endl;
                            state->webSocket().close(msg->closeInfo.code, msg->closeInfo.reason);
                        }
                        else if (msg->type == ix::WebSocketMessageType::Error)
                        {
                            std::stringstream ss;
                            ss << "Connection error: " << msg->errorInfo.reason << std::endl;
                            ss << "#retries: " << msg->errorInfo.retries << std::endl;
                            ss << "Wait time(ms): " << msg->errorInfo.wait_time << std::endl;
                            ss << "HTTP Status: " << msg->errorInfo.http_status << std::endl;
                            std::cerr << ss.str();
                        }
                        else if (msg->type == ix::WebSocketMessageType::Message)
                        {
                            std::cerr << "Received " << msg->wireSize << " bytes from client" << std::endl;
                            state->webSocket().send(msg->str, msg->binary);
                        }
                    });
            });

        auto res = server.listen();
        if (!res.first)
        {
            std::cerr << res.second << std::endl;
            return 1;
        }

        server.start();
        server.wait();

        return 0;
    }
} // namespace ix