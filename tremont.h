// tremont: Obfuscated, Reliable Streaming over RTP
#pragma once

#include<WinSock2.h>
#include<stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	typedef struct _Nexus Nexus;
	typedef Nexus Tremont_Nexus;

	typedef uint32_t stream_id;
	typedef stream_id tremont_stream_id;

#pragma pack(push, 1)
	typedef struct _rtp_header_t
	{
		unsigned char         CC : 4;     /* CC field */
		unsigned char         X : 1;      /* X field */
		unsigned char         P : 1;      /* padding flag */
		unsigned char         version : 2;
		unsigned char         PT : 7;     /* PT field */
		unsigned char         M : 1;      /* M field */
		uint16_t              seq_num;    /* length of the recovery */
		uint32_t              TS;         /* Timestamp */
		uint32_t              ssrc;
	} rtp_header_t;
#pragma pack(pop)

	/*
		Create a nexus.
	*/
	int tremont_init_nexus(Tremont_Nexus** new_nexus);

	/*
		Sets the key that the nexus will use.
	*/
	int tremont_key_nexus(char* key, size_t key_len, Tremont_Nexus* nexus);

	/*
		Sets key_ptr to the location of the key
		Sets key_len to the size of the key
	*/
	int tremont_getkey_nexus(char** key_ptr, size_t* key_len, Tremont_Nexus* nexus);

	/*
		Binds a nexus to a UDP socket.
	*/
	int tremont_bind_nexus(SOCKET sock, Tremont_Nexus* nexus);

	/*
		Sets the max size of an RTP packet.
	*/
	int tremont_set_size(uint32_t size, Tremont_Nexus* nexus);

	/*
		Copies the header of a given RTP struct and uses it.
	*/
	int tremont_set_header(rtp_header_t* rtp_header, Tremont_Nexus* nexus);

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
		Associates a stream_id with a password.
		Password is used to authenticate the initial connection.
		Call before req/accept.
	*/
	int tremont_auth_stream(tremont_stream_id id, char* buf, size_t buf_len, Tremont_Nexus* nexus);

	/*
		Sends a stream request to a remote nexus.
		Blocks until timeout. If timeout = 0, blocks until someone requests
	*/
#ifdef __cplusplus
	int tremont_req_stream(tremont_stream_id id, sockaddr* addr, uint32_t timeout, Tremont_Nexus* nexus);
#else
	int tremont_req_stream(tremont_stream_id id, struct sockaddr* addr, uint32_t timeout, Tremont_Nexus* nexus);
#endif

	/*
		Accepts a stream from the nexus.
		Blocks until timeout.
	*/
	int tremont_accept_stream(tremont_stream_id id, uint32_t timeout, Tremont_Nexus* nexus);

	/*
		Marks the stream as desired, but does not block or timeout.
		Usually used when setting up callbacks.
	*/
	int tremont_desire_stream(tremont_stream_id id, Tremont_Nexus* nexus);

	/*
		Registers a callback that takes a tremont_cb_param struct.
		Callback is called upon establishing a stream with the matching stream id.
		Callback is unregistered after call.

		-- ALLOCATE PARAMS USING MALLOC --
		Tremont will call free(3) on params.
	*/
	struct tremont_cb_param {
		tremont_stream_id stream_id;
		void* params;
	};
	typedef void (*tremont_cb)(struct tremont_cb_param* param);
	int tremont_cb_stream(tremont_stream_id id, tremont_cb cb, void* params, Tremont_Nexus* nexus);
	
	/*
		Removes the callback associated with the stream_id.
		Will also attempt to free params.
	*/
	int tremont_rmcb_stream(tremont_stream_id id, Tremont_Nexus* nexus);


	/*
		Returns a pointer to the remote addr of a stream
	*/
#ifdef __cplusplus
	int tremont_getaddr_stream(tremont_stream_id id, sockaddr* dest, Tremont_Nexus* nexus);
#else
	int tremont_getaddr_stream(tremont_stream_id id, struct sockaddr* dest, Tremont_Nexus* nexus);
#endif

	/*
		Ends a stream.
	*/
	int tremont_end_stream(tremont_stream_id id, Tremont_Nexus* nexus);

	/*
		Sets stream options.
		OPT_NONBLOCK: 0 for blocking, 1 for nonblocking. 0 is default
		OPT_TIMEOUT: 0 for no timeout, new_val is by seconds
	*/

	#define OPT_NONBLOCK 0x0
	#define OPT_TIMEOUT  0x1

	int tremont_opts_stream(stream_id id, uint8_t opt, uint8_t new_val, Tremont_Nexus* nexus);

	/*
		Returns size of input buffer or -1 if the stream doesn't exist anymore;
	*/
	int tremont_poll_stream(tremont_stream_id id, Tremont_Nexus* nexus);

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

#ifdef __cplusplus
}
#endif