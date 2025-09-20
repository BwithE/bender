#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>

#define BUF_SIZE 4096
#define MAX_PROXIES 32

typedef struct {
    char listen_ip[64];
    int listen_port;
    char forward_ip[64];
    int forward_port;
    int active;
    pthread_t thread;
} ProxyConfig;

ProxyConfig proxies[MAX_PROXIES];
int proxy_count = 0;

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) flags = 0;
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void* proxy_thread(void* arg) {
    ProxyConfig* config = (ProxyConfig*)arg;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        config->active = 0;
        return NULL;
    }

    struct sockaddr_in listen_addr;
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(config->listen_port);
    inet_pton(AF_INET, config->listen_ip, &listen_addr.sin_addr);

    if (bind(listen_fd, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        config->active = 0;
        return NULL;
    }

    if (listen(listen_fd, 128) < 0) {
        perror("listen");
        close(listen_fd);
        config->active = 0;
        return NULL;
    }

    set_nonblocking(listen_fd);

    printf("[*] Proxy thread started: %s:%d -> %s:%d\n",
           config->listen_ip, config->listen_port,
           config->forward_ip, config->forward_port);

    while (config->active) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            usleep(10000);
            continue;
        }
        set_nonblocking(client_fd);

        int target_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (target_fd < 0) {
            perror("socket target");
            close(client_fd);
            continue;
        }
        set_nonblocking(target_fd);

        struct sockaddr_in target_addr;
        target_addr.sin_family = AF_INET;
        target_addr.sin_port = htons(config->forward_port);
        inet_pton(AF_INET, config->forward_ip, &target_addr.sin_addr);

        if (connect(target_fd, (struct sockaddr*)&target_addr, sizeof(target_addr)) < 0) {
            // ignoring non-blocking connect error here
        }

        while (config->active) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(client_fd, &readfds);
            FD_SET(target_fd, &readfds);
            int maxfd = client_fd > target_fd ? client_fd : target_fd;

            struct timeval tv = {0, 100000};
            int ret = select(maxfd + 1, &readfds, NULL, NULL, &tv);
            if (ret < 0) break;
            if (ret == 0) continue;

            char buf[BUF_SIZE];
            int n;

            if (FD_ISSET(client_fd, &readfds)) {
                n = recv(client_fd, buf, BUF_SIZE, 0);
                if (n <= 0) break;
                send(target_fd, buf, n, 0);
            }

            if (FD_ISSET(target_fd, &readfds)) {
                n = recv(target_fd, buf, BUF_SIZE, 0);
                if (n <= 0) break;
                send(client_fd, buf, n, 0);
            }
        }

        close(client_fd);
        close(target_fd);
    }

    close(listen_fd);
    return NULL;
}

void shutdown_proxy(int index) {
    if (index < 0 || index >= proxy_count) return;
    if (proxies[index].active) {
        proxies[index].active = 0;
        pthread_join(proxies[index].thread, NULL);
    }
}

void shutdown_all() {
    for (int i = 0; i < proxy_count; i++) {
        shutdown_proxy(i);
    }
}

void list_proxies() {
    //printf("proxy#: 1\n");  // moved here so it prints once before the list

    if (proxy_count == 0) {
        printf("[*] No proxies detected.\n");
        return;
    }
    printf("------ Active Proxies ------\n");
    for (int i = 0; i < proxy_count; i++) {
        if (proxies[i].active) {
            printf("%d. %s:%d -> %s:%d\n", i + 1,
                   proxies[i].listen_ip, proxies[i].listen_port,
                   proxies[i].forward_ip, proxies[i].forward_port);
        }
    }
}

