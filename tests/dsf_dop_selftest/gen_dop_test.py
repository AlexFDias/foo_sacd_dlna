import numpy as np
# --- 0.5 s of stereo DSD64: 1 kHz left, 2 kHz right (second-order sigma-delta modulator)
FS = 2822400
N  = FS // 2
def sdm(freq, amp):
    t = np.arange(N) / FS
    x = amp * np.sin(2*np.pi*freq*t)
    bits = np.empty(N, dtype=np.uint8)
    v1 = v2 = 0.0; y = 1.0
    for i in range(N):
        v1 += x[i] - y
        v2 += v1 - y
        y = 1.0 if v2 >= 0 else -1.0
        bits[i] = 1 if y > 0 else 0
    return bits
L = sdm(1000, 0.4); R = sdm(2000, 0.4)
def pack_msb_first(bits):           # OLDEST bit in the MSB of each byte (DFF/DoP convention)
    return np.packbits(bits, bitorder='big')
bl, br = pack_msb_first(L), pack_msb_first(R)
nbytes = len(bl) - (len(bl) % 2)
bl, br = bl[:nbytes], br[:nbytes]
np.save('dsd_left_msb.npy', bl); np.save('dsd_right_msb.npy', br)
# --- pack as DoP (spec 1.1): [8-bit marker][16 DSD bits, OLDEST byte first]
frames = nbytes // 2
marker = np.where(np.arange(frames) % 2 == 0, 0x05, 0xFA).astype(np.uint32)
def dop(b):
    older = b[0::2].astype(np.uint32); newer = b[1::2].astype(np.uint32)
    w = (marker << 16) | (older << 8) | newer
    w = w.astype(np.int64); w[w >= (1 << 23)] -= (1 << 24)        # signed int24
    return w.astype(np.float64) / 8388608.0                       # normalised audio_sample
inter = np.empty(frames * 2, dtype=np.float64)
inter[0::2] = dop(bl); inter[1::2] = dop(br)
inter.tofile('dop_input.f64')
print("DoP frames:", frames, "| bytes per channel:", nbytes, "| 4096-byte blocks:", nbytes/4096)
