#pragma once

#include "plugin.h"

#include <steam/isteamgamecoordinator.h>

#include <deque>
#include <functional>
#include <string>
#include <vector>

// Fake game coordinator messages. GCSDK::CGCClient reads through ISteamGameCoordinator
// IsMessageAvailable / RetrieveMessage, both serve one queue with our and the real GC messages.
class GCManager
{
public:
	GCManager();
	~GCManager();

	bool Init(char* error, size_t maxlength);

	// Refused under FollowCS2ServerGuidelines. onDelivered runs the frame after the game read the message
	bool Send(uint32_t eMsg, const void* body, size_t bodySize, std::function<void()> onDelivered = nullptr);
	// The same, bytes as RetrieveMessage gives them
	bool SendRaw(uint32_t eMsgFlagged, std::string data, std::function<void()> onDelivered = nullptr);
	// Runs the frame after the game read every message queued so far
	void AfterQueued(std::function<void()> fn);
	void OnFrame();
	void OnDeactivated();

	ISteamGameCoordinator* GetGC();
	bool IsReady() const { return m_pGC != nullptr && m_pGCMessageAvailable != nullptr; }
	// From the GC's welcome until a connection status without a session; no real cache comes without one
	bool HasSession() const { return m_bSession; }
	bool CanSend();

	void AddListener(ISteamWorksGCListener* pListener);
	void RemoveListener(ISteamWorksGCListener* pListener);

	// The message a script forward is running for
	const std::string* Current() const { return m_pCurrent; }

private:
	struct Message
	{
		uint32_t m_eMsg;
		std::string m_Data;
		std::function<void()> m_OnDelivered;
	};

	bool Wanted() const;
	void Attach();
	void Detach();
	void PullRealMessages(ISteamGameCoordinator* pGC);
	bool ProcessRealMessage(uint32_t eMsg, std::string& data);

private: // Hooks
	HookReturn<void> SteamAPI_RegisterCallback(FunctionContext& ctx, bool post);
	HookReturn<void> SteamAPI_UnregisterCallback(FunctionContext& ctx, bool post);
	HookReturn<void> ISteamGameCoordinator_SendMessage(FunctionContext& ctx, bool post);
	HookReturn<void> ISteamGameCoordinator_IsMessageAvailable(FunctionContext& ctx, bool post);
	HookReturn<void> ISteamGameCoordinator_RetrieveMessage(FunctionContext& ctx, bool post);

	ISource2ModFunction* m_pfnRegisterCallback = nullptr;
	ISource2ModFunction* m_pfnUnregisterCallback = nullptr;
	ISource2ModFunction* m_pfnSendMessage = nullptr;
	ISource2ModFunction* m_pfnIsMessageAvailable = nullptr;
	ISource2ModFunction* m_pfnRetrieveMessage = nullptr;

	Source2ModHookId m_hSendMessage = 0;
	Source2ModHookId m_hIsMessageAvailable = 0;
	Source2ModHookId m_hRetrieveMessage = 0;

	ISteamGameCoordinator* m_pGC = nullptr;
	CCallbackBase* m_pGCMessageAvailable = nullptr;
	bool m_bSession = false;
	bool m_bPulling = false;
	const std::string* m_pCurrent = nullptr;
	std::vector<ISteamWorksGCListener*> m_Listeners;
	std::deque<Message> m_Queue;
	std::vector<std::function<void()>> m_Delivered;
	std::vector<std::function<void()>> m_DeliveredLastFrame;
};

extern GCManager* g_pGCManager;
