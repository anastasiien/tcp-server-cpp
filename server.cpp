// Стандартные библиотеки C++
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector> 
#include <string> 
#include <cstring>
// Системные библиотеки для работы с сетью
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/file.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

using namespace std;

#define PORT 5050
#define BACKLOG 8
#define HISTORY_FILE "chat_history.txt"
#define LOCK_FILE "history.lock" 
#define MAX_MESSAGE_SIZE (10 * 1024 * 1024)

vector<string> chat_history;
pthread_mutex_t history_mutex = PTHREAD_MUTEX_INITIALIZER;

int listen_fd = -1; 
int lock_fd = -1;
volatile sig_atomic_t running = 1;

// Функция для получения блокировки файла, чтобы сервер не запускался дважды
int acquire_lock() { 
    lock_fd = open(LOCK_FILE, O_RDWR | O_CREAT, 0666);
    if (lock_fd == -1) {
        perror("open lock file");
        return 0;
    }
    if (flock(lock_fd, LOCK_EX | LOCK_NB) == -1) { 

        close(lock_fd);
        return 0;
    }
    string pid = to_string(getpid()); 
    ftruncate(lock_fd, 0); 
    if (write(lock_fd, pid.c_str(), pid.length()) < 0) { 
        perror("write lock file");

    }
    fsync(lock_fd);
    return 1;
}

// Функция для освобождения блокировки файла
void release_lock() { 
    if (lock_fd != -1) {
        flock(lock_fd, LOCK_UN); 
        close(lock_fd);
        unlink(LOCK_FILE);
    }
}

// Функция для безопасной отправки всех данных
int safe_send_all(int fd, const char* data, size_t len) { 
    size_t total = 0;
    while (total < len && running) { 
        ssize_t n;
        do {
            n = send(fd, data + total, len - total, 0); 
        } while (n < 0 && errno == EINTR && running);
        
        if (n < 0) {
            return 0;
        }
        if (n == 0) {
            return 0;
        }
        total += (size_t)n;
    }
    return 1;
}

 // Функция для безопасного приема всех данных 
int safe_recv_all(int fd, char* buf, size_t len) {
    size_t total = 0;
    while (total < len && running) { 
        ssize_t n;
        do {
            n = recv(fd, buf + total, len - total, 0);
        } while (n < 0 && errno == EINTR && running);
        
        if (n < 0) {
            return 0;
        }
        if (n == 0) {
            return 0;
        }
        total += (size_t)n;
    }
    return 1;
}

// Функция для отправки сообщения с заголовком длины
int send_message(int fd, const string& msg) { 
    uint32_t payload_len = msg.length();
    uint32_t total_len = htonl(4 + payload_len);
    if (!safe_send_all(fd, (char*)&total_len, 4)) return 0;
    if (payload_len > 0 && !safe_send_all(fd, msg.c_str(), payload_len)) return 0;
    return 1;
}

// Функция для приема сообщения
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

// Функция для записи сообщения в историю
void log_history(const string& who, const string& msg) {
    pthread_mutex_lock(&history_mutex);
    chat_history.push_back(who + ": " + msg); 
    pthread_mutex_unlock(&history_mutex);
}

// Функция для получения всей истории в виде строки
string get_history_string() { 
    pthread_mutex_lock(&history_mutex);
    stringstream ss;
    ss << "--- History ---\n";
    for (size_t i = 0; i < chat_history.size(); i++) ss << chat_history[i] << "\n";   
    ss << "----------------\n";
    pthread_mutex_unlock(&history_mutex); 
    return ss.str();
}

// Функция для сохранения истории в файл
void save_history() {
    pthread_mutex_lock(&history_mutex); 
    ofstream out(HISTORY_FILE);
    for (size_t i = 0; i < chat_history.size(); i++) out << chat_history[i] << "\n";
    out.close();
    pthread_mutex_unlock(&history_mutex);
}

// Обработчик сигналов
void handle_signal(int sig) {
    running = 0;
    if (listen_fd != -1) { 

        shutdown(listen_fd, SHUT_RDWR);
        close(listen_fd);
    }
}


struct client_arg {
    int client_fd;
    struct sockaddr_in addr; 
};

// Функция обработки клиента
void* handle_client(void* arg) { 
    client_arg* c = (client_arg*)arg;
    int fd = c->client_fd;
    char ipbuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &c->addr.sin_addr, ipbuf, sizeof(ipbuf));
    string client_ip = ipbuf;
    delete c;
    // Цикл обработки сообщений от клиента
    while (running) {
        string msg = receive_message(fd);
        if (msg.empty()) break;
        
        log_history("CLIENT(" + client_ip + ")", msg);

        string response;

        if (msg == "ping") response = "pong\n";
        else if (msg == "history") response = get_history_string();
        else if (msg == "quit") {
            response = "Server confirmed quit\n";
            send_message(fd, response);
            break;
        } else response = "ECHO: " + msg + "\n";

        if (!send_message(fd, response)) break;
    }

    close(fd);
    return NULL;
}

// Главная функция сервера
int main() { 
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (!acquire_lock()) {
        cerr << "Server already running\n";
        return 1;
    }

    ifstream in(HISTORY_FILE);
    string line;
    while (getline(in, line)) chat_history.push_back(line);
    in.close();
    // Создаем слушающий сокет для TCP
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {   
        perror("socket");
        release_lock();
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in srv; 
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(PORT);
    srv.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr*)&srv, sizeof(srv)) < 0) { 
        perror("bind");
        release_lock();
        return 1;
    }

    if (listen(listen_fd, BACKLOG) < 0) { 
        perror("listen");
        release_lock();
        return 1;
    }

    cout << "Server listening on port " << PORT << "\n";
    // Основной цикл сервера
    while (running) {
        struct sockaddr_in cli;
        socklen_t clen = sizeof(cli);

        int client_fd;
        do {
            client_fd = accept(listen_fd, (struct sockaddr*)&cli, &clen);
        } while (client_fd < 0 && errno == EINTR && running);
        
        if (client_fd < 0) {
            if (!running) break;
            if (errno != EBADF) {
                perror("accept");
            }
            continue;
        }

        client_arg* arg = new client_arg;
        arg->client_fd = client_fd;
        arg->addr = cli;

        pthread_t tid;

        if (pthread_create(&tid, NULL, handle_client, arg) != 0) {
            delete arg; 
            close(client_fd);
            perror("pthread_create");
            continue; 
        }
        pthread_detach(tid);
    }

    save_history();
    release_lock();
    return 0;
}
