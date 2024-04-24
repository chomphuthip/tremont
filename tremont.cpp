#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <vector>
#include <mutex>
#include <deque>
#include <chrono>

#include <Winsock2.h>
#include <stdint.h>
#include <time.h>

#pragma comment(lib, "Ws2_32.lib")

#include "tremont.h"

#include <iostream>

/*
	Rules:
	1. Everything is little endian. (best rule ever)
*/

class byte_stream {
	private:
		std::deque<byte> _data;

	public:
		void write(const byte* src, size_t size) {
			for (size_t i = 0; i < size; i++) { _data.push_back(src[i]); }
		}

		int read(byte* dest, size_t n) {
			size_t left_in_queue = _data.size();
			size_t to_read = (n > left_in_queue) ? left_in_queue : n;

			size_t i = 0;
			for (; i < to_read; i++) { 
				dest[i] = _data.front(); 
				_data.pop_front();
			}
			return static_cast<int>(i);
		}

		size_t size() {
			return _data.size();
		}
};

typedef uint32_t stream_id;
typedef uint32_t block_id;

typedef struct _Nexus Nexus;
typedef struct _Stream Stream;

#pragma pack(push, 1)
typedef struct _RTPHeader
{
	unsigned char         CC : 4;        /* CC field */
	unsigned char         X : 1;         /* X field */
	unsigned char         P : 1;         /* padding flag */
	unsigned char         version : 2;
	unsigned char         PT : 7;     /* PT field */
	unsigned char         M : 1;       /* M field */
	uint16_t              seq_num;      /* length of the recovery */
	uint32_t              TS;                   /* Timestamp */
	uint32_t              ssrc;
} rtp_header_t; //12 bytes
#pragma pack(pop)

typedef struct _Stream {
	sockaddr remote_addr = { 0 };
	int remote_addr_len = sizeof(sockaddr);

	Nexus* nexus = NULL;

	stream_id stream_id = 0;

	block_id remote_block_id = 0;
	block_id local_block_id = 0;

	byte_stream data_from;
	std::mutex* data_from_mu = new std::mutex;
	std::condition_variable* data_from_cv = new std::condition_variable;
} Stream;

typedef struct _Nexus {
	std::unordered_set<stream_id> desired_streams;
	std::mutex desired_streams_mu;
	std::condition_variable desired_streams_cv;

	std::unordered_set<stream_id> fufilled_streams;
	std::mutex fufilled_streams_mu;
	std::condition_variable fufilled_streams_cv;

	std::unordered_set<stream_id> marked_streams;
	std::mutex marked_streams_mu;
	std::condition_variable marked_streams_cv;

	std::unordered_map<stream_id, Stream> streams;

	std::unordered_map<stream_id, std::atomic<block_id>> ack_tracker;

	rtp_header_t rtp_header = { 0 };
	byte* key = NULL;
	size_t key_len = 0;

	SOCKET socket = NULL;
	std::mutex socket_mu;

	/*
		polled by the nexus thread

		0x1: normal
		0x2: hey thread we are shutting down, change me to 0x3 to ack
		0x3: ok im shutting down
	*/
	std::atomic<uint8_t> thread_ctrl = 0x0;
	std::thread* nexus_thread = NULL;
} Nexus;


/*
	forward dec of priv funcs so pub funcs can use them
*/
int _init_nexus_thread(Nexus* nexus);
void _send_stream_syn(stream_id id, sockaddr* addr, Nexus* nexus);
void _send_stream_fin(stream_id id, Nexus* nexus);
int _send_data_pkt(stream_id id, byte* buf, size_t buf_len, Nexus* nexus);
int _send_data_ack(stream_id s_id, block_id b_id, Nexus* nexus);
int _send_syn_ack(stream_id s_id, Nexus* nexus);
int _send_fin_ack(stream_id s_id, Nexus* nexus);
bool _stream_exists(stream_id id, Nexus* nexus);


/*
	TODO: Add error checking;
*/
int tremont_init_nexus(Nexus** new_nexus) {
	*new_nexus = new Nexus;
	return 0;
}

int tremont_rtp_nexus(rtp_header_t* rtp_header, Nexus* nexus) {
	memcpy(&nexus->rtp_header, rtp_header, sizeof(rtp_header));
	return 0;
}

int tremont_key_nexus(byte* key, size_t key_len, Nexus* nexus) {
	nexus->key = key;
	nexus->key_len = key_len;
	return 0;
}

