//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======
//
// Purpose: Game system that owns the game's GCSDK::CGCClient and runs it every frame
//
//=============================================================================

#ifndef GC_CLIENTSYSTEM_H
#define GC_CLIENTSYSTEM_H
#ifdef _WIN32
#pragma once
#endif

#include <gcsdk/gcclient.h>
#include <gcsdk_gcmessages.pb.h>
#include <igamesystem.h>
#include <steam/steamclientpublic.h>
#include <tier0/utlmap.h>
#include <tier0/utlstring.h>

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
// class CGCClientSystem: public CAutoGameSystem, public IGameSystem
class CGCClientSystem : public CAutoGameSystem
{
public:
	CGCClientSystem();
	virtual ~CGCClientSystem();

	GCSDK::CGCClient *GetGCClient() { return &m_GCClient; }
	const GCSDK::CGCClient *GetGCClient() const { return &m_GCClient; }

	bool BConnectedtoGC() const { return m_bConnectedToGC; }
	const char *GetTxnCountryCode() const { return m_sTxnCountryCode.Get(); }

protected:
	bool m_bConnectedToGC;

	// the GC's rtime32 minus our own at the moment the welcome arrived
	int32 m_nGCRTimeDelta;
	uint32 m_rtimeGCWelcome;
	double m_timeGCWelcome;

	// the account's store fields, taken from CMsgClientWelcome
	uint32 m_eCurrency;
	CUtlString m_sBalanceUrl;
	CUtlString m_sTxnCountryCode;
	uint32 m_unBalance;
	bool m_bHasBalance;

	CMsgClientWelcome_Location m_msgLocation;

	CUtlOrderedMap< char *, CUtlString, int > m_mapKeyedStrings;

	bool m_bInittedGC;

	GCSDK::CGCClient m_GCClient;

	CCallback< CGCClientSystem, SteamServersConnected_t, true > m_CallbackLogonSuccess;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
// class CCSGCServerSystem: public CGCClientSystem, public CAutoGameSystem, public IGameSystem, public GCSDK::ISharedObjectListener
class CCSGCServerSystem : public CGCClientSystem, public GCSDK::ISharedObjectListener
{
public:
	CCSGCServerSystem();
	virtual ~CCSGCServerSystem();

	int GetGCConnections() const { return m_nGCConnections; }

protected:
	[[maybe_unused]] char pad0[0x18];

	// the SO cache listener is removed from m_GCClient on Shutdown while this is valid
	CSteamID m_SteamID;

	[[maybe_unused]] char pad1[0x18];

	// +1 on every GC welcome, the "instance idx" of "GC Connection established"
	int32 m_nGCConnections;
};

extern CCSGCServerSystem* g_pCSGCServerSystem;

#endif // GC_CLIENTSYSTEM_H
