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
