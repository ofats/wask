#include <algorithm>
#include <string>

#include <netdb.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

constexpr char usage_message[] = "Usage: ./wask [-o OUTPUT_PATH] URL";
constexpr std::size_t buffer_size = 1024;

void show_usage_and_exit() {
    std::fprintf(stderr, "%s\n", usage_message);
    std::exit(1);
}

void show_error_and_exit(const char* msg) {
    perror(msg);
    exit(1);
}

std::pair<std::string, std::string> extract_host_and_path(
    std::string_view url) {
    auto slash_pos = std::find(url.cbegin(), url.cend(), '/');
    return {std::string(url.cbegin(), slash_pos),
            std::string(slash_pos, url.cend())};
}

int get_socket() {
    int result = socket(AF_INET, SOCK_STREAM, 0);
    if (result < 0) {
        show_error_and_exit("socket() failed");
    }
    return result;
}

std::uint32_t get_server_addr(const char* url) {
    auto* server = gethostbyname(url);
    if (nullptr == server) {
        show_error_and_exit("gethostbyname() failed");
    }
    std::uint32_t result = 0;
    bcopy((const char*)server->h_addr, (char*)&result, server->h_length);
    return result;
}

void make_connection(int sockfd, sockaddr_in& server_addr) {
    if (connect(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        show_error_and_exit("connect() failed");
    }
}

void write_to_socket(int sockfd, std::string_view msg) {
    do {
        auto n = write(sockfd, msg.cbegin(), msg.size());
        if (n < 0) {
            show_error_and_exit("write() failed");
        }
        msg = msg.substr(n);
    } while (!msg.empty());
}

int main(int argc, const char* argv[]) {
    if (argc < 2) {
        show_usage_and_exit();
    }
    if (std::equal(argv[1], argv[1] + 2, "-o")) {
        if (argc < 4) {
            show_usage_and_exit();
        }
        if (nullptr == std::freopen(argv[2], "w", stdout)) {
            std::fprintf(stderr, "Can't open %s for writing\n", argv[2]);
            std::exit(1);
        }
    }

    auto [host, path] = extract_host_and_path(argv[argc - 1]);

    auto sockfd = get_socket();

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(80);
    server_addr.sin_addr.s_addr = get_server_addr(host.c_str());

    make_connection(sockfd, server_addr);

    using namespace std::string_literals;

    auto msg = "GET " + (path.empty() ? "/"s : path) +
               " HTTP/1.0\r\n"  // We are using HTTP/1.0 to disable chunked
                                // transfer encoding and to implicitly close
                                // connection after reading
               "Host: " +
               host +
               "\r\n"
               "Accept: */*\r\n\r\n";

    write_to_socket(sockfd, msg);

    enum class read_state_t {
        head,
        r,
        n,
        r2,
        body
    } read_state = read_state_t::head;

    char buffer[buffer_size];
    ssize_t n = 0;
    // Just reading until occurence of EIO when server closes connection
    while ((n = read(sockfd, buffer, buffer_size - 1)) > 0) {
        // Streaming parser that trying to find first occurence of
        // \r\n\r\n (or just \n\n) to start body reading
        if (read_state_t::body == read_state) {
            printf("%s", buffer);
        } else {
            for (const auto* p = buffer; '\0' != *p; ++p) {
                if (read_state_t::body == read_state) {
                    printf("%s", p);
                    break;
                }
                switch (read_state) {
                    case read_state_t::head:
                        if ('\r' == *p) {
                            read_state = read_state_t::r;
                        } else if ('\n' == *p) {  // for those servers that
                                                  // don't write \r before \n
                            read_state = read_state_t::n;
                        }
                        break;
                    case read_state_t::r:
                        if ('\n' == *p) {
                            read_state = read_state_t::n;
                        } else {
                            std::fprintf(stderr, "No \\n after \\r\n");
                            std::exit(1);
                        }
                        break;
                    case read_state_t::n:
                        if ('\r' == *p) {
                            read_state = read_state_t::r2;
                        } else if ('\n' == *p) {  // for those servers that
                                                  // don't write \r before \n
                            read_state = read_state_t::body;
                        } else {
                            read_state = read_state_t::head;
                        }
                        break;
                    case read_state_t::r2:
                        if ('\n' == *p) {
                            read_state = read_state_t::body;
                        } else {
                            std::fprintf(stderr, "No \\n after \\r\\n\\r\n");
                            std::exit(1);
                        }
                        break;
                    default:
                        break;
                }
            }
        }
        bzero(buffer, buffer_size);
    }

    close(sockfd);
    return 0;
}
