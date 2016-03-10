#Moonlight

Moonlight-common-c contains common C code between [Moonlight](http://moonlight-stream.com) clients, including 
[Moonlight Chrome](https://github.com/moonlight-stream/moonlight-chrome) and
[Moonlight iOS](https://github.com/moonlight-stream/moonlight-ios).

If you are implementing your own Moonlight game streaming client that can use a C library, you will need the code here.

It implements the actual GameStream protocol.

## Note to Developers

Moonlight-common-c requires the _specific_ version of ENet that is bundled as a submodule. This version has changes required for IPv6 compatibility and retransmission reliability, among other things. These are breaking API/ABI changes which make Moonlight-common-c incompatible with other versions of the ENet library. Attempting to runtime link to another libenet library will cause your client to crash when connecting to recent versions of GeForce Experience.
