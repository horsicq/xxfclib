The method 94 decoder is a native C adaptation of packMP3 v1.0g (22 January
2016), Copyright 2010–2016 Ratisbon University and Matthias Stirner.

Source: https://github.com/packjpg/packMP3
Reference revision: e61c11941552f4ffe6e219a847f441d9520d2e50
Original files: packmp3.cpp, aricoder.cpp/.h, bitops.cpp/.h, huffmp3.cpp/.h,
huffmp3tbl.h, pmp3tbl.h and pmp3bitlen.h.

The original package permits use under LGPL version 3 or any later version.
COPYING.LESSER contains LGPLv3; COPYING contains the incorporated GPLv3 text
(the standard license text from the official GCC source mirror).

Changes for xxfclib: only lossless reconstruction is retained; native C
structures and explicit functions replace C++ classes; all mutable state is
per call; memory I/O replaces file/process I/O; allocations use xx_mem_* with
an aggregate 256 MiB limit and cleanup on every exit; arithmetic EOF padding
is bounded; model, Huffman, frame, region and output bounds are checked.
No C++ runtime, external decoder process, or third-party runtime is required.

The accepted stream signature is packMP3 v1.0's MS0A. Original MPEG-1 Layer
III files at 32, 44.1 and 48 kHz are restored byte for byte, including mono/
stereo, variable and fixed bitrates, original ID3 tags, CRC, bit reservoir,
stuffing, ancillary data and padding. MPEG-2/2.5 and future packMP3 stream
versions are outside this codec profile and are rejected.
