"""Checks the DSF written by the harness: structure, bit-exactness against the
original DSD stream, and in-band SNR of an independent ffmpeg decode."""
import numpy as np, struct, subprocess, sys
rev = np.array([int('{:08b}'.format(i)[::-1], 2) for i in range(256)], dtype=np.uint8)
L = np.load('dsd_left_msb.npy'); R = np.load('dsd_right_msb.npy')
b = open('out.dsf', 'rb').read()
assert b[0:4] == b'DSD ' and b[28:32] == b'fmt ', "bad DSF magic"
advertised, = struct.unpack('<Q', b[12:20])
fmt_size, = struct.unpack('<Q', b[32:40])
ver, fid, ctype, ch, rate, bps, spc, blk, _ = struct.unpack('<IIIIIIQII', b[40:80])
dpos = 28 + fmt_size; assert b[dpos:dpos+4] == b'data'
data_size, = struct.unpack('<Q', b[dpos+4:dpos+12]); data = np.frombuffer(b[dpos+12:], dtype=np.uint8)
ok = True
def check(name, cond):
    global ok; ok &= bool(cond); print(('PASS  ' if cond else 'FAIL  ') + name)
check("advertised file size == real size", advertised == len(b))
check("data chunk size == 12 + data bytes", data_size - 12 == len(data))
check("data is a whole number of 4096-byte blocks per channel", len(data) % (2 * 4096) == 0)
check("bits per sample field == 1 (LSB first)", bps == 1)
if len(data) % (2 * 4096) == 0:
    blocks = data.reshape(-1, 2, 4096); c0 = blocks[:, 0, :].reshape(-1); c1 = blocks[:, 1, :].reshape(-1); n = len(L)
    check("left channel is bit-exact (bit-reversed DoP payload)", np.array_equal(rev[c0[:n]], L))
    check("right channel is bit-exact", np.array_equal(rev[c1[:n]], R))
    check("final block is padded with zeros", not c0[n:].any() and not c1[n:].any())
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', 'out.dsf', '-ar', '44100', '-ac', '2', '-c:a', 'pcm_f32le', 'decoded.wav'], check=True)
raw = open('decoded.wav', 'rb').read(); i = raw.find(b'data') + 8
a = np.frombuffer(raw[i:i + (len(raw) - i) // 8 * 8], dtype='<f4').reshape(-1, 2).astype(float)[3000:3000 + 16384]
for c, f0 in ((0, 1000), (1, 2000)):
    x = a[:, c]; w = np.hanning(len(x)); P = np.abs(np.fft.rfft(x * w)) ** 2; fr = np.fft.rfftfreq(len(x), 1 / 44100)
    tone = np.abs(fr - f0) < 6 * fr[1]; band = (fr > 20) & (fr < 20000) & ~tone
    snr = 10 * np.log10(P[tone].sum() / P[band].sum())
    print(f"      channel {'LR'[c]}: {f0} Hz tone, in-band SNR {snr:.1f} dB")
    check(f"channel {'LR'[c]} SNR above 45 dB (wrong bit/byte order gives ~25 dB)", snr > 45)
sys.exit(0 if ok else 1)