void add_proxy_interactive() {
    if (proxy_count >= MAX_PROXIES) {
        printf("[!] Max proxies reached.\n");
        return;
    }

    ProxyConfig* config = &proxies[proxy_count];

    printf("listener: ");
    if (scanf("%63[^:\n]:%d%*c", config->listen_ip, &config->listen_port) != 2) {
        printf("[!] Invalid listener format. Use ip:port\n");
        while (getchar() != '\n');  // clear input buffer
        return;
    }

    printf("forwarder: ");
    if (scanf("%63[^:\n]:%d%*c", config->forward_ip, &config->forward_port) != 2) {
        printf("[!] Invalid forwarder format. Use ip:port\n");
        while (getchar() != '\n');  // clear input buffer
        return;
    }

    config->active = 1;

    if (pthread_create(&config->thread, NULL, proxy_thread, config) != 0) {
        perror("pthread_create");
        config->active = 0;
        return;
    }

    // Sleep briefly so thread prints start message before menu prompt
    struct timespec ts = {0, 200 * 1000000}; // 200ms
    nanosleep(&ts, NULL);

    printf("[+] Proxy added: %s:%d -> %s:%d\n",
           config->listen_ip, config->listen_port,
           config->forward_ip, config->forward_port);

    proxy_count++;
}

void remove_proxy_interactive() {
    list_proxies();
    if (proxy_count == 0) return;

    printf("Remove proxy: ");
    int num;
    if (scanf("%d%*c", &num) != 1 || num < 1 || num > proxy_count) {
        printf("[!] Invalid selection\n");
        while (getchar() != '\n');  // clear input buffer
        return;
    }
    int idx = num - 1;
    if (!proxies[idx].active) {
        printf("[!] Proxy already inactive\n");
        return;
    }

    printf("[!] Removing proxy: %s:%d -> %s:%d\n",
           proxies[idx].listen_ip, proxies[idx].listen_port,
           proxies[idx].forward_ip, proxies[idx].forward_port);

    shutdown_proxy(idx);

    // Shift remaining proxies down to fill gap
    for (int i = idx; i < proxy_count - 1; i++) {
        proxies[i] = proxies[i + 1];
    }
    proxy_count--;
}

void clear_stdin_line() {
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF);
}

int get_menu_choice() {
    char buf[16];
    while (1) {
        printf("proxy#: ");
        if (!fgets(buf, sizeof(buf), stdin)) {
            return -1; // EOF
        }

        // Trim newline and spaces
        size_t len = strlen(buf);
        while (len > 0 && isspace((unsigned char)buf[len - 1])) {
            buf[--len] = '\0';
        }

        if (len == 0) {
            // empty input, reprint menu
            return 0;
        }

        char* endptr;
        long val = strtol(buf, &endptr, 10);
        if (*endptr == '\0') {
            return (int)val;
        }
        // Invalid input, prompt again
    }
}

void print_menu() {
    printf("------ Proxy ------\n");
    printf("1. List proxies\n");
    printf("2. Add proxy\n");
    printf("3. Remove proxy\n");
    printf("4. Terminate\n");
}

void menu() {
    while (1) {
        print_menu();

        int choice = get_menu_choice();

        if (choice == -1) {
            // EOF detected, terminate cleanly
            printf("\n[!] Terminating proxies!\n");
            shutdown_all();
            exit(0);
        }

        if (choice == 0) {
            // empty input, just loop menu again
            continue;
        }

        switch (choice) {
            case 1:
                list_proxies();
                break;
            case 2:
                add_proxy_interactive();
                break;
            case 3:
                remove_proxy_interactive();
                break;
            case 4:
                printf("[!] Terminating proxies!\n");
                shutdown_all();
                exit(0);
            default:
                printf("[!] Invalid option\n");
                break;
        }
    }
}

int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN);  // Ignore SIGPIPE on send()

    if (argc == 5 && strcmp(argv[1], "-l") == 0 && strcmp(argv[3], "-f") == 0) {
        ProxyConfig config;
        if (sscanf(argv[2], "%63[^:]:%d", config.listen_ip, &config.listen_port) != 2) {
            fprintf(stderr, "Invalid listen argument format\n");
            return 1;
        }
        if (sscanf(argv[4], "%63[^:]:%d", config.forward_ip, &config.forward_port) != 2) {
            fprintf(stderr, "Invalid forward argument format\n");
            return 1;
        }
        config.active = 1;
        proxy_thread(&config);
        return 0;
    }

    menu();

    return 0;
}
