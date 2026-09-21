# DoP -> DSF self-test

Validates the SACD/DSP -> DSF path independently of foobar2000 and of the renderer.

`run.sh` generates a real DSD64 stereo signal (1 kHz left, 2 kHz right, second-order
sigma-delta), packs it as DoP exactly as the DoP 1.1 specification describes, feeds it
through the plugin's **own** `SacdDecoder::unpackDop()` and `DsfWriter` (extracted from the
sources at run time, so it always tests the current code), and then checks:

* DSF structure: advertised sizes, whole 4096-byte blocks, zero padding of the last block;
* the payload is **bit-exact** against the original DSD stream;
* an independent `ffmpeg` decode has a clean tone with in-band SNR above 45 dB.

Why it exists: writing the DoP bytes in the wrong order and without reversing the bits
still produces a tone (only the noise-shaped quantisation noise is scrambled), so the
mistake is invisible to a listening test of the tone but costs ~30-45 dB of SNR.

Requires `g++` (C++17), `python3` with `numpy`, and `ffmpeg`. Only the SDK types used by
`unpackDop()` are stubbed (`stub.h`); `audio_math::convert_to_int24` is a faithful copy of
`pfc/audio_math.cpp` (it multiplies the scale by 0x800000).
