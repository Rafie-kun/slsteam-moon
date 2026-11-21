#pragma once

#include "steam.hpp"

#include "protobufs/steammessages_base.pb.h"
#include "protobufs/encrypted_app_ticket.pb.h"
#include "protobufs/steammessages_clientserver.pb.h"
#include "protobufs/steammessages_clientserver_2.pb.h"
#include "protobufs/steammessages_clientserver_appinfo.pb.h"
#include "protobufs/steammessages_clientserver_friends.pb.h"
#include "protobufs/steammessages_clientserver_userstats.pb.h"

#include <cstddef>
#include <cstdint>

enum EMsgType : EMsg
{
	EMSG_SERVICE_METHOD = 146,
	EMSG_SERVICE_METHOD_RESPONSE = 147,
	EMSG_GAMESPLAYED_NO_DATABLOB = 715,
	EMSG_GAMESPLAYED = 742,
	EMSG_REQUEST_USERSTATS = 818,
	EMSG_REQUEST_USERSTATS_RESPONSE = 819,
	EMSG_APPOWNERSHIPTICKET_RESPONSE = 858,
	// Depot decryption key request/response
	EMSG_GET_DEPOT_DECRYPTION_KEY = 5438,
	EMSG_GET_DEPOT_DECRYPTION_KEY_RESPONSE = 5439,
	EMSG_ENCRYPTED_APPTICKET_RESPONSE = 5527,
	EMSG_WALLET_INFO_UPDATE = 5528,
	EMSG_GAMESPLAYED_WITH_DATABLOB = 5410,
	EMSG_EMAIL_ADDRESS_INFO = 5456,
	EMSG_RICH_PRESENCE_UPLOAD = 7501,
	EMSG_PICS_PRODUCTINFO_REQUEST = 8903,
	EMSG_PICS_PRODUCTINFO_RESPONSE = 8904,
	EMSG_PICS_ACCESSTOKEN_RESPONSE = 8906,
	EMSG_PICS_CHANGES_RESPONSE = 8902,
	EMSG_SHARED_LIBRARY_STOP_PLAYING = 9406,
};

enum EGameFlags
{
	//1 << 0 is set for spacewar, not other mp games. idk
	EGAMEFLAG_JOINABLE = 1 << 1, //Or in Server, etc
	EGAMEFLAG_MULTIPLAYER = 1 << 13,
};

class CProtoBufMsgBase
{
public:
#if defined(__x86_64__)
	// steamrt64 layout (AceSLS/SLSsteam CProtoBufMsgBase, 0x50 bytes).  `type`
	// is the low half of the 32-bit EMsg word at 0x28 (the protobuf flag sits in
	// the high half), exactly what the i386 field below holds.
	char __pad_0x0[0x28];		//0x0
	uint16_t type;				//0x28
	char __pad_0x2A[0x6];		//0x2A
	CMsgProtoBufHeader* header; //0x30
	void* __pBody;				//0x38
	char __pad_0x40[0x10];		//0x40
#else
	char __pad_0x0[0x14];		//0x0
	uint16_t type;				//0x14
	char __pad_0x16[0x6];		//0x16
	CMsgProtoBufHeader* header; //0x1C
	void* __pBody;				//0x20
	char __pad_0x24[0x8];		//0x24
#endif

	template<typename T> constexpr T* getBody() const
	{
		return reinterpret_cast<T*>(__pBody);
	}
}; //0x2C on i386, 0x50 on x86_64

#if defined(__x86_64__)
static_assert(offsetof(CProtoBufMsgBase, type) == 0x28);
static_assert(offsetof(CProtoBufMsgBase, header) == 0x30);
static_assert(offsetof(CProtoBufMsgBase, __pBody) == 0x38);
static_assert(sizeof(CProtoBufMsgBase) == 0x50);
#else
static_assert(offsetof(CProtoBufMsgBase, type) == 0x14);
static_assert(offsetof(CProtoBufMsgBase, header) == 0x1C);
static_assert(offsetof(CProtoBufMsgBase, __pBody) == 0x20);
static_assert(sizeof(CProtoBufMsgBase) == 0x2C);
#endif
