
#pragma once

#include "../sdk/CWebSocketFrame.hpp"

#include <cstdint>
#include <future>


namespace ManifestCode
{
	struct CodeRequest
	{
		uint64_t jobId = 0;
		std::future<uint64_t> result;
	};
	CodeRequest requestCode(uint32_t appId, uint32_t depotId, uint64_t gid);
	void discardRequest(uint64_t jobId);
	void resetSession();
	bool hkBBuildAndAsyncSendFrame(void* pConnection,
	                               EWebSocketOpCode eOpCode,
	                               uint8_t* pubData,
	                               uint32_t cubData);

	void* hkRecvPkt(void* pManager, CRemoteClientPacket* pPacket);

#if defined(__x86_64__)
	bool hkBRouteMsgToJob(void* pJobMgr, void* arg2, void* pMsg, void* pJob, int32_t arg5);
#else
	bool hkBRouteMsgToJob(void* pJobMgr, void* arg2, void* pMsg, void* pJob);
#endif

	bool hkCDepotDownloadMgr_BYldRequestDepotManifest(void* pthis, uint32_t appId, uint32_t depotId, uint64_t manifestId, const char* branch, void* arg20);
}
