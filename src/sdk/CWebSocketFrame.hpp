
#pragma once

#include <cstddef>
#include <cstdint>


constexpr uint32_t kMsgHdrProtoFlag = 0x80000000u;

struct MsgHdr
{
	uint32_t eMsg;          // raw value: actual eMsg OR kMsgHdrProtoFlag
	uint32_t headerLength;  // length of the CMsgProtoBufHeader that follows
};

#if defined(__x86_64__)
// steamrt64: the frame lives at +0x10 / +0x18, read off the packet wrapper that
// CRemoteClientManager::RecvPkt builds (data pointer at +0x10, size at +0x18, with
// the proto flag as the top bit of the first dword) and confirmed by
// CJobMgr::BRouteMsgToJob.  Only these three fields are known; the rest is padding.
struct CRemoteClientPacket
{
	uint32_t   m_hConnection;        // 0x0
	uint8_t    __pad0x4[0xC];        // 0x4
	uint8_t*   m_pubData;            // 0x10
	uint32_t   m_cubData;            // 0x18
};
static_assert(offsetof(CRemoteClientPacket, m_pubData) == 0x10);
static_assert(offsetof(CRemoteClientPacket, m_cubData) == 0x18);
#else
struct CRemoteClientPacket
{
	uint32_t   m_hConnection;        // HCONNECTION
	uint8_t*   m_pubData;
	uint32_t   m_cubData;
	int32_t    m_cRef;
	uint8_t*   m_pubNetworkBuffer;
	CRemoteClientPacket* m_pNext;
};
#endif

enum EWebSocketOpCode : uint32_t
{
	k_eWebSocketOpCode_Continuation = 0,
	k_eWebSocketOpCode_Text         = 1,
	k_eWebSocketOpCode_Binary       = 2,
	k_eWebSocketOpCode_Close        = 8,
	k_eWebSocketOpCode_Ping         = 9,
	k_eWebSocketOpCode_Pong         = 10,
};

constexpr uint32_t kEMsgServiceMethodCallFromClient = 151;
constexpr uint32_t kEMsgServiceMethodResponse       = 147;
