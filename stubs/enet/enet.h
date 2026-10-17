// Browser replacement for the parts of ENet 1.3 that 0 A.D. uses (see stubs/webenet.cpp).
// Peers inside this page are linked directly; remote peers are reached through WebRTC data channels
// managed by site/net.js.  Same API surface and event semantics as ENet so the game code is unchanged.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <condition_variable>

#define ENET_VERSION_MAJOR 1
#define ENET_VERSION_MINOR 3
#define ENET_VERSION_PATCH 17
#define ENET_HOST_ANY 0
#define ENET_HOST_BROADCAST 0xFFFFFFFFu
#define ENET_PORT_ANY 0
#define ENET_PROTOCOL_MAXIMUM_PEER_ID 0xFFF

typedef uint8_t enet_uint8;
typedef uint16_t enet_uint16;
typedef uint32_t enet_uint32;
typedef int ENetSocket;

#define ENET_SOCKET_NULL (-1)
enum ENetSocketType { ENET_SOCKET_TYPE_STREAM = 1, ENET_SOCKET_TYPE_DATAGRAM = 2 };

struct ENetAddress { enet_uint32 host; enet_uint16 port; };
struct ENetBuffer { size_t dataLength; void* data; };

enum ENetPacketFlag {
	ENET_PACKET_FLAG_RELIABLE = 1, ENET_PACKET_FLAG_UNSEQUENCED = 2,
	ENET_PACKET_FLAG_NO_ALLOCATE = 4, ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT = 8, ENET_PACKET_FLAG_SENT = 256
};
typedef struct _ENetPacket {
	size_t referenceCount; enet_uint32 flags; enet_uint8* data; size_t dataLength; void* freeCallback; void* userData;
} ENetPacket;

enum ENetPeerState {
	ENET_PEER_STATE_DISCONNECTED = 0, ENET_PEER_STATE_CONNECTING = 1, ENET_PEER_STATE_CONNECTED = 5,
	ENET_PEER_STATE_DISCONNECT_LATER = 6, ENET_PEER_STATE_DISCONNECTING = 7, ENET_PEER_STATE_ZOMBIE = 9
};
enum ENetEventType {
	ENET_EVENT_TYPE_NONE = 0, ENET_EVENT_TYPE_CONNECT = 1, ENET_EVENT_TYPE_DISCONNECT = 2, ENET_EVENT_TYPE_RECEIVE = 3
};

struct _ENetHost;
typedef struct _ENetHost ENetHost;
typedef struct _ENetPeer {
	ENetHost* host;
	enet_uint16 outgoingPeerID, incomingPeerID;
	enet_uint32 connectID;
	void* data;
	ENetPeerState state;
	ENetAddress address;
	enet_uint32 incomingDataTotal, outgoingDataTotal, lastSendTime, lastReceiveTime, nextTimeout;
	enet_uint32 packetsSent, packetsLost, lastRoundTripTime, roundTripTime, mtu, reliableDataInTransit;
	// --- browser transport internals ---
	int link;            // 0 none, 1 same-page peer, 2 remote (WebRTC)
	ENetHost* rhost;     // link==1: the other host
	int ridx;            // link==1: peer index in the other host
	bool eventPending;   // a DISCONNECT event for this slot has not been delivered yet
} ENetPeer;

struct ENetEvent { ENetEventType type; ENetPeer* peer; enet_uint8 channelID; enet_uint32 data; ENetPacket* packet; };

struct _ENetHost {
	ENetSocket socket;
	ENetAddress address;
	ENetPeer* peers;
	size_t peerCount;
	enet_uint32 mtu;
	// --- browser transport internals ---
	bool listening;
	std::mutex m;
	std::condition_variable cv;
	std::deque<ENetEvent> q;
};

#ifdef __cplusplus
extern "C" {
#endif
int enet_initialize(void);
void enet_deinitialize(void);
enet_uint32 enet_time_get(void);
ENetHost* enet_host_create(const ENetAddress*, size_t peerCount, size_t channelLimit, enet_uint32 inBw, enet_uint32 outBw);
void enet_host_destroy(ENetHost*);
ENetPeer* enet_host_connect(ENetHost*, const ENetAddress*, size_t channelCount, enet_uint32 data);
int enet_host_service(ENetHost*, ENetEvent*, enet_uint32 timeout);
void enet_host_flush(ENetHost*);
int enet_peer_send(ENetPeer*, enet_uint8 channel, ENetPacket*);
void enet_peer_disconnect(ENetPeer*, enet_uint32 data);
void enet_peer_disconnect_now(ENetPeer*, enet_uint32 data);
void enet_peer_reset(ENetPeer*);
ENetPacket* enet_packet_create(const void* data, size_t dataLength, enet_uint32 flags);
void enet_packet_destroy(ENetPacket*);
int enet_address_set_host(ENetAddress*, const char* hostName);
int enet_address_get_host_ip(const ENetAddress*, char* name, size_t nameLength);
// UDP sockets do not exist in the browser: these always fail (only used by the optional STUN helper).
ENetSocket enet_socket_create(ENetSocketType);
int enet_socket_connect(ENetSocket, const ENetAddress*);
int enet_socket_get_address(ENetSocket, ENetAddress*);
int enet_socket_send(ENetSocket, const ENetAddress*, const ENetBuffer*, size_t);
int enet_socket_receive(ENetSocket, ENetAddress*, ENetBuffer*, size_t);
void enet_socket_destroy(ENetSocket);
#ifdef __cplusplus
}
#endif
