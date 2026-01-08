Github: https://github.com/ar-in0/Squig

#### Resources
https://www.videosdk.live/blog/webrtc-vs-rtmp  
https://www.gumlet.com/learn/rtsp-vs-rtmp/  

RTMP spec (IMP): https://rtmp.veriskope.com/docs/spec/  

rtmp intro: https://rtmp.veriskope.com/docs/overview/ 


#### Notes
0. Stream from Iphone 13 to laptop running Linux 6.8  
-- Larix on Iphone to broadcast. (rtmp client)
-- Custom processing on laptop. (rtmp server)
See also: OBS Studio, OBS Camera for IOS (both open source)

![Packet capture from Larix to Squig Server](image.png)
- The unknowns have got to be video data. 
- There are some chunk IDs. See rtmp intro for meaning.


#### Misc
##### Endianness and Network Byte Order
- Recall endianness is a property of the ISA. Defined by how the CPU’s load/store instructions   
assemble or disassemble bytes when moving data between memory and registers.    
```
// Example: LOAD16 R0, [0x1000], i.e. pseudocode for
// loading 2 bytes at address 0x1000 into some register.

// In a little-endian ISA such as x86, the 
// actual instruction would be something like:
// store the 2-byte value at address 0x1000 in register AX
// i.e. mov eax [0x1000] 
// CPU implements this instruction via:
// ax = mem[0x1000] + (mem[0x1001] << 8) (0x0018 + 0x0022 << 8 = 0x2200)
//
// In the corresponding situation with a big-endian
// ISA, the CPU would perform:
// i.e. the value is as if the MSB is on the extreme left
// of the 2-byte sequence.
// R0 = (mem[0x1000] << 8) + mem[0x1001]
``` 
The network: Transmits bytes in some sequence.  
Network order: For multi-byte integers, the most significant BYTE (not bit) appears first in the stream.  
`htons`:  
```
uint16_t x = 0x1822;      // host-order value (may be used on a LE or BE ISA).
// On a little-endian system, htons() swaps the bytes of the value
// so that when the value is stored and transmitted, the most significant BYTE (not bit) is sent first.
// On little-endian: y == 0x2218
//
// On a big-endian system, htons() does nothing
// On big-endian: y == 0x1822
uint16_t y = htons(x);   // value transformed
```


#### Build
```
cmake -S /home/armaan/Fun-CS/Squig/submodules/EasyRTMP -B /home/armaan/Fun-CS/Squig/submodules/EasyRTMP/build
// add -DUSE_OPENSSL ON to enable openssl
// build as .ar is better, small size, good for embedded to (in case I build custom clients)

cmake --build /home/armaan/Fun-CS/Squig/submodules/EasyRTMP/build

doxygen Doxyfile : generates index.html for docs, open in browser.

```