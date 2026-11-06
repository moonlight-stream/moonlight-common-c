# Native microphone session negotiation

This shared-library change provides opt-in NVIDIA GameStream microphone negotiation and session endpoint access. The companion macOS Qt changes provide capture, consent, status and stop handling; the companion Windows package provides the version-pinned host integration, optional installer and manual effects/rollback documentation. Those companions are linked in the pull-request description. This library itself does not capture or transmit voice, create a Windows endpoint, or implement a new microphone cipher.

## Session behavior

Initialize STREAM_CONFIGURATION with LiInitializeStreamConfiguration(), then explicitly set requestNativeMic before connecting. Zero is the default and retains the existing handshake. Only opted-in NVIDIA-style generation 7+ hosts are probed; Sunshine is excluded. The tested native host advertises a=x-nv-general.clientPorts.mic:0 before SETUP, so zero or absent SDP is deferred negotiation, not a destination. Malformed or duplicate attributes disable the optional path.

Optional SETUP streamid=mic/0/0 advertises native client ports 50002-50003. A 200 response supplies the server port through a single unambiguous Transport header. When that entire header is absent, a valid positive SDP port may be used; zero/absent SDP cannot provide a fallback. Malformed, duplicated or ambiguous Transport values leave microphone unavailable. Optional rejection leaves normal handshake progression intact. A transport failure still consumes existing RTSP timeout and has not been proven harmless on every native host version.

After successful LiStartConnection(), LiGetNativeMicPort() returns the negotiated destination or zero. LiGetNativeMicAddress() copies the resolved session IPv4 address, avoiding a separate DNS destination choice. Native state clears at connection start, connection stop, handshake entry and handshake failure. Query only between successful start and stop, under the existing single-connection lifecycle; do not race queries with lifecycle changes.

LiIsNativeMicAudioEncrypted() reflects the tested native host's negotiated legacy audio CBC mode. It is not independent authenticated microphone negotiation or a Sunshine capability. The Qt client refuses implicit plaintext forwarding. A port/mode query alone does not establish a working recording endpoint or delivery.

## Compatibility and security

Historical end-to-end evidence covers native GameStream 7.1.437.0, GeForce Experience 3.25.1.27 and native audio 4.37.0.0 with the documented Windows modifications. Other versions, unmodified hosts, and proposed Sunshine microphone protocols are unverified. General generation 7 opt-in eligibility is broader than this tested set and is not a claim of broad compatibility.

Historical pre-SETUP SDP showed port 0 followed by successful SETUP with port 48002 and synthetic delivery. The old collector was not Content-Length framed; that observation is not a freshly bounded protocol capture. Current parser tests consume bounded spans and distinguish absent, deferred-zero, invalid and valid data.

Native CBC is encryption without packet authentication. It follows the existing identifier plus 16-bit RTP sequence IV construction: repeats after 65,536 packets and overlaps playback/microphone IV domains. The full client/host documentation describes these limitations and the trusted-network/tunnel boundary. No new authenticated protocol is claimed. Prior AES-GCM guidance on the separate Sunshine proposal (https://github.com/LizardByte/Sunshine/pull/4078) is context, not an asserted maintainer requirement for this NVIDIA contribution. The separate protocol proposal https://github.com/moonlight-stream/moonlight-common-c/pull/123 is not silently substituted for native compatibility.

The Windows companion must be evaluated as privileged, version-specific integration. It locally patches a verified original and performs native runtime handoff; a source build/manual route is provided alongside the optional executable. No proprietary NVIDIA binary or secret is redistributed. See the companion's exact effects, hashes, persistence and rollback before activation.

## Verification

Full common-c Debug and Release builds passed on macOS arm64 against OpenSSL. The production header's 59 parser table cases plus null-output checks passed through CTest and sanitizers. Independent production-block tests passed 24 reply/state scenarios and 7 eligibility gates with controlled SETUP replies; these do not execute a complete network handshake. Exact host/version, current candidate fixtures and historical live evidence remain separated in the full contribution's test matrix.

Cross-platform CI and the revised Windows candidate's native acceptance, reboot and full uninstall/recovery require their own evidence. Source builds and fixtures are not presented as proof of every runtime case. The regular pull requests ask maintainers to review the complete, explicitly scoped implementation and its preferred distribution route.
