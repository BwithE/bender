#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#pragma comment(lib, "Ws2_32.lib")

#define BUF_SIZE 4096
#define MAX_PROXIES 32

typedef struct {
    char listen_ip[64];
    int listen_port;
    char forward_ip[64];
    int forward_port;
    int active;
    HANDLE thread;
    SOCKET listen_fd;
} ProxyConfig;

ProxyConfig proxies[MAX_PROXIES];
int proxy_count = 0;

DWORD WINAPI connection_handler(LPVOID arg) {
    SOCKET* params = (SOCKET*)arg;
    SOCKET client_fd = params[0];
    ProxyConfig* config = (ProxyConfig*)params[1];
    free(params);

    SOCKET target_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (target_fd == INVALID_SOCKET) { closesocket(client_fd); return 1; }

    struct sockaddr_in target_addr = {0};
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(config->forward_port);
    InetPtonA(AF_INET, config->forward_ip, &target_addr.sin_addr);

    if (connect(target_fd, (struct sockaddr*)&target_addr, sizeof(target_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "Connect failed: %d\n", WSAGetLastError());
        closesocket(client_fd); closesocket(target_fd); return 1;
    }

    char buf[BUF_SIZE];
    int n;
    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(client_fd, &fds);
        FD_SET(target_fd, &fds);

        int ret = select(0, &fds, NULL, NULL, NULL);
        if (ret == SOCKET_ERROR) break;

        if (FD_ISSET(client_fd, &fds)) { n = recv(client_fd, buf, BUF_SIZE, 0); if (n <= 0) break; send(target_fd, buf, n, 0); }
        if (FD_ISSET(target_fd, &fds)) { n = recv(target_fd, buf, BUF_SIZE, 0); if (n <= 0) break; send(client_fd, buf, n, 0); }
    }

    closesocket(client_fd);
    closesocket(target_fd);
    return 0;
}

DWORD WINAPI proxy_thread(LPVOID arg) {
    ProxyConfig* config = (ProxyConfig*)arg;
    config->listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (config->listen_fd == INVALID_SOCKET) { fprintf(stderr,"Socket failed\n"); return 1; }

    struct sockaddr_in listen_addr = {0};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(config->listen_port);
    InetPtonA(AF_INET, config->listen_ip, &listen_addr.sin_addr);

    if (bind(config->listen_fd, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) == SOCKET_ERROR) { fprintf(stderr,"Bind failed\n"); return 1; }
    if (listen(config->listen_fd, SOMAXCONN) == SOCKET_ERROR) { fprintf(stderr,"Listen failed\n"); return 1; }

    printf("[*] Proxy %s:%d -> %s:%d started\n", config->listen_ip, config->listen_port, config->forward_ip, config->forward_port);

    while (config->active) {
        struct sockaddr_in client_addr;
        int len = sizeof(client_addr);
        SOCKET client_fd = accept(config->listen_fd, (struct sockaddr*)&client_addr, &len);
        if (client_fd == INVALID_SOCKET) { if (!config->active) break; Sleep(50); continue; }

        SOCKET* params = (SOCKET*)malloc(2*sizeof(SOCKET));
        params[0] = client_fd;
        params[1] = (SOCKET)config;
        HANDLE t = CreateThread(NULL,0,connection_handler,params,0,NULL);
        if (t) CloseHandle(t);
    }

    closesocket(config->listen_fd);
    return 0;
}

void shutdown_proxy(int index) {
    if (index < 0 || index >= proxy_count) return;
    if (proxies[index].active) {
        proxies[index].active = 0;
        closesocket(proxies[index].listen_fd);
        WaitForSingleObject(proxies[index].thread, INFINITE);
        CloseHandle(proxies[index].thread);
    }
}

void shutdown_all() {
    for (int i = 0; i < proxy_count; i++) shutdown_proxy(i);
}

void list_proxies() {
    if (proxy_count == 0) { printf("[*] No proxies.\n"); return; }
    printf("------ Active Proxies ------\n");
    for (int i = 0; i < proxy_count; i++) {
        if (proxies[i].active)
            printf("%d) %s:%d -> %s:%d\n", i+1, proxies[i].listen_ip, proxies[i].listen_port, proxies[i].forward_ip, proxies[i].forward_port);
    }
}

void add_proxy_interactive() {
    if (proxy_count >= MAX_PROXIES) { printf("[!] Max proxies reached.\n"); return; }
    ProxyConfig* config = &proxies[proxy_count];
    char listen_input[128], forward_input[128];

    printf("Listener (ip:port): ");
    if (!fgets(listen_input,sizeof(listen_input),stdin)) return;
    if (sscanf(listen_input,"%63[^:]:%d",config->listen_ip,&config->listen_port)!=2){ printf("[!] Invalid format.\n"); return; }

    printf("Forwarder (ip:port): ");
    if (!fgets(forward_input,sizeof(forward_input),stdin)) return;
    if (sscanf(forward_input,"%63[^:]:%d",config->forward_ip,&config->forward_port)!=2){ printf("[!] Invalid format.\n"); return; }

    config->active = 1;
    config->thread = CreateThread(NULL,0,proxy_thread,config,0,NULL);
    Sleep(200);
    printf("[+] Proxy added: %s:%d -> %s:%d\n", config->listen_ip, config->listen_port, config->forward_ip, config->forward_port);
    proxy_count++;
}

void remove_proxy_interactive() {
    list_proxies();
    if (proxy_count == 0) return;
    printf("Remove proxy #: ");
    char buf[16]; if(!fgets(buf,sizeof(buf),stdin)) return;
    int num = atoi(buf); if(num<1 || num>proxy_count){printf("[!] Invalid choice\n"); return;}
    int idx = num-1; if(!proxies[idx].active){printf("[!] Already inactive\n"); return;}
    shutdown_proxy(idx);
    for(int i=idx;i<proxy_count-1;i++) proxies[i]=proxies[i+1];
    proxy_count--;
}

void print_menu() {
    printf("------ Proxy Menu ------\n1) List proxies\n2) Add proxy\n3) Remove proxy\n4) Exit\n");
}

int get_menu_choice() { char buf[16]; printf("Choice: "); if(!fgets(buf,sizeof(buf),stdin)) return -1; return atoi(buf); }

int main(int argc, char* argv[]) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa)!=0){ fprintf(stderr,"WSAStartup failed\n"); return 1; }

    if(argc==5 && strcmp(argv[1],"-l")==0 && strcmp(argv[3],"-f")==0) {
        // Single-use mode
        ProxyConfig proxy = {0};
        proxy.active = 1;
        sscanf(argv[2],"%63[^:]:%d",proxy.listen_ip,&proxy.listen_port);
        sscanf(argv[4],"%63[^:]:%d",proxy.forward_ip,&proxy.forward_port);
        HANDLE t = CreateThread(NULL,0,proxy_thread,&proxy,0,NULL);
        WaitForSingleObject(t,INFINITE);
    } else {
        // Menu mode
        int running = 1;
        while(running){
            print_menu();
            int choice = get_menu_choice();
            switch(choice){
                case 1: list_proxies(); break;
                case 2: add_proxy_interactive(); break;
                case 3: remove_proxy_interactive(); break;
                case 4: running=0; break;
                default: printf("[!] Invalid choice\n"); break;
            }
        }
        shutdown_all();
    }

    WSACleanup();
    return 0;
}
