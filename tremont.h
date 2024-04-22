// tremont: Sneaky, Reliable Streaming over RTP
#pragma once

#include<stdint.h>

#include<WinSock2.h>


/*
	Bound to a UDP socket.
	Manages streams.
*/
typedef Nexus Tremont_Nexus;
typedef stream_id tremont_stream_id;

/*
	Create a nexus.
*/
int tremont_init_nexus(Tremont_Nexus* new_nexus);

/*
	Sets the key that the nexus will use.
*/
int tremont_key_nexus(char* key, size_t key_len, Tremont_Nexus* nexus);

/*
	Binds a nexus to a UDP socket.
*/
int tremont_bind_nexus(SOCKET sock, Tremont_Nexus* nexus);

/*
	Gets a unused stream ID.
*/
int tremont_newid_nexus(tremont_stream_id* new_id, Tremont_Nexus* nexus);

/*
	Checks if a stream ID is in use.
*/
int tremont_verifyid_nexus(tremont_stream_id* id, Tremont_Nexus* nexus);

/*
	Destroys the nexus and cleans up
*/
int tremont_destroy_nexus(Tremont_Nexus* nexus);

/*
	Sends a stream request to a remote nexus.
	Blocks until timeout.
*/
int tremont_req_stream(tremont_stream_id id, sockaddr_in* addr, uint32_t timeout, Tremont_Nexus* nexus);

/*
	Accepts a stream from the nexus.
	Blocks until timeout.
*/
int tremont_accept_stream(tremont_stream_id id, sockaddr_in* addr, uint32_t timeout, Tremont_Nexus* nexus);

/*
	Ends a stream.
*/
int tremont_end_stream(tremont_stream_id id, Tremont_Nexus* nexus);

/*
	Sends data.
	Blocks while data is being sent and ack'd.
*/
int tremont_send(tremont_stream_id id, byte* buf, size_t len, Tremont_Nexus* nexus);

/*
	Recieves data.
	Blocks while data is being received.
*/
int tremont_recv(tremont_stream_id id, byte* buf, size_t len, Tremont_Nexus* nexus);