# xmrd
Drop-in alternative for xibo-xmr

## Build
```shell
sudo apt install libzmq3-dev
git clone https://github.com/ajiwo/xmrd.git
cd xmrd && git submodule update --init --recursive
mkdir build && cd build
cmake .. && make
# create/edit /etc/xmr/config.json
mkdir -p /etc/xmr
./xmrd
```
## Configuration
By default, xmrd expect `/etc/xmr/config.json` as the configuration file.
```json
{
  "listenOn": "tcp://127.0.0.1:50001",
  "pubOn": ["tcp://*:9505", "tcp://*:8505"],
  "debug": false,
  "ipv6RespSupport": true,
  "ipv6PubSupport": false
}
```
xmrd only support up to 5 `pubOn` endpoint urls.

## Configure xmrd as a service on Debian 8 (x64) with systemd
```shell
cd ~/xmrd/build/
ln -s xmrd /usr/local/bin/xmrd
chmod ugo+x /usr/local/bin/xmrd
```

### Configuration xmrd.service

```
cat > /lib/systemd/system/xmrd.service <<- "EOF"
[Unit]
Description=XMR ZMQ Service
After=network.target

[Service]
ExecStart=/usr/local/bin/xmrd
Restart=always
KillMode=process

[Install]
WantedBy=multi-user.target
EOF
```

### Enable xmrd, Start the service & check status
```shell
systemctl enable xmrd
systemctl start xmrd
systemctl status xmrd
```

## How to install latest version of cmake?
Fetch latest version from: https://cmake.org/download/ for your OS
For Linux 64 bit Distributions
```
wget https://cmake.org/files/v3.8/cmake-3.8.1-Linux-x86_64.sh

sh cmake-3.8.1-Linux-x86_64.sh --prefix=/opt/cmake
```

### Contributor(s)
* https://github.com/ajiwo
* https://github.com/yashodhank