int tremont_bind_nexus(SOCKET sock, Nexus* nexus) {
	nexus->socket = sock;
	nexus->thread_ctrl = 0x1;
	int res = _init_nexus_thread(nexus);
	return res;
}

int tremont_newid_nexus(stream_id* new_id, Nexus* nexus) {
	__time32_t t;
	if (_time32(&t) == -1) { return -1; }
	*new_id = t;
	return 0;
}

int tremont_verifyid_nexus(stream_id* id, Nexus* nexus) {
	return (nexus->streams.count(*id) > 0) ? -1 : 0;
}

int tremont_destroy_nexus(Nexus* nexus) {
	nexus->thread_ctrl = 0x2;
	while (nexus->thread_ctrl != 0x3) { std::this_thread::yield(); }
	nexus->nexus_thread->join();
	delete nexus;
	return 0;
}

int tremont_req_stream(stream_id id, sockaddr* addr, uint32_t timeout, Nexus* nexus) {
	std::unique_lock<std::mutex> desired_streams_lock(nexus->desired_streams_mu);
	nexus->desired_streams.insert(id);
	desired_streams_lock.unlock();

	std::unique_lock<std::mutex> fufilled_streams_lock(nexus->fufilled_streams_mu);
	if (timeout == 0) {
		_send_stream_syn(id, addr, nexus);
		nexus->fufilled_streams_cv.wait(fufilled_streams_lock,
			[&nexus, id] { return nexus->fufilled_streams.count(id) > 0; });
		return 0;
	}

	std::chrono::duration<uint32_t> _timeout(timeout);
	bool fufilled_in_time;
	
	_send_stream_syn(id, addr, nexus);
	fufilled_in_time = nexus->fufilled_streams_cv.wait_for(fufilled_streams_lock,
		_timeout,
		[&nexus, id] { return nexus->fufilled_streams.count(id) > 0; });
	if (!fufilled_in_time) return -1;
	
	return 0;
}

int tremont_accept_stream(stream_id id, uint32_t timeout, Nexus* nexus) {
	std::unique_lock<std::mutex> desired_streams_lock(nexus->desired_streams_mu);
	nexus->desired_streams.insert(id);
	desired_streams_lock.unlock();

	std::unique_lock<std::mutex> fufilled_streams_lock(nexus->fufilled_streams_mu);
	if (timeout == 0) {
		nexus->fufilled_streams_cv.wait(fufilled_streams_lock,
			[&nexus, id] { return nexus->fufilled_streams.count(id) > 0; });
		return 0;
	}

	std::chrono::duration<uint32_t> _timeout(timeout);
	bool fufilled_in_time;

	fufilled_in_time = nexus->fufilled_streams_cv.wait_for(fufilled_streams_lock,
		_timeout,
		[&nexus, id] { return nexus->fufilled_streams.count(id) > 0; });
	if (!fufilled_in_time) return -1;

	return 0;
}

int tremont_end_stream(stream_id id, Nexus* nexus) {
	std::lock_guard<std::mutex> lock(nexus->marked_streams_mu);
	nexus->marked_streams.insert(id);
	_send_stream_fin(id, nexus);
	return 0;
}

int tremont_send(stream_id id, byte* buf, size_t len, Nexus* nexus) {
	if (!_stream_exists(id, nexus)) return -2;

	block_id cur_block = ++nexus->streams[id].local_block_id;
	int res = -1;
	while (nexus->ack_tracker[id] != cur_block) {
		res = _send_data_pkt(id, buf, len, nexus);
	}
	return res;
}

int tremont_recv(stream_id id, byte* buf, size_t len, Nexus* nexus) {
	if (!_stream_exists(id, nexus)) return -2;

	Stream* stream = &nexus->streams[id];
	std::unique_lock<std::mutex> data_lock(*stream->data_from_mu);
	stream->data_from_cv->wait(data_lock,
		[stream, len] { return stream->data_from.size() > len; });
	int res = stream->data_from.read(buf, len);
	return res;
}

bool _stream_exists(stream_id id, Nexus* nexus) {
	return nexus->streams.count(id) != 0;
}

/*
	manages everthing
	
	when you send a packet and get back and ack, the nexus thread updates
		the ack tracker
	
	when your thread sends a syn, the nexus does the rest of the negotiation
	
	its kinda like a parent who just gets whatever their kids want
*/

#define SYN         0x01
#define FIN         0x02
#define DATA        0x03
#define DATA_ACK    0x04
#define CTRL_ACK    0x05

#define MAX_RECV 1440
#define RTP_HLEN 12

