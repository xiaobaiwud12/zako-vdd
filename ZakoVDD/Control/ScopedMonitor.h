#pragma once

#if defined(ZAKO_SCOPED_MONITOR_OFFLINE_TEST)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#else
#include "..\Driver.h"
#endif

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

// Scoped monitor ABI v1. This contract is deliberately separate from the
// existing text-command IOCTL and its shared request structures.
#define ZAKO_SCOPED_MONITOR_IOCTL_QUERY \
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x820, METHOD_BUFFERED, FILE_READ_ACCESS)
#define ZAKO_SCOPED_MONITOR_IOCTL_MUTATE \
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x821, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

#define ZAKO_SCOPED_MONITOR_ABI_VERSION 1u
#define ZAKO_SCOPED_MONITOR_FLAG_SCOPED_M2 1u
#define ZAKO_SCOPED_MONITOR_INDEX 0u
#define ZAKO_SCOPED_MONITOR_ABSENT_INDEX UINT32_MAX

enum ZakoScopedMonitorOperation : UINT32
{
	ZakoScopedMonitorOperationCapability = 0,
	ZakoScopedMonitorOperationCreate = 1,
	ZakoScopedMonitorOperationDestroy = 2,
	ZakoScopedMonitorOperationLookup = 3,
};

typedef struct ZAKO_SCOPED_MONITOR_REQUEST
{
	UINT32 Size;
	UINT32 Version;
	UINT32 Operation;
	UINT32 Reserved;
	GUID ClientGuid;
} ZAKO_SCOPED_MONITOR_REQUEST;

typedef struct ZAKO_SCOPED_MONITOR_RESPONSE
{
	UINT32 Size;
	UINT32 Version;
	UINT32 Flags;
	UINT32 MonitorIndex;
	GUID ClientGuid;
	UINT32 Present;
	UINT32 Reserved;
} ZAKO_SCOPED_MONITOR_RESPONSE;

static_assert(sizeof(ZAKO_SCOPED_MONITOR_REQUEST) == 32, "scoped monitor request must be 32 bytes");
static_assert(offsetof(ZAKO_SCOPED_MONITOR_REQUEST, Version) == 4, "request Version offset");
static_assert(offsetof(ZAKO_SCOPED_MONITOR_REQUEST, Operation) == 8, "request Operation offset");
static_assert(offsetof(ZAKO_SCOPED_MONITOR_REQUEST, Reserved) == 12, "request Reserved offset");
static_assert(offsetof(ZAKO_SCOPED_MONITOR_REQUEST, ClientGuid) == 16, "request GUID offset");
static_assert(sizeof(ZAKO_SCOPED_MONITOR_RESPONSE) == 40, "scoped monitor response must be 40 bytes");
static_assert(offsetof(ZAKO_SCOPED_MONITOR_RESPONSE, Version) == 4, "response Version offset");
static_assert(offsetof(ZAKO_SCOPED_MONITOR_RESPONSE, Flags) == 8, "response Flags offset");
static_assert(offsetof(ZAKO_SCOPED_MONITOR_RESPONSE, MonitorIndex) == 12, "response index offset");
static_assert(offsetof(ZAKO_SCOPED_MONITOR_RESPONSE, ClientGuid) == 16, "response GUID offset");
static_assert(offsetof(ZAKO_SCOPED_MONITOR_RESPONSE, Present) == 32, "response Present offset");
static_assert(offsetof(ZAKO_SCOPED_MONITOR_RESPONSE, Reserved) == 36, "response Reserved offset");

static_assert(ZAKO_SCOPED_MONITOR_IOCTL_QUERY == 0x00226080u, "query IOCTL must stay 0x00226080");
static_assert(ZAKO_SCOPED_MONITOR_IOCTL_MUTATE == 0x0022E084u, "mutate IOCTL must stay 0x0022E084");

namespace ZakoScopedMonitor
{
	struct PlatformState
	{
		bool hasMonitor0 = false;
		bool hasMonitorGuids0 = false;
		bool hasAnyMonitor = false;
		bool hasForeignMonitor = false;
		GUID monitorGuid0 = {};
	};

	inline bool SameGuid(const GUID &left, const GUID &right)
	{
		return memcmp(&left, &right, sizeof(GUID)) == 0;
	}

	inline bool IsZeroGuid(const GUID &guid)
	{
		static const GUID zero = {};
		return SameGuid(guid, zero);
	}

	inline ZAKO_SCOPED_MONITOR_RESPONSE EmptyResponse()
	{
		ZAKO_SCOPED_MONITOR_RESPONSE response = {};
		response.Size = sizeof(response);
		response.Version = ZAKO_SCOPED_MONITOR_ABI_VERSION;
		response.Flags = ZAKO_SCOPED_MONITOR_FLAG_SCOPED_M2;
		response.MonitorIndex = ZAKO_SCOPED_MONITOR_ABSENT_INDEX;
		response.Present = 0;
		return response;
	}

