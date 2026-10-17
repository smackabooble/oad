// Browser implementation of the ENet API subset used by 0 A.D. (see enet/enet.h).
//
//  * The host's own client and the server live in the same page: they are linked directly (no sockets).
//  * Other players are reached over WebRTC data channels.  Those exist only on the browser's main thread,
//    so the engine threads hand work to site/net.js through MAIN_THREAD_ASYNC_EM_ASM.  The other direction
//    never calls into C++ (the browser's main thread has no valid C thread state): net.js writes records into
//    a shared-memory ring buffer (g_ring) and wakes the engine threads with an atomic notify; enet_host_service()
//    drains the ring.
#include <enet/enet.h>
#include <emscripten.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <set>
#include <climits>
#include <string>
#include <vector>

namespace
{
std::mutex g_mutex;                       // protects g_hosts, g_listener, g_codes
std::set<ENetHost*> g_hosts;
ENetHost* g_listener = nullptr;
std::vector<std::string> g_codes;         // game codes handed out by enet_address_set_host
std::atomic<uint32_t> g_nextRemoteIp{1};

const enet_uint32 CODE_TAG = 0xFE000000u; // address.host = CODE_TAG | (index + 1) marks a game code


// ------------------------------------------------------------------ shared ring (net.js -> engine)
constexpr uint32_t RING_CAP = 1u << 21;         // 2 MiB
alignas(16) uint8_t g_ring[RING_CAP];
alignas(16) volatile uint32_t g_ctl[4];         // [0]=head (written by JS), [1]=tail (engine), [2]=wake counter
std::mutex g_drainMutex;
std::atomic<bool> g_inited{false};
std::map<std::pair<ENetHost*, int>, std::vector<uint8_t>> g_rx;   // reassembly of fragmented messages (g_mutex)

enum RecType : uint32_t { R_PAD = 0, R_INCOMING = 1, R_CONNECTED = 2, R_CLOSED = 3, R_DATA = 4, R_ALIVE = 5, R_FREE = 6 };

inline uint32_t Ld(volatile uint32_t* p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
inline void St(volatile uint32_t* p, uint32_t v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }

void Wake()
{
	__atomic_add_fetch(&g_ctl[2], 1u, __ATOMIC_SEQ_CST);
	__builtin_wasm_memory_atomic_notify((int*)&g_ctl[2], INT_MAX);
}

void EnsureInit()
{
	bool expected = false;
	if (!g_inited.compare_exchange_strong(expected, true))
		return;
	MAIN_THREAD_ASYNC_EM_ASM({ if (window.OADNet) OADNet.init($0, $1, $2, $3); }, g_ring, RING_CAP, &g_ctl[0], &g_ctl[1]);
}

bool IsLoopback(enet_uint32 host) { return (host & 0xFF) == 127; }

void Push(ENetHost* h, const ENetEvent& e)
{
	{
		std::lock_guard<std::mutex> lk(h->m);
		h->q.push_back(e);
	}
	Wake();
}

int FreePeer(ENetHost* h)
{
	for (size_t i = 0; i < h->peerCount; ++i)
		if (h->peers[i].state == ENET_PEER_STATE_DISCONNECTED && !h->peers[i].eventPending)
			return (int)i;
	return -1;
}

void InitPeer(ENetHost* h, int i)
{
	ENetPeer& p = h->peers[i];
	memset(&p, 0, sizeof(p));
	p.host = h;
	p.incomingPeerID = p.outgoingPeerID = (enet_uint16)i;
	p.mtu = h->mtu;
	p.state = ENET_PEER_STATE_DISCONNECTED;
}

void SetPeerConnected(ENetPeer& p)
{
	p.state = ENET_PEER_STATE_CONNECTED;
	p.lastReceiveTime = p.lastSendTime = enet_time_get();
	p.roundTripTime = p.lastRoundTripTime = 0;
}

void QueueDisconnect(ENetPeer& p, enet_uint32 data)
{
	p.state = ENET_PEER_STATE_DISCONNECTED;
	p.link = 0;
	p.eventPending = true;
	ENetEvent e{};
	e.type = ENET_EVENT_TYPE_DISCONNECT;
	e.peer = &p;
	e.data = data;
	Push(p.host, e);
}

void JsConnect(ENetHost* h, int idx, const std::string& code)
{
	char* c = strdup(code.c_str());   // freed by the engine once net.js reports it consumed (R_FREE)
	MAIN_THREAD_ASYNC_EM_ASM({ if (window.OADNet) OADNet.connect($0, $1, UTF8ToString($2), $2); }, h, idx, c);
}
void JsDisconnect(ENetHost* h, int idx, enet_uint32 reason)
{
	MAIN_THREAD_ASYNC_EM_ASM({ if (window.OADNet) OADNet.disconnect($0, $1, $2); }, h, idx, reason);
}
} // namespace

extern "C" {

int enet_initialize(void) { return 0; }
void enet_deinitialize(void) {}

enet_uint32 enet_time_get(void)
{
	using namespace std::chrono;
	static const auto t0 = steady_clock::now();
	return (enet_uint32)duration_cast<milliseconds>(steady_clock::now() - t0).count() + 1000;
}

ENetHost* enet_host_create(const ENetAddress* address, size_t peerCount, size_t, enet_uint32, enet_uint32)
{
	if (peerCount == 0 || peerCount > ENET_PROTOCOL_MAXIMUM_PEER_ID)
		return nullptr;
	EnsureInit();
	ENetHost* h = new ENetHost();
	h->socket = ENET_SOCKET_NULL;
	h->address.host = ENET_HOST_ANY;
	h->address.port = address ? address->port : 0;
	h->peerCount = peerCount;
	h->mtu = 1372;
	h->listening = false;
	h->peers = new ENetPeer[peerCount];
	for (size_t i = 0; i < peerCount; ++i)
		InitPeer(h, (int)i);

	std::lock_guard<std::mutex> lk(g_mutex);
	g_hosts.insert(h);
	// A host created with an explicit port accepts incoming connections (the game server).
	if (address && address->port != 0)
	{
		h->listening = true;
		g_listener = h;
		MAIN_THREAD_ASYNC_EM_ASM({ if (window.OADNet) OADNet.listen($0); }, h);
	}
	return h;
}

void enet_host_destroy(ENetHost* h)
{
	if (!h)
		return;
	std::vector<std::pair<ENetHost*, int>> notify;
	{
		std::lock_guard<std::mutex> lk(g_mutex);
		g_hosts.erase(h);
		if (g_listener == h)
		{
			g_listener = nullptr;
			MAIN_THREAD_ASYNC_EM_ASM({ if (window.OADNet) OADNet.unlisten($0); }, h);
		}
		for (size_t i = 0; i < h->peerCount; ++i)
		{
			ENetPeer& p = h->peers[i];
			if (p.state == ENET_PEER_STATE_DISCONNECTED)
				continue;
			if (p.link == 1 && g_hosts.count(p.rhost))
			{
				ENetPeer& rp = p.rhost->peers[p.ridx];
				if (rp.state != ENET_PEER_STATE_DISCONNECTED && rp.rhost == h)
					QueueDisconnect(rp, 0);
			}
			else if (p.link == 2)
				JsDisconnect(h, (int)i, 0);
		}
	}
	for (ENetEvent& e : h->q)
		if (e.type == ENET_EVENT_TYPE_RECEIVE && e.packet)
			enet_packet_destroy(e.packet);
	delete[] h->peers;
	delete h;
}

ENetPeer* enet_host_connect(ENetHost* h, const ENetAddress* addr, size_t, enet_uint32)
{
	if (!h || !addr)
		return nullptr;
	std::lock_guard<std::mutex> lk(g_mutex);
	int idx = FreePeer(h);
	if (idx < 0)
		return nullptr;
	ENetPeer& me = h->peers[idx];
	InitPeer(h, idx);
	me.address = *addr;
	me.state = ENET_PEER_STATE_CONNECTING;

	if (IsLoopback(addr->host))
	{
		ENetHost* srv = g_listener;
		int sidx = (srv && srv != h) ? FreePeer(srv) : -1;
		if (sidx < 0)
		{
			QueueDisconnect(me, 0); // connection refused
			return &me;
		}
		InitPeer(srv, sidx);
		ENetPeer& sp = srv->peers[sidx];
		sp.address.host = (127u) | (1u << 24);
		sp.address.port = (enet_uint16)(40000 + idx);
		sp.link = 1; sp.rhost = h; sp.ridx = idx;
		me.link = 1; me.rhost = srv; me.ridx = sidx;
		SetPeerConnected(sp);
		SetPeerConnected(me);
		ENetEvent e{}; e.type = ENET_EVENT_TYPE_CONNECT;
		e.peer = &sp; Push(srv, e);
		e.peer = &me; Push(h, e);
		return &me;
	}
	if ((addr->host & 0xFF000000u) == CODE_TAG)
	{
		size_t ci = (addr->host & 0x00FFFFFFu) - 1;
		if (ci < g_codes.size())
		{
			me.link = 2;
			JsConnect(h, idx, g_codes[ci]);
			return &me;
		}
	}
	QueueDisconnect(me, 0); // real IP addresses cannot be reached from a browser
	return &me;
}

static void DrainRing();

int enet_host_service(ENetHost* h, ENetEvent* event, enet_uint32 timeout)
{
	if (!h)
		return -1;
	if (event)
		memset(event, 0, sizeof(*event));
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
	for (;;)
	{
		// Same-page links have no pings: consider them alive whenever they are serviced.
		for (size_t i = 0; i < h->peerCount; ++i)
			if (h->peers[i].link == 1 && h->peers[i].state == ENET_PEER_STATE_CONNECTED)
				h->peers[i].lastReceiveTime = enet_time_get();
		const uint32_t wake = Ld(&g_ctl[2]);
		DrainRing();
		{
			std::lock_guard<std::mutex> lk(h->m);
			if (!h->q.empty())
			{
				ENetEvent e = h->q.front();
				h->q.pop_front();
				if (e.type == ENET_EVENT_TYPE_DISCONNECT && e.peer)
					e.peer->eventPending = false;
				if (event)
					*event = e;
				else if (e.type == ENET_EVENT_TYPE_RECEIVE && e.packet)
					enet_packet_destroy(e.packet);
				return 1;
			}
		}
		const auto now = std::chrono::steady_clock::now();
		if (now >= deadline)
			return 0;
		const long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count();
		// Sleeps until something is pushed (Wake) or net.js writes to the ring, or the timeout ends.
		__builtin_wasm_memory_atomic_wait32((int*)&g_ctl[2], (int)wake, ns);
	}
}

void enet_host_flush(ENetHost*) {}

int enet_peer_send(ENetPeer* p, enet_uint8, ENetPacket* packet)
{
	if (!p || !packet)
		return -1;
	if (p->state != ENET_PEER_STATE_CONNECTED)
	{
		enet_packet_destroy(packet);
		return -1;
	}
	const size_t len = packet->dataLength;
	p->packetsSent++;
	p->outgoingDataTotal += (enet_uint32)len;
	p->lastSendTime = enet_time_get();

	if (p->link == 1)
	{
		std::lock_guard<std::mutex> lk(g_mutex);
		ENetHost* rh = p->rhost;
		if (!g_hosts.count(rh) || rh->peers[p->ridx].state != ENET_PEER_STATE_CONNECTED || rh->peers[p->ridx].rhost != p->host)
		{
			enet_packet_destroy(packet);
			return -1;
		}
		ENetPeer& rp = rh->peers[p->ridx];
		rp.lastReceiveTime = enet_time_get();
		rp.incomingDataTotal += (enet_uint32)len;
		ENetEvent e{};
		e.type = ENET_EVENT_TYPE_RECEIVE;
		e.peer = &rp;
		e.packet = packet; // ownership moves to the receiver
		Push(rh, e);
		return 0;
	}
	if (p->link == 2)
	{
		uint8_t* copy = (uint8_t*)malloc(len ? len : 1);
		memcpy(copy, packet->data, len);
		MAIN_THREAD_ASYNC_EM_ASM({ if (window.OADNet) OADNet.send($0, $1, $2, $3); }, p->host, (int)(p - p->host->peers), copy, (int)len);
		enet_packet_destroy(packet);
		return 0;
	}
	enet_packet_destroy(packet);
	return -1;
}

static void DisconnectPeer(ENetPeer* p, enet_uint32 data, bool localEvent)
{
	if (!p || p->state == ENET_PEER_STATE_DISCONNECTED)
		return;
	ENetHost* h = p->host;
	std::lock_guard<std::mutex> lk(g_mutex);
	if (p->link == 1 && g_hosts.count(p->rhost))
	{
		ENetPeer& rp = p->rhost->peers[p->ridx];
		if (rp.state != ENET_PEER_STATE_DISCONNECTED && rp.rhost == h)
			QueueDisconnect(rp, data);
	}
	else if (p->link == 2)
		JsDisconnect(h, (int)(p - h->peers), data);
	if (localEvent)
		QueueDisconnect(*p, 0);
	else
	{
		p->state = ENET_PEER_STATE_DISCONNECTED;
		p->link = 0;
	}
}
void enet_peer_disconnect(ENetPeer* p, enet_uint32 data) { DisconnectPeer(p, data, true); }
void enet_peer_disconnect_now(ENetPeer* p, enet_uint32 data) { DisconnectPeer(p, data, false); }

void enet_peer_reset(ENetPeer* p)
{
	if (!p)
		return;
	ENetHost* h = p->host;
	void* data = p->data;
	enet_uint16 idIn = p->incomingPeerID, idOut = p->outgoingPeerID;
	if (p->state != ENET_PEER_STATE_DISCONNECTED)
		DisconnectPeer(p, 0, false);
	memset(p, 0, sizeof(*p));
	p->host = h;
	p->data = data;
	p->incomingPeerID = idIn;
	p->outgoingPeerID = idOut;
	p->mtu = h ? h->mtu : 1372;
}

ENetPacket* enet_packet_create(const void* data, size_t dataLength, enet_uint32 flags)
{
	ENetPacket* p = new ENetPacket();
	p->flags = flags;
	p->dataLength = dataLength;
	p->data = (enet_uint8*)malloc(dataLength ? dataLength : 1);
	if (data)
		memcpy(p->data, data, dataLength);
	return p;
}
void enet_packet_destroy(ENetPacket* p)
{
	if (!p)
		return;
	free(p->data);
	delete p;
}

int enet_address_set_host(ENetAddress* a, const char* name)
{
	if (!a || !name)
		return -1;
	unsigned b[4];
	char tail;
	if (sscanf(name, "%u.%u.%u.%u%c", &b[0], &b[1], &b[2], &b[3], &tail) == 4 && b[0] < 256 && b[1] < 256 && b[2] < 256 && b[3] < 256)
	{
		a->host = b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
		return 0;
	}
	if (!strcmp(name, "localhost"))
	{
		a->host = 127u | (1u << 24);
		return 0;
	}
	// Anything else is a game code (see site/net.js).
	std::string code(name);
	std::lock_guard<std::mutex> lk(g_mutex);
	size_t i = 0;
	for (; i < g_codes.size(); ++i)
		if (g_codes[i] == code)
			break;
	if (i == g_codes.size())
		g_codes.push_back(code);
	a->host = CODE_TAG | (enet_uint32)(i + 1);
	return 0;
}

int enet_address_get_host_ip(const ENetAddress* a, char* out, size_t len)
{
	if (!a || !out || !len)
		return -1;
	if ((a->host & 0xFF000000u) == CODE_TAG)
	{
		std::lock_guard<std::mutex> lk(g_mutex);
		size_t ci = (a->host & 0x00FFFFFFu) - 1;
		snprintf(out, len, "%s", ci < g_codes.size() ? g_codes[ci].c_str() : "?");
		return 0;
	}
	snprintf(out, len, "%u.%u.%u.%u", a->host & 0xFF, (a->host >> 8) & 0xFF, (a->host >> 16) & 0xFF, (a->host >> 24) & 0xFF);
	return 0;
}

ENetSocket enet_socket_create(ENetSocketType) { return ENET_SOCKET_NULL; }
int enet_socket_connect(ENetSocket, const ENetAddress*) { return -1; }
int enet_socket_get_address(ENetSocket, ENetAddress*) { return -1; }
int enet_socket_send(ENetSocket, const ENetAddress*, const ENetBuffer*, size_t) { return -1; }
int enet_socket_receive(ENetSocket, ENetAddress*, ENetBuffer*, size_t) { return -1; }
void enet_socket_destroy(ENetSocket) {}

// ---------------------------------------------------------------------------------------------
// Records written by site/net.js into the ring (see DrainRing)
// ---------------------------------------------------------------------------------------------
} // extern "C"

namespace
{
struct RecHdr { uint32_t type, size; uint32_t host; int32_t idx; uint32_t a, b; uint32_t len, pad; };  // 32 bytes, payload follows

void OnIncoming(ENetHost* h, int connHandle)
{
	int idx = -1;
	{
		if (g_hosts.count(h) && h->listening)
			idx = FreePeer(h);
		if (idx >= 0)
		{
			InitPeer(h, idx);
			ENetPeer& p = h->peers[idx];
			p.link = 2;
			uint32_t n = g_nextRemoteIp++;
			p.address.host = 10u | ((n & 0xFF) << 8) | (((n >> 8) & 0xFF) << 16) | (1u << 24);
			p.address.port = (enet_uint16)(50000 + idx);
			SetPeerConnected(p);
			ENetEvent e{}; e.type = ENET_EVENT_TYPE_CONNECT; e.peer = &p;
			Push(h, e);
		}
	}
	MAIN_THREAD_ASYNC_EM_ASM({ if (window.OADNet) OADNet.assigned($0, $1); }, connHandle, idx);
}

void OnRecord(const RecHdr& r, const uint8_t* payload, uint32_t len)
{
	ENetHost* h = (ENetHost*)(uintptr_t)r.host;
	const int idx = r.idx;
	switch (r.type)
	{
	case R_FREE:
		free((void*)(uintptr_t)r.a);
		return;
	case R_INCOMING:
	{
		std::lock_guard<std::mutex> lk(g_mutex);
		OnIncoming(h, idx);
		return;
	}
	default: break;
	}
	std::lock_guard<std::mutex> lk(g_mutex);
	if (!g_hosts.count(h) || idx < 0 || (size_t)idx >= h->peerCount)
		return;
	ENetPeer& p = h->peers[idx];
	switch (r.type)
	{
	case R_CONNECTED:
		if (p.state == ENET_PEER_STATE_CONNECTING)
		{
			SetPeerConnected(p);
			ENetEvent e{}; e.type = ENET_EVENT_TYPE_CONNECT; e.peer = &p;
			Push(h, e);
		}
		break;
	case R_CLOSED:
		g_rx.erase({h, idx});
		if (p.state != ENET_PEER_STATE_DISCONNECTED && p.link == 2)
			QueueDisconnect(p, r.a);
		break;
	case R_ALIVE:
		if (p.state == ENET_PEER_STATE_CONNECTED && p.link == 2)
		{
			p.lastReceiveTime = enet_time_get();
			if (r.a > 0)
			{
				const enet_uint32 rtt = r.a - 1;
				p.lastRoundTripTime = rtt;
				p.roundTripTime = p.roundTripTime ? (p.roundTripTime * 7 + rtt) / 8 : rtt;
			}
		}
		break;
	case R_DATA:
	{
		if (p.state != ENET_PEER_STATE_CONNECTED || p.link != 2)
			break;
		p.lastReceiveTime = enet_time_get();
		const uint32_t total = r.a, off = r.b;
		std::vector<uint8_t>& buf = g_rx[{h, idx}];
		if (off == 0)
			buf.assign(total, 0);
		if (off + len > buf.size())
			break;
		memcpy(buf.data() + off, payload, len);
		if (off + len == buf.size())
		{
			p.incomingDataTotal += total;
			ENetEvent e{};
			e.type = ENET_EVENT_TYPE_RECEIVE;
			e.peer = &p;
			e.packet = enet_packet_create(buf.data(), buf.size(), ENET_PACKET_FLAG_RELIABLE);
			buf.clear();
			Push(h, e);
		}
		break;
	}
	default: break;
	}
}
} // namespace

static void DrainRing()
{
	if (Ld(&g_ctl[0]) == Ld(&g_ctl[1]))
		return;
	std::lock_guard<std::mutex> lk(g_drainMutex);
	for (;;)
	{
		const uint32_t head = Ld(&g_ctl[0]);
		uint32_t tail = Ld(&g_ctl[1]);
		if (head == tail)
			return;
		const uint32_t pos = tail & (RING_CAP - 1);
		RecHdr r{};
		memcpy(&r, g_ring + pos, std::min<size_t>(sizeof(r), RING_CAP - pos));
		if (r.size < 8 || pos + r.size > RING_CAP)
		{
			St(&g_ctl[1], head); // corrupt: drop everything
			return;
		}
		if (r.type != R_PAD && sizeof(RecHdr) + r.len <= r.size)
			OnRecord(r, g_ring + pos + sizeof(RecHdr), r.len);
		St(&g_ctl[1], tail + r.size);
	}
}