void _nexus_data(byte* raw, int bytes_in, Nexus* nexus);
void _nexus_data_ack(byte* raw, int bytes_in, Nexus* nexus);
void _nexus_syn(byte* raw, int bytes_in, sockaddr* remote_addr, Nexus* nexus);
void _nexus_ctrl_ack(byte* raw, int bytes_in, sockaddr* remote_addr, Nexus* nexus);
void _nexus_fin(byte* raw, int bytes_in, Nexus* nexus);
void _nexus_xor_trans(byte* data, size_t len, Nexus* nexus);
void _set_blocking(SOCKET socket, bool mode);

void _nexus_thread(Nexus* nexus) {
	char buffer[MAX_RECV];
	byte* data = (byte*)(buffer + RTP_HLEN);
	sockaddr remote_addr;
	int remote_addr_len = sizeof(sockaddr);
	ZeroMemory(&remote_addr, remote_addr_len);
	int bytes_in;
	_set_blocking(nexus->socket, false);

	while (nexus->thread_ctrl == 0x1) {
		bytes_in = recvfrom(nexus->socket, buffer, MAX_RECV, 0,
			&remote_addr, &remote_addr_len);
		if (bytes_in < 0) {
			if (WSAGetLastError() == WSAEWOULDBLOCK) { continue; }
			std::cerr << "Winsock err: " << WSAGetLastError() << std::endl;
			return;
		}
		_nexus_xor_trans(data, bytes_in - RTP_HLEN, nexus);
		uint32_t opcode = (uint32_t)data[0];
		switch (opcode) {
		case DATA:
			_nexus_data(data, bytes_in, nexus);
			break;
		case DATA_ACK:
			_nexus_data_ack(data, bytes_in, nexus);
			break;
		case SYN:
			_nexus_syn(data, bytes_in, &remote_addr, nexus);
			break;
		case CTRL_ACK: 
			_nexus_ctrl_ack(data, bytes_in, &remote_addr, nexus);
			break;
		case FIN:
			_nexus_fin(data, bytes_in, nexus);
			break;
		default:
			continue;
		}
	}
	nexus->thread_ctrl = 0x3;
	return;
}

int _init_nexus_thread(Nexus* nexus) {
	try { nexus->nexus_thread = new std::thread(_nexus_thread, nexus); }
	catch (std::bad_alloc& e) { perror(e.what()); return -1; }
	return 0;
}

#pragma pack(push, 1)
typedef struct {
	uint32_t opcode;
	stream_id s_id;
	block_id b_id;
} data_pkt_t;

#pragma pack(pop)


void _nexus_data(byte* raw, int bytes_in, Nexus* nexus) {
	data_pkt_t* data_pkt = (data_pkt_t*)raw;
	if (nexus->streams.count(data_pkt->s_id) == 0) return;
	Stream* stream = &nexus->streams[data_pkt->s_id];

	if (data_pkt->b_id != stream->remote_block_id + 1) { return; }
	_send_data_ack(data_pkt->s_id, data_pkt->b_id, nexus);
	
	std::unique_lock<std::mutex> stream_lock(*stream->data_from_mu);
	byte* chunk = raw + sizeof(data_pkt_t);
	stream->data_from.write(chunk, bytes_in - sizeof(data_pkt_t));
	stream->data_from_cv->notify_all();
}

#pragma pack(push, 1)
typedef struct {
	uint32_t opcode;
	stream_id s_id;
	block_id b_id;
} data_ack_pkt_t;

#pragma pack(pop)

void _nexus_data_ack(byte* raw, int bytes_in, Nexus* nexus) {
	data_ack_pkt_t* ack_pkt = (data_ack_pkt_t*)raw;
	if (nexus->streams.count(ack_pkt->s_id) == 0) return;

	if (ack_pkt->b_id != nexus->ack_tracker[ack_pkt->s_id] + 1) { return; }
	nexus->ack_tracker[ack_pkt->s_id]++;
}

#pragma pack(push, 1)
typedef struct {
	uint32_t opcode;
	stream_id s_id;
} syn_pkt_t;

#pragma pack(pop)

void _nexus_syn(byte* raw, int bytes_in, sockaddr* remote_addr, Nexus* nexus) {
	syn_pkt_t* syn_pkt = (syn_pkt_t*)raw;
	
	std::unique_lock<std::mutex> d_streams_lock(nexus->desired_streams_mu);
	if (nexus->desired_streams.count(syn_pkt->s_id) == 0) return;
	nexus->desired_streams.erase(syn_pkt->s_id);
	nexus->desired_streams_cv.notify_all();

	nexus->streams.emplace(syn_pkt->s_id, Stream());
	Stream* new_stream = &nexus->streams[syn_pkt->s_id];

	memcpy(&new_stream->remote_addr, remote_addr, sizeof(sockaddr));
	new_stream->remote_addr_len = sizeof(sockaddr);
	new_stream->nexus = nexus;
	new_stream->stream_id = syn_pkt->s_id;

	nexus->ack_tracker[syn_pkt->s_id] = 0;

	_send_syn_ack(new_stream->stream_id, nexus);

	std::unique_lock<std::mutex> f_streams_lock(nexus->fufilled_streams_mu);
	nexus->fufilled_streams.insert(syn_pkt->s_id);
	nexus->fufilled_streams_cv.notify_all();
}

#pragma pack(push, 1)
typedef struct {
	uint32_t opcode;
	uint32_t replying_opcode;
	stream_id s_id;
} ctrl_ack_pkt_t;

#pragma pack(pop)

void _nexus_ctrl_ack(byte* raw, int bytes_in, sockaddr* remote_addr, Nexus* nexus) {
	ctrl_ack_pkt_t* ack_pkt = (ctrl_ack_pkt_t*)raw;

	if (ack_pkt->replying_opcode == SYN) {
		std::unique_lock<std::mutex> d_streams_lock(nexus->desired_streams_mu);
		if (nexus->desired_streams.count(ack_pkt->s_id) == 0) return;
		nexus->desired_streams.erase(ack_pkt->s_id);
		nexus->desired_streams_cv.notify_all();

		nexus->streams.emplace(ack_pkt->s_id, Stream());
		Stream* new_stream = &nexus->streams[ack_pkt->s_id];

		memcpy(&new_stream->remote_addr, remote_addr, sizeof(remote_addr));
		new_stream->nexus = nexus;
		new_stream->stream_id = ack_pkt->s_id;

		nexus->ack_tracker[ack_pkt->s_id] = 0;

		std::lock_guard<std::mutex> f_streams_lock(nexus->fufilled_streams_mu);
		nexus->fufilled_streams.insert(ack_pkt->s_id);
		nexus->fufilled_streams_cv.notify_all();
	}
	if (ack_pkt->replying_opcode == FIN) {
		if (nexus->streams.count(ack_pkt->s_id) == 0) return;
		nexus->streams.erase(ack_pkt->s_id);
	}
}

#pragma pack(push, 1)
typedef struct {
	uint32_t opcode;
	stream_id s_id;
} fin_pkt_t;

#pragma pack(pop)

void _nexus_fin(byte* raw, int bytes_in, Nexus* nexus) {
	fin_pkt_t* fin_pkt = (fin_pkt_t*)raw;
	Stream* stream = &nexus->streams[fin_pkt->s_id];
	delete stream->data_from_mu;
	delete stream->data_from_cv;
	nexus->streams.erase(fin_pkt->s_id);
	_send_fin_ack(fin_pkt->s_id, nexus);
}

void _nexus_xor_trans(byte* data, size_t len, Nexus* nexus) {
	if (nexus->key == NULL || nexus->key_len == 0) return;
	for (size_t i = 0; i < len; i++) {
		data[i] = data[i] ^ nexus->key[i % nexus->key_len];
	}
}

void _set_blocking(SOCKET sock, bool mode) {
	u_long mode_code = mode ? 0 : 1;
	ioctlsocket(sock, FIONBIO, &mode_code);
}

char* _craft_rtp_pkt(byte* data, size_t data_len, size_t* res_size_out, Nexus* nexus) {
	*res_size_out = data_len + sizeof(rtp_header_t);
	byte* result = (byte*)malloc(*res_size_out);
	if (result == NULL) return NULL;

	memcpy(result, &nexus->rtp_header, sizeof(rtp_header_t));
	memcpy(result + sizeof(nexus->rtp_header), data, data_len);

	_nexus_xor_trans(result + sizeof(rtp_header_t), data_len,
		nexus);
	
	return (char*)result;
}

void _send_stream_syn(stream_id id, sockaddr* addr, Nexus* nexus) {
	syn_pkt_t p;
	p.opcode = SYN;
	p.s_id = id;

	std::lock_guard<std::mutex> sock_lock(nexus->socket_mu);
	sockaddr remote_addr;
	int remote_addr_len = sizeof(remote_addr);
	
	size_t rtp_pkt_len;
	char* rtp_pkt = _craft_rtp_pkt((byte*)&p, sizeof(syn_pkt_t),
		&rtp_pkt_len, nexus);
	
	int res = sendto(nexus->socket, rtp_pkt, static_cast<int>(rtp_pkt_len), 0,
		addr, sizeof(sockaddr));

	free(rtp_pkt);
}

