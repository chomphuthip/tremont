# tremont

S/RTP has some interesting properties that makes it seem like a good transport medium for offensive security implants:
* SRTP is a profile of RTP. Having SRTP being a profile of RTP rather than a whole different type of packet makes it hard to identify which data is SRTP.
* No one size fits all way to decrypt like HTTPS. An attacker would need to know the signaling protocol, which is different across different RTP products.
* RTP has high potential throughput. RTP sending h265 video frames has packets up to 1440 bytes long, sending multiple frames a second.

Tremont is a simple C++ library that trys to take advantage of RTP to reliably send obfuscated data.
