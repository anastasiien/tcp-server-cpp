#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

#define SERVER_IP "127.0.0.1"
#define PORT 5050


int safe_send_all(int fd, const char* data, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(fd, data + total, len - total, 0);
        if (n <= 0) return 0;
        total += n;
    }
    return 1;
}

int safe_recv_all(int fd, char* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = recv(fd, buf + total, len - total, 0);
        if (n <= 0) return 0;
        total += n;
    }
    return 1;
}

int send_message(int fd, const string& msg) {
    uint32_t payload_len = msg.length();
    uint32_t total_len = htonl(4 + payload_len);
    if (!safe_send_all(fd, (char*)&total_len, 4)) return 0;
    if (payload_len > 0 && !safe_send_all(fd, msg.c_str(), payload_len)) return 0;
    return 1;
}

string receive_message(int fd) {
    char header[4];
    if (!safe_recv_all(fd, header, 4)) return "";
    uint32_t total_len;
    memcpy(&total_len, header, 4);
    total_len = ntohl(total_len);
    if (total_len < 4) return "";
    size_t payload_len = total_len - 4;
    
    if (payload_len == 0) return ""; 

    string payload(payload_len, '\0');
    if (!safe_recv_all(fd, &payload[0], payload_len)) return "";
    return payload;
}

int main() {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) { perror("socket"); return 1; }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("inet_pton"); close(sock_fd); return 1;
    }

    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect"); close(sock_fd); return 1;
    }

    cout << "Connected to server " << SERVER_IP << ":" << PORT << "\n";
    cout << "Commands: ping, history, quit, or any text\n";

    while (true) {
        cout << "> ";
        string input;
        if (!getline(cin, input)) break;
                
        if (!send_message(sock_fd, input)) break;

        string response = receive_message(sock_fd);
        if (response.empty()) {
            cout << "Server closed connection or error\n";
            break;
        }

        cout << response;

        if (input == "quit") break;
    }

    close(sock_fd);
    cout << "Client exiting\n";
    return 0;
}