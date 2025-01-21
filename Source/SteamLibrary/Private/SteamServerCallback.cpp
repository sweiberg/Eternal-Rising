#include "SteamServerCallback.h"
#include "ClientNetworkSubsystem.h"

void FSteamServerCallback::ServerResponded(HServerListRequest hRequest, int iServer)
{
	if (Subsystem)
	{
		Subsystem->OnServerResponded(hRequest, iServer);
	}
}

void FSteamServerCallback::ServerFailedToRespond(HServerListRequest hRequest, int iServer)
{
	// TODO: Handle Failure to Respond
}

void FSteamServerCallback::RefreshComplete(HServerListRequest hRequest, EMatchMakingServerResponse response)
{
	if (Subsystem)
	{
		Subsystem->OnServerRefreshComplete(hRequest, response);
	}
}