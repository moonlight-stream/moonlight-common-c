# Experimental native microphone negotiation

This draft isolates opt-in NVIDIA GameStream microphone session negotiation for review. It does not capture audio, transmit voice, create a Windows recording device, or implement microphone encryption. Existing callers retain their current handshake when the new configuration field is zero.

## Session behavior

A caller initializes `STREAM_CONFIGURATION` with `LiInitializeStreamConfiguration()` and explicitly sets `requestNativeMic` before starting a connection. Only a generation 7 or later NVIDIA-style host advertising a valid `a=x-nv-general.clientPorts.mic:` SDP attribute is probed. Sunshine hosts are excluded: this is not a Sunshine microphone protocol extension.

The client sends an optional `SETUP streamid=mic/0/0` with the native client port pair `50002-50003`. A successful response supplies the server port through a single unambiguous Transport header. If that header is absent, the advertised SDP port is used. Malformed, duplicated, or ambiguous values leave the port unavailable. Rejection of the optional request leaves the normal handshake running. A transport failure can still consume the existing RTSP timeout; uninterrupted operation with every host version is not established by parser tests.

After a successful `LiStartConnection()`, `LiGetNativeMicPort()` returns the negotiated port, or zero when unavailable. State is cleared at connection start, connection stop, handshake entry, and handshake failure. The query is subject to the existing single-connection lifecycle; callers should not query concurrently with start or stop.

A negotiated port proves only that the host accepted this SETUP exchange. It does not prove microphone encryption, driver availability, or a working host recording endpoint. Playback audio encryption must not be treated as proof of microphone encryption.

## Scope and compatibility

| Peer | Behavior |
| --- | --- |
| Existing client with requestNativeMic zero | Existing session negotiation |
| Host without the native SDP attribute | No microphone SETUP |
| Sunshine | No native microphone SETUP |
| Native host with a valid advertisement | Optional SETUP attempted |
| Host declining microphone SETUP | Microphone unavailable; normal negotiation continues |

The native negotiation was derived from a local prototype. That prototype used host-side modifications; it does not establish compatibility with an unmodified host. This draft intentionally contains no host patch, privileged helper, binary, or instruction to alter a host executable. Current upstream host-version compatibility remains a review and testing task.

## Source review and verification

The bounded parsers in `src/NativeMicNegotiation.h` are shared directly with the regression tests. They distinguish absent, invalid, and valid values, reject ambiguous duplicates, and validate complete decimal ports and port ranges before exposing a destination. See `tests/README.md` for standalone test commands.

The ordinary CMake library build also compiles the handshake integration. Cross-platform CI and host-level connection/reconnect verification are required before promotion from draft. A parser test or successful build is not an end-to-end microphone test.

## Direction for a complete upstream feature

The intended user experience is an off-by-default microphone option in the normal client, explicit operating-system permission, visible capture status and an immediate stop control, paired with a supported host implementation. Users should obtain that through the projects' normal source and release processes.

For Sunshine, the next design step is agreement on advertised capabilities and authenticated microphone transport, followed by small host and client changes using that agreed protocol. Prior maintainer guidance recommends RTSP/SDP configuration and AES-GCM: https://github.com/LizardByte/Sunshine/pull/4078 . Another protocol proposal is open at https://github.com/moonlight-stream/moonlight-common-c/pull/123 . This native experiment does not establish or supersede that protocol.

Before a complete feature is ready, the host recording endpoint, authentication and replay protection, direction-specific nonce/key domains, bounded queues, capture shutdown, permission errors, and unsupported-peer behavior need implementation and verification. Synthetic tone tests can demonstrate the complete path without publishing private recordings or pairing data.

A convenience build should be produced by the project's build process from an identified source revision. Checksums and provenance help connect artifacts to source; official release signing and maintainer review provide a stronger distribution route than a separately shared Windows executable.
