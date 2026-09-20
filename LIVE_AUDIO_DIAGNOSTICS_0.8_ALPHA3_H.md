# v0.8 Alpha 3 H — Live audio and conversion diagnostics

The SACD DLNA Status / Diagnostics UI reports the active music object and the processing path used to produce the HTTP/DLNA payload.

Displayed live information:

- Music: title, artist and album.
- Source: file format, original file size, source sample rate, channels and bits per sample.
- Output: served format, generated/served file size, DSD rate, channels and bits per sample.
- Speed: measured TX in Mbit/s, required payload bandwidth and effective `x realtime` transmission rate.
- Pipeline: native DSD, SACD ISO → foo_input_sacd → DSF, or PCM/DSD → DSD Processor → DSF/DSD.
- Conversion: `NO CONVERSION`, `SACD DECODE / CACHE`, `DSP CONVERTING`, or `DSP OUTPUT / CACHED`.

The UI also shows the preparation state while a DSF cache is being generated, so a long-running SACD ISO or DSD Processor operation is visible before the renderer starts downloading the final object.

`x realtime` is a network metric calculated from measured transmitted bytes/sec versus the estimated DSD payload bytes/sec. It is not a measurement of the T+A renderer's internal playback clock.