	inline bool IsValidRequestShape(
		const ZAKO_SCOPED_MONITOR_REQUEST &request,
		bool mutate,
		NTSTATUS &status)
	{
		if (request.Size != sizeof(request) ||
		    request.Version != ZAKO_SCOPED_MONITOR_ABI_VERSION ||
		    request.Reserved != 0)
		{
			status = STATUS_INVALID_PARAMETER;
			return false;
		}

		const bool zeroGuid = IsZeroGuid(request.ClientGuid);
		switch (request.Operation)
		{
		case ZakoScopedMonitorOperationCapability:
			if (mutate || !zeroGuid)
			{
				break;
			}
			status = STATUS_SUCCESS;
			return true;
		case ZakoScopedMonitorOperationCreate:
		case ZakoScopedMonitorOperationDestroy:
			if (!mutate || zeroGuid)
			{
				break;
			}
			status = STATUS_SUCCESS;
			return true;
		case ZakoScopedMonitorOperationLookup:
			if (mutate || zeroGuid)
			{
				break;
			}
			status = STATUS_SUCCESS;
			return true;
		default:
			break;
		}

		status = STATUS_INVALID_PARAMETER;
		return false;
	}

	template <typename Platform>
	inline NTSTATUS ProcessRequest(
		Platform &platform,
		bool mutate,
		size_t inputBufferLength,
		size_t outputBufferLength,
		const ZAKO_SCOPED_MONITOR_REQUEST &request,
		ZAKO_SCOPED_MONITOR_RESPONSE &response)
	{
		try
		{
			if (inputBufferLength != sizeof(ZAKO_SCOPED_MONITOR_REQUEST) ||
			    outputBufferLength != sizeof(ZAKO_SCOPED_MONITOR_RESPONSE))
			{
				return STATUS_INVALID_BUFFER_SIZE;
			}

			NTSTATUS status = STATUS_SUCCESS;
			if (!IsValidRequestShape(request, mutate, status))
			{
				return status;
			}

			response = EmptyResponse();
			response.ClientGuid = request.ClientGuid;
			if (request.Operation == ZakoScopedMonitorOperationCapability)
			{
				return STATUS_SUCCESS;
			}

			const PlatformState before = platform.Snapshot();
			if (before.hasMonitorGuids0 && !before.hasMonitor0)
			{
				return STATUS_INVALID_DEVICE_STATE;
			}

			if (request.Operation == ZakoScopedMonitorOperationLookup)
			{
				if (before.hasMonitor0 && before.hasMonitorGuids0 &&
				    SameGuid(before.monitorGuid0, request.ClientGuid))
				{
					response.MonitorIndex = ZAKO_SCOPED_MONITOR_INDEX;
					response.Present = 1;
				}
				return STATUS_SUCCESS;
			}

			if (request.Operation == ZakoScopedMonitorOperationCreate)
			{
				if (before.hasForeignMonitor)
				{
					return STATUS_DEVICE_BUSY;
				}
				if (before.hasMonitor0 && before.hasMonitorGuids0 &&
				    SameGuid(before.monitorGuid0, request.ClientGuid))
				{
					response.MonitorIndex = ZAKO_SCOPED_MONITOR_INDEX;
					response.Present = 1;
					return STATUS_SUCCESS;
				}
				if (before.hasAnyMonitor)
				{
					return STATUS_DEVICE_BUSY;
				}

				status = platform.Create(request.ClientGuid);
				if (!NT_SUCCESS(status))
				{
					return status;
				}

				const PlatformState after = platform.Snapshot();
				if (!after.hasMonitor0 || !after.hasMonitorGuids0 ||
				    !SameGuid(after.monitorGuid0, request.ClientGuid))
				{
					platform.RollbackCreate(request.ClientGuid);
					return STATUS_UNSUCCESSFUL;
				}

				response.MonitorIndex = ZAKO_SCOPED_MONITOR_INDEX;
				response.Present = 1;
				return STATUS_SUCCESS;
			}

			status = platform.Destroy(request.ClientGuid);
			if (!NT_SUCCESS(status))
			{
				return status;
			}

			const PlatformState after = platform.Snapshot();
			const bool requestedStillPresent =
				after.hasMonitor0 &&
				after.hasMonitorGuids0 &&
				SameGuid(after.monitorGuid0, request.ClientGuid);
			if (requestedStillPresent)
			{
				return STATUS_INVALID_DEVICE_STATE;
			}

			response.MonitorIndex = ZAKO_SCOPED_MONITOR_ABSENT_INDEX;
			response.Present = 0;
			return STATUS_SUCCESS;
		}
		catch (...)
		{
			return STATUS_UNSUCCESSFUL;
		}
	}
}
