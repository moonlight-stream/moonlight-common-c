# Moonlight Streaming Core Library

Moonlight-common-c contains the core GameStream client code shared between [Moonlight](http://moonlight-stream.com) clients, including 
[Moonlight Chrome](https://github.com/moonlight-stream/moonlight-chrome),
[Moonlight iOS](https://github.com/moonlight-stream/moonlight-ios), and [Moonlight Android](https://github.com/moonlight-stream/moonlight-android).

If you are implementing your own Moonlight game streaming client that can use a C library, you will need the code here.

## Note to Developers

Moonlight-common-c requires the _specific_ version of ENet that is bundled as a submodule. This version has changes required for IPv6 compatibility and retransmission reliability, among other things. These are breaking API/ABI changes which make Moonlight-common-c incompatible with other versions of the ENet library. Attempting to runtime link to another libenet library will cause your client to crash when connecting to recent versions of GeForce Experience.
