/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * SteamWorks_s2m
 * Copyright (C) 2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl gives
 * you permission to link the code of this program (as well as its derivative
 * works) to "Counter-Strike 2," "Source 2," "Steam," and any Game MODs or
 * server software running on software by Valve Corporation. You must obey the
 * GNU General Public License in all respects for all other code used.
 *
 * Additionally, this exception applies to all derivative works unless
 * otherwise stated in LICENSE.txt.
 *
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: SteamWorks_s2m
 */

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

	void AddListener(ISteamWorksListener* pListener);
	void RemoveListener(ISteamWorksListener* pListener);

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
	std::vector<ISteamWorksListener*> m_Listeners;
	std::deque<Message> m_Queue;
	std::vector<std::function<void()>> m_Delivered;
	std::vector<std::function<void()>> m_DeliveredLastFrame;
};

extern GCManager* g_pGCManager;