void _send_stream_fin(stream_id id, Nexus* nexus) {
	fin_pkt_t p;
	p.opcode = FIN;
	p.s_id = id;

	std::lock_guard<std::mutex> sock_lock(nexus->socket_mu);
	Stream* stream = &nexus->streams[id];
	sockaddr remote_addr;
	int remote_addr_len = sizeof(remote_addr);

	size_t rtp_pkt_len;
	char* rtp_pkt = _craft_rtp_pkt((byte*)&p, sizeof(fin_pkt_t),
		&rtp_pkt_len, nexus);

	int res = sendto(nexus->socket, rtp_pkt, static_cast<int>(rtp_pkt_len), 0,
		&stream->remote_addr, sizeof(sockaddr));

	free(rtp_pkt);
}

int _send_data_pkt(stream_id id, byte* buf, size_t buf_len, Nexus* nexus) {
	std::lock_guard<std::mutex> sock_lock(nexus->socket_mu);

	data_pkt_t p;
	p.opcode = DATA;
	p.s_id = id;
	p.b_id = nexus->streams[id].local_block_id;

	size_t offset = 0;
	size_t temp_size = sizeof(data_pkt_t) + buf_len;
	byte* temp = (byte*)malloc(temp_size);
	if (temp == NULL) return -1;

	memcpy(temp + offset, &p, sizeof(data_pkt_t));
	offset += sizeof(data_pkt_t);

	memcpy(temp + offset, buf, buf_len);

	size_t rtp_pkt_len;
	char* rtp_pkt = _craft_rtp_pkt(temp, temp_size, &rtp_pkt_len, nexus);

	Stream* stream = &nexus->streams[id];
	
	int res = sendto(nexus->socket, rtp_pkt, static_cast<int>(rtp_pkt_len), 0,
		&stream->remote_addr, stream->remote_addr_len);

	free(temp);
	free(rtp_pkt);
	return 0;
}

int _send_data_ack(stream_id s_id, block_id b_id, Nexus* nexus) {
	std::lock_guard<std::mutex> sock_lock(nexus->socket_mu);

	data_ack_pkt_t p;
	p.opcode = DATA_ACK;
	p.s_id = s_id;
	p.b_id = b_id;

	size_t rtp_pkt_len;
	char* rtp_pkt = _craft_rtp_pkt((byte*)&p, sizeof(data_ack_pkt_t),
		&rtp_pkt_len, nexus);

	Stream* stream = &nexus->streams[s_id];
	sendto(nexus->socket, rtp_pkt, static_cast<int>(rtp_pkt_len), 0,
		&stream->remote_addr, stream->remote_addr_len);

	free(rtp_pkt);
	return 0;
}

int _send_syn_ack(stream_id s_id, Nexus* nexus) {
	std::lock_guard<std::mutex> sock_lock(nexus->socket_mu);

	ctrl_ack_pkt_t p;
	p.opcode = CTRL_ACK;
	p.replying_opcode = SYN;
	p.s_id = s_id;

	size_t rtp_pkt_len;
	char* rtp_pkt = _craft_rtp_pkt((byte*)&p, sizeof(ctrl_ack_pkt_t),
		&rtp_pkt_len, nexus);

	Stream* stream = &nexus->streams[s_id];
	sendto(nexus->socket, rtp_pkt, static_cast<int>(rtp_pkt_len), 0,
		&stream->remote_addr, stream->remote_addr_len);

	free(rtp_pkt);
	return 0;
}

int _send_fin_ack(stream_id s_id, Nexus* nexus) {
	std::lock_guard<std::mutex> sock_lock(nexus->socket_mu);

	ctrl_ack_pkt_t p;
	p.opcode = CTRL_ACK;
	p.replying_opcode = FIN;
	p.s_id = s_id;

	size_t rtp_pkt_len;
	char* rtp_pkt = _craft_rtp_pkt((byte*)&p, sizeof(ctrl_ack_pkt_t),
		&rtp_pkt_len, nexus);

	Stream* stream = &nexus->streams[s_id];
	sendto(nexus->socket, rtp_pkt, static_cast<int>(rtp_pkt_len), 0,
		&stream->remote_addr, stream->remote_addr_len);

	free(rtp_pkt);
	return 0;
}