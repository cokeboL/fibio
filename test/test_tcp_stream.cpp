//
//  test_tcp_stream.cpp
//  fibio
//
//  Created by Chen Xu on 14-3-12.
//  Copyright (c) 2014 0d0a.com. All rights reserved.
//

#include <iostream>
#include <vector>
#include <chrono>
#include <boost/random.hpp>
#include <fibio/fiber.hpp>
#include <fibio/stream/iostream.hpp>
#include <boost/lexical_cast.hpp>
#include <fibio/io/ssl/stream.hpp>

using namespace fibio;

void child() {
    this_fiber::sleep_for(std::chrono::seconds(1));
    stream::tcp_stream str;
    std::error_code ec=str.connect("127.0.0.1", "23456");
    assert(!ec);
    str << "hello" << std::endl;
    for(int i=0; i<100; i++) {
        // Receive a random number from server and send it back
        std::string line;
        std::getline(str, line);
        int n=boost::lexical_cast<int>(line);
        str << n << std::endl;
    }
    str.close();
}

void parent() {
    fiber f(child);
    boost::random::mt19937 rng;
    boost::random::uniform_int_distribution<> rand(1,1000);

    tcp_stream_acceptor acc(23456);
    std::error_code ec;
    stream::tcp_stream str;
    acc(str, ec);
    assert(!ec);
    std::string line;
    std::getline(str, line);
    assert(line=="hello");
    for (int i=0; i<100; i++) {
        // Ping client with random number
        int n=rand(rng);
        str << n << std::endl;
        std::getline(str, line);
        int r=boost::lexical_cast<int>(line);
        assert(n==r);
    }
    str.close();
    acc.close();
    
    f.join();
}

typedef io::fiberized<asio::ip::tcp::socket> socket_type;
typedef stream::fiberized_iostream<io::fiberized<asio::ssl::stream<asio::ip::tcp::socket>>> ssl_tcp_stream;

// Certificates are copied from ASIO SSL example
void ssl_child() {
    this_fiber::sleep_for(std::chrono::seconds(1));
    asio::ssl::context ctx(asio::ssl::context::sslv23);
    ctx.load_verify_file("/tmp/ca.pem");
    ssl_tcp_stream str(ctx);
    str.stream_descriptor().set_verify_callback([](bool preverified, asio::ssl::verify_context&ctx){
        char subject_name[256];
        X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
        X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
        std::cout << "Verifying " << subject_name << "\n";
        return preverified;
    });
    std::error_code ec;
    ec=str.connect("127.0.0.1", "23457");
    assert(!ec);
    str.stream_descriptor().handshake(asio::ssl::stream_base::client, ec);
    assert(!ec);
    str << "hello" << std::endl;
    for(int i=0; i<100; i++) {
        // Receive a random number from server and send it back
        std::string line;
        std::getline(str, line);
        int n=boost::lexical_cast<int>(line);
        str << n << std::endl;
    }
    str.close();
}

void ssl_parent() {
    fiber f(ssl_child);
    boost::random::mt19937 rng;
    boost::random::uniform_int_distribution<> rand(1,1000);
    
    asio::ssl::context ctx(asio::ssl::context::sslv23);
    ctx.set_options(
                    asio::ssl::context::default_workarounds
                    | asio::ssl::context::no_sslv2
                    | asio::ssl::context::single_dh_use);
    ctx.set_password_callback([](std::size_t, asio::ssl::context::password_purpose)->std::string{ return "test"; });
    ctx.use_certificate_chain_file("/tmp/server.pem");
    ctx.use_private_key_file("/tmp/server.pem", asio::ssl::context::pem);
    ctx.use_tmp_dh_file("/tmp/dh512.pem");

    ssl_tcp_stream str(ctx);
    
    tcp_acceptor acc(asio::ip::tcp::endpoint(asio::ip::address::from_string("127.0.0.1"), 23457));
    std::error_code ec;
    acc.accept(str.stream_descriptor().next_layer(), ec);
    assert(!ec);
    str.stream_descriptor().handshake(asio::ssl::stream_base::server, ec);
    assert(!ec);
    std::string line;
    std::getline(str, line);
    assert(line=="hello");
    for (int i=0; i<100; i++) {
        // Ping client with random number
        int n=rand(rng);
        str << n << std::endl;
        std::getline(str, line);
        int r=boost::lexical_cast<int>(line);
        assert(n==r);
    }
    str.close();
    acc.close();
    
    f.join();
}

int main_fiber(int argc, char *argv[]) {
    fiber_group fibers;
    fibers.create_fiber(parent);
    fibers.create_fiber(ssl_parent);
    fibers.join_all();
    std::cout << "main_fiber exiting" << std::endl;
    return 0;
}

int main(int argc, char *argv[]) {
    return fiberize(1, main_fiber, argc, argv);
}
