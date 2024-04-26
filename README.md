# tremont

Tremont is a simple C++ library that uses RTP to reliably send obfuscated data.

S/RTP has some interesting properties that make it a strong choice for offensive security communications:

* ***Enormous Cost to Defenders***: RTP is used for all sorts of critical communication within an enterprise. Disabling RTP or even investigating RTP traffic can be extremely expensive for defenders, as they would either have to come up with complex defense measures or accept the risk and act reactively, potentially sifting through terabytes of traffic logs.
  
* ***Protocol Flexibility***: Different RTP products use different signaling protocols to negotiate keys for SRTP, meaning that there is no one-size-fits-all way to decrypt traffic, unlike HTTPS.  While Tremont uses a weak symmetric XOR encryption scheme, being able to identify the payload as either being RFC 3711 SRTP or just a XORed payload is nontrivial.

* ***High Throughput***: RTP sending h265 video frames has packets up to 1440 bytes long, sending multiple frames a second. Unlike HTTP, where file transfers can be uniquely identified from a bigger traffic capture, RTP is a consistant stream of a packets with a fixed size.
