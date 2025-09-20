# bender
A lightweight TCP proxy that bends traffic between a client and a server.

---
## Compile

Dependencies
```
apt install -y gcc-multilib g++-multilib build-essential mingw-w64
```

64bit linux
```
gcc lin-proxy.c -o proxy
```

32bit linux
```
gcc -m32 lin-proxy.c -o proxy
```

64bit Windows
```
x86_64-w64-mingw32-gcc win-proxy.c -o proxy.exe -lws2_32
```

32bit Windows
```
i686-w64-mingw32-gcc win-proxy.c -o proxy.exe -lws2_32
```

---
## Running one proxy
```
./proxy -l 0.0.0.0:8888 -f 127.0.0.1:80

proxy.exe -l 0.0.0.0:8888 -f 127.0.0.1:80
```

---
## Building multiple proxies via the menu
```
./proxy     
------ Proxy ------
1. List proxies
2. Add proxy
3. Remove proxy
4. Terminate
proxy#: 1
[*] No proxies detected.
------ Proxy ------
1. List proxies
2. Add proxy
3. Remove proxy
4. Terminate
proxy#: 2
listener: 0.0.0.0:8888
forwarder: 127.0.0.1:80
[*] Proxy thread started: 0.0.0.0:8888 -> 127.0.0.1:80
[+] Proxy added: 0.0.0.0:8888 -> 127.0.0.1:80
------ Proxy ------
1. List proxies
2. Add proxy
3. Remove proxy
4. Terminate
proxy#: 2
listener: 0.0.0.0:2121
forwarder: 127.0.0.1:21
[*] Proxy thread started: 0.0.0.0:2121 -> 127.0.0.1:21
[+] Proxy added: 0.0.0.0:2121 -> 127.0.0.1:21
------ Proxy ------
1. List proxies
2. Add proxy
3. Remove proxy
4. Terminate
proxy#: 1	
------ Active Proxies ------
1. 0.0.0.0:8888 -> 127.0.0.1:80
2. 0.0.0.0:2121 -> 127.0.0.1:21
```
