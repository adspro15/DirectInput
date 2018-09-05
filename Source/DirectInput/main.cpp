#include "stdafx.h"
#include "main.tmh"

#include "DirectInput.h"

//////////////////////////////////////////////////////////////////////////

static constexpr wchar_t s_DevName[] = LR"(\Device\{ED0EB7DC-9BD5-4DD8-8B5F-24BBFBF31CF0})";
static constexpr wchar_t s_SymName[] = LR"(\DosDevices\Global\{ED0EB7DC-9BD5-4DD8-8B5F-24BBFBF31CF0})";

enum class IoCode : UINT32
{
    BeginCode = 0x800,

    SendInput = CTL_CODE(FILE_DEVICE_UNKNOWN, BeginCode + 1, METHOD_BUFFERED, FILE_ANY_ACCESS),
};

struct SendInputArgs
{
    UINT32  InputCount;
    UINT32  InputBytes;
    UINT64  Inputs;
};

//////////////////////////////////////////////////////////////////////////

extern"C"
{
    DRIVER_INITIALIZE DriverEntry;
}

static PDEVICE_OBJECT s_Nothing = nullptr;

//////////////////////////////////////////////////////////////////////////

_Dispatch_type_(IRP_MJ_CREATE)
_Dispatch_type_(IRP_MJ_CLOSE)
static auto DeviceCreateClose(
    PDEVICE_OBJECT /*aDeviceObject*/,
    PIRP aIrp)
    -> NTSTATUS
{
    aIrp->IoStatus.Status       = STATUS_SUCCESS;
    aIrp->IoStatus.Information  = 0;

    return IoCompleteRequest(aIrp, IO_NO_INCREMENT), STATUS_SUCCESS;
}

_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
static auto DeviceControl(
    PDEVICE_OBJECT /*aDeviceObject*/,
    PIRP aIrp)
    -> NTSTATUS
{
    NTSTATUS  vStatus       = STATUS_SUCCESS;
    ULONG_PTR vReturnInfo   = 0u;

    auto vIrp       = IoGetCurrentIrpStackLocation(aIrp);
    auto vInBytes   = vIrp->Parameters.DeviceIoControl.InputBufferLength;
    auto vOutBytes  = vIrp->Parameters.DeviceIoControl.OutputBufferLength;
    
    switch (IoCode(vIrp->Parameters.DeviceIoControl.IoControlCode))
    {
    case IoCode::SendInput:
    {
        if (sizeof(SendInputArgs) != vInBytes || sizeof(UINT32) != vOutBytes)
        {
            vStatus = STATUS_INVALID_PARAMETER;
            break;
        }

        auto vInBuffer  = aIrp->AssociatedIrp.SystemBuffer;
        auto vOutBuffer = aIrp->AssociatedIrp.SystemBuffer;

        auto vInputData = static_cast<SendInputArgs*>(vInBuffer);
        auto vSendCount = static_cast<UINT32*>(vOutBuffer);
        
        __try
        {
            ProbeForRead((void*)vInputData->Inputs, vInputData->InputBytes * vInputData->InputCount, sizeof(UINT8));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            vStatus = GetExceptionCode();
            break;
        }

        auto vMdl = IoAllocateMdl((void*)vInputData->Inputs, vInputData->InputBytes * vInputData->InputCount, FALSE, TRUE, NULL);
        if (!vMdl)
        {
            vStatus = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        __try
        {
            MmProbeAndLockPages(vMdl, UserMode, IoReadAccess);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            vStatus = GetExceptionCode();

            IoFreeMdl(vMdl), vMdl = nullptr;
            break;
        }

        auto vInputs = MmGetSystemAddressForMdlSafe(vMdl, NormalPagePriority | MdlMappingNoExecute);
        if (nullptr == vInputs)
        {
            MmUnlockPages(vMdl);
            IoFreeMdl(vMdl), vMdl = nullptr;

            vStatus = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        vStatus = DirectInput::SendInput(vInputData->InputCount, (LPINPUT)vInputs, vInputData->InputBytes, vSendCount);

        MmUnlockPages(vMdl);
        IoFreeMdl(vMdl), vMdl = nullptr;
        
        vReturnInfo = sizeof(*vSendCount);
        break;
    }
    default:
    {
        vStatus = STATUS_NOT_IMPLEMENTED;
        break;
    }
    }

    aIrp->IoStatus.Status       = vStatus;
    aIrp->IoStatus.Information  = vReturnInfo;

    return IoCompleteRequest(aIrp, IO_NO_INCREMENT), vStatus;
}

static auto DriverUnload(PDRIVER_OBJECT aDriverObject)
-> void
{
    constexpr UNICODE_STRING cSymName = RTL_CONSTANT_STRING(s_SymName);
    IoDeleteSymbolicLink(const_cast<PUNICODE_STRING>(&cSymName));

    if (s_Nothing)
    {
        IoDeleteDevice(s_Nothing), s_Nothing = nullptr;
    }

    WPP_CLEANUP(aDriverObject);
}

extern"C"
auto DriverEntry(
    PDRIVER_OBJECT  aDriverObject,
    PUNICODE_STRING aRegistryPath)
    -> NTSTATUS
{
    NTSTATUS vStatus = STATUS_SUCCESS;

    for (;;)
    {
        WPP_INIT_TRACING(aDriverObject, aRegistryPath);

        vStatus = DirectInput::Initialize();
        if (!NT_SUCCESS(vStatus))
        {
            break;
        }

        constexpr UNICODE_STRING cDevName = RTL_CONSTANT_STRING(s_DevName);
        constexpr UNICODE_STRING cSymName = RTL_CONSTANT_STRING(s_SymName);

        vStatus = IoCreateDevice(
            aDriverObject,
            0,
            const_cast<PUNICODE_STRING>(&cDevName),
            FILE_DEVICE_UNKNOWN,
            FILE_DEVICE_SECURE_OPEN,
            FALSE,
            &s_Nothing);
        if (!NT_SUCCESS(vStatus))
        {
            break;
        }

        vStatus = IoCreateSymbolicLink(
            const_cast<PUNICODE_STRING>(&cSymName), 
            const_cast<PUNICODE_STRING>(&cDevName));
        if (!NT_SUCCESS(vStatus))
        {
            break;
        }
        s_Nothing->Flags |= DO_BUFFERED_IO;
        s_Nothing->Flags &= ~DO_DEVICE_INITIALIZING;

        aDriverObject->MajorFunction[IRP_MJ_CREATE]         = DeviceCreateClose;
        aDriverObject->MajorFunction[IRP_MJ_CLOSE]          = DeviceCreateClose;
        aDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControl;

        aDriverObject->DriverUnload = DriverUnload;
        break;
    }
    TraceReturn(vStatus);

    if (!NT_SUCCESS(vStatus))
    {
        DriverUnload(aDriverObject);
    }

    return vStatus;
}
