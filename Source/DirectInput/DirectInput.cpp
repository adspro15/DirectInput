#include "stdafx.h"
#include "DirectInput.tmh"

#include "DirectInput.h"

namespace wdk
{
    extern"C"
    {
        extern POBJECT_TYPE* IoDriverObjectType;

        NTSTATUS ObReferenceObjectByName(
            __in PUNICODE_STRING    ObjectName,
            __in ULONG              Attributes,
            __in_opt PACCESS_STATE  AccessState,
            __in_opt ACCESS_MASK    DesiredAccess,
            __in POBJECT_TYPE       ObjectType,
            __in KPROCESSOR_MODE    AccessMode,
            __inout_opt PVOID       ParseContext,
            __out PVOID *           Object);

    }
}

namespace DirectInput
{
    struct ClassServiceData : public CONNECT_DATA
    {
        enum Classes: UINT32
        {
            Keyboard,
            Mouse,
            Maximum
        };

        auto Initialize(Classes aClass) -> NTSTATUS;

        auto CallService(PVOID aInputData, PVOID aInputDataEnd, UINT32* aConsumed)
            -> NTSTATUS;
    };

    static ClassServiceData s_ClassServices[ClassServiceData::Classes::Maximum]{};

    //////////////////////////////////////////////////////////////////////////

    auto ClassServiceData::Initialize(Classes aClass) 
        -> NTSTATUS
    {
#   pragma prefast(push)
#   pragma prefast(disable: 28175)

        constexpr auto KeyboardHid  = LR"(\Driver\kbdhid)";
        constexpr auto MouseHid     = LR"(\Driver\mouhid)";
        constexpr auto PS2Port      = LR"(\Driver\i8042prt)";
        constexpr auto KeyboardClass= LR"(\Driver\kbdclass)";
        constexpr auto MouseClass   = LR"(\Driver\mouclass)";

        NTSTATUS vStatus = STATUS_SUCCESS;

        PDRIVER_OBJECT vDeviceDriver = nullptr;
        PDRIVER_OBJECT vClassDriver  = nullptr;
        for (;;)
        {
            auto vObjectName    = UNICODE_STRING{};
            auto vDeviceName    = static_cast<decltype(PS2Port)>(nullptr);
            auto vClassName     = static_cast<decltype(PS2Port)>(nullptr);

            if (Classes::Keyboard == aClass)
            {
                vDeviceName = KeyboardHid;
                vClassName  = KeyboardClass;
            }
            else if (Classes::Mouse == aClass)
            {
                vDeviceName = MouseHid;
                vClassName  = MouseClass;
            }

            RtlInitUnicodeString(&vObjectName, vDeviceName);
            vStatus = wdk::ObReferenceObjectByName(
                &vObjectName, OBJ_CASE_INSENSITIVE,
                nullptr,
                FILE_ANY_ACCESS,
                *wdk::IoDriverObjectType,
                KernelMode,
                nullptr,
                (PVOID*)&vDeviceDriver);
            if (!NT_SUCCESS(vStatus))
            {
                if (STATUS_OBJECT_NAME_NOT_FOUND != vStatus)
                {
                    break;
                }

                vDeviceName = PS2Port;
                RtlInitUnicodeString(&vObjectName, vDeviceName);
                vStatus = wdk::ObReferenceObjectByName(
                    &vObjectName, OBJ_CASE_INSENSITIVE,
                    nullptr,
                    FILE_ANY_ACCESS,
                    *wdk::IoDriverObjectType,
                    KernelMode,
                    nullptr,
                    (PVOID*)&vDeviceDriver);
                if (!NT_SUCCESS(vStatus))
                {
                    break;
                }
            }

            RtlInitUnicodeString(&vObjectName, vClassName);
            vStatus = wdk::ObReferenceObjectByName(
                &vObjectName, OBJ_CASE_INSENSITIVE,
                nullptr,
                FILE_ANY_ACCESS,
                *wdk::IoDriverObjectType,
                KernelMode,
                nullptr,
                (PVOID*)&vClassDriver);
            if (!NT_SUCCESS(vStatus))
            {
                break;
            }
            auto vClassDriverStart = (PVOID)(vClassDriver->DriverStart);
            auto vClassDriverEnd   = (PVOID)((SIZE_T)vClassDriverStart + vClassDriver->DriverSize);

            vStatus = STATUS_NOT_FOUND;
            for (auto vDeviceDevice = vDeviceDriver->DeviceObject; vDeviceDevice; vDeviceDevice = vDeviceDevice->NextDevice)
            {
                auto vDeviceExtBytes = (intptr_t)vDeviceDevice->DeviceObjectExtension - (intptr_t)vDeviceDevice->DeviceExtension;
                if (vDeviceExtBytes < 0)
                {
                    continue;
                }
                auto vDeviceExtPtrCount = vDeviceExtBytes / sizeof(void*) - 1;
                auto vDeviceExt = static_cast<void**>(vDeviceDevice->DeviceExtension);

                for (auto vClassDevice = vClassDriver->DeviceObject; vClassDevice; vClassDevice = vClassDevice->NextDevice)
                {
                    for (auto i = 0u; i < vDeviceExtPtrCount; ++i)
                    {
                        if (vDeviceExt[i] == vClassDevice           &&
                            vDeviceExt[i + 1] > vClassDriverStart   &&
                            vDeviceExt[i + 1] < vClassDriverEnd)
                        {
                            ClassDeviceObject = vClassDevice;
                            ClassService      = vDeviceExt[i + 1];

                            vStatus = STATUS_SUCCESS;
                            break;
                        }
                    }
                    if (NT_SUCCESS(vStatus))
                    {
                        break;
                    }
                }
                if (NT_SUCCESS(vStatus))
                {
                    break;
                }
            }
            if (!NT_SUCCESS(vStatus))
            {
                break;
            }

            break;
        }
        if (vClassDriver)  ObDereferenceObject(vClassDriver), vClassDriver = nullptr;
        if (vDeviceDriver) ObDereferenceObject(vDeviceDriver), vDeviceDriver = nullptr;

        return vStatus;

#   pragma prefast(pop)
    }

    auto ClassServiceData::CallService(PVOID aInputData, PVOID aInputDataEnd, UINT32* aConsumed)
        -> NTSTATUS
    {
        struct DPCContext
        {
            KDPC            Dpc;
            KEVENT          Event;
            PVOID           Callback;
            PDEVICE_OBJECT  Device;
            PVOID           InputData;
            PVOID           InputDataEnd;
            ULONG           Consumed;
        };

        NTSTATUS vStatus = STATUS_SUCCESS;

        auto vContext = static_cast<DPCContext*>(nullptr);
        for (;;)
        {
            vContext = (DPCContext*)ExAllocatePoolWithTag(NonPagedPool, sizeof(DPCContext), 'ItiD');
            if (nullptr == vContext)
            {
                vStatus = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            vContext->Callback      = ClassService;
            vContext->Device        = ClassDeviceObject;
            vContext->InputData     = aInputData;
            vContext->InputDataEnd  = aInputDataEnd;
            vContext->Consumed      = 0;

            KeInitializeEvent(&vContext->Event, NotificationEvent, FALSE);
            KeInitializeDpc(
                &vContext->Dpc,
                [](PKDPC /*aDpc*/, PVOID aContext, PVOID /*aSysArg1*/, PVOID /*aSysArg2*/)
                ->void
            {
                auto vContext = static_cast<DPCContext*>(aContext);

                static_cast<PSERVICE_CALLBACK_ROUTINE>(vContext->Callback)(
                    vContext->Device,
                    vContext->InputData,
                    vContext->InputDataEnd,
                    &vContext->Consumed);

                KeSetEvent(&vContext->Event, IO_KEYBOARD_INCREMENT, FALSE);
            }, vContext);

            if (!KeInsertQueueDpc(&vContext->Dpc, nullptr, nullptr))
            {
                vStatus = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            vStatus = KeWaitForSingleObject(&vContext->Event, Executive, KernelMode, FALSE, nullptr);
            if (!NT_SUCCESS(vStatus))
            {
                break;
            }

            if (aConsumed) *aConsumed = vContext->Consumed;
            break;
        }
        if (vContext) ExFreePoolWithTag(vContext, 'ItiD'), vContext = nullptr;

        return vStatus;
    }

    auto Initialize() 
        -> NTSTATUS
    {
        NTSTATUS vStatus = STATUS_SUCCESS;

        for (auto i = 0u; i < ClassServiceData::Maximum; ++i)
        {
            auto& vServiceData = s_ClassServices[i];

            vStatus = vServiceData.Initialize(ClassServiceData::Classes(i));
            if (!NT_SUCCESS(vStatus))
            {
                break;
            }

            TraceError("ClassDevice : %p", vServiceData.ClassDeviceObject);
            TraceError("ClassService: %p", vServiceData.ClassService);
        }

        return vStatus;
    }

    template<typename T>
    static auto MouseInput($MOUSEINPUT<T>& aInput)
        -> NTSTATUS
    {
        NTSTATUS vStatus    = STATUS_SUCCESS;
        auto&    vService   = s_ClassServices[ClassServiceData::Mouse];

        for (;;)
        {
            auto vInput  = MOUSE_INPUT_DATA{};
            vInput.LastX = aInput.dx;
            vInput.LastY = aInput.dy;
            vInput.ButtonData = static_cast<decltype(vInput.ButtonData)>(aInput.mouseData);

            if (MOUSEEVENTF_ABSOLUTE & aInput.dwFlags)
            {
                vInput.Flags |= MOUSE_MOVE_ABSOLUTE;
            }
            if (MOUSEEVENTF_VIRTUALDESK & aInput.dwFlags)
            {
                vInput.Flags |= MOUSE_VIRTUAL_DESKTOP;
            }
            if (MOUSEEVENTF_MOVE_NOCOALESCE & aInput.dwFlags)
            {
                vInput.Flags |= MOUSE_MOVE_NOCOALESCE;
            }

            if (MOUSEEVENTF_WHEEL & aInput.dwFlags)
            {
                vInput.ButtonFlags |= MOUSE_WHEEL;
            }
            if (MOUSEEVENTF_HWHEEL & aInput.dwFlags)
            {
                vInput.ButtonFlags |= MOUSE_HWHEEL;
            }

            if (MOUSEEVENTF_LEFTDOWN & aInput.dwFlags)
            {
                vInput.ButtonFlags |= MOUSE_LEFT_BUTTON_DOWN;
            }
            if (MOUSEEVENTF_LEFTUP & aInput.dwFlags)
            {
                vInput.ButtonFlags |= MOUSE_LEFT_BUTTON_UP;
            }
            if (MOUSEEVENTF_RIGHTDOWN & aInput.dwFlags)
            {
                vInput.ButtonFlags |= MOUSE_RIGHT_BUTTON_DOWN;
            }
            if (MOUSEEVENTF_RIGHTUP & aInput.dwFlags)
            {
                vInput.ButtonFlags |= MOUSE_RIGHT_BUTTON_UP;
            }
            if (MOUSEEVENTF_MIDDLEDOWN & aInput.dwFlags)
            {
                vInput.ButtonFlags |= MOUSE_MIDDLE_BUTTON_DOWN;
            }
            if (MOUSEEVENTF_MIDDLEUP & aInput.dwFlags)
            {
                vInput.ButtonFlags |= MOUSE_MIDDLE_BUTTON_UP;
            }

            if (MOUSEEVENTF_XDOWN & aInput.dwFlags)
            {
                vInput.ButtonFlags |= MOUSE_BUTTON_4_DOWN;
            }
            if (MOUSEEVENTF_XUP & aInput.dwFlags)
            {
                vInput.ButtonFlags |= MOUSE_BUTTON_4_UP;
            }

            vStatus = vService.CallService(&vInput, &vInput + 1, nullptr);
            break;
        }

        return vStatus;
    }

    template<typename T>
    static auto KeyboardInput($KEYBDINPUT<T>& aInput)
        -> NTSTATUS
    {
        NTSTATUS vStatus    = STATUS_SUCCESS;
        auto&    vService   = s_ClassServices[ClassServiceData::Keyboard];

        for (;;)
        {
            if (!(KEYEVENTF_SCANCODE & aInput.dwFlags))
            {
                vStatus = STATUS_NOT_IMPLEMENTED;
                break;
            }
            if (KEYEVENTF_UNICODE & aInput.dwFlags)
            {
                vStatus = STATUS_NOT_IMPLEMENTED;
                break;
            }

            auto vInput = KEYBOARD_INPUT_DATA{};
            vInput.MakeCode = aInput.wScan;

            if (KEYEVENTF_KEYUP & aInput.dwFlags)
            {
                vInput.Flags |= KEY_BREAK;
            }
            if (KEYEVENTF_EXTENDEDKEY & aInput.dwFlags)
            {
                vInput.Flags |= KEY_E0;
            }

            vStatus = vService.CallService(&vInput, &vInput + 1, nullptr);
            break;
        }

        return vStatus;
    }

    auto SendInput(UINT32 aInputCount, LPINPUT aInputs, UINT32 aInputBytes, UINT32* aConsumed)
        -> NTSTATUS
    {
        NTSTATUS vStatus = STATUS_SUCCESS;

        auto vSendCount = 0u;
        if (sizeof(INPUT32) == aInputBytes)
        {
            auto vInputs = reinterpret_cast<LPINPUT32>(aInputs);
            for (auto i = 0u; i < aInputCount; ++i)
            {
                auto& vInput = vInputs[i];
                if (INPUT_MOUSE == vInput.type)
                    vStatus = MouseInput(vInput.mi);
                else if (INPUT_KEYBOARD == vInput.type)
                    vStatus = KeyboardInput(vInput.ki);

                if (!NT_SUCCESS(vStatus))
                {
                    break;
                }

                ++vSendCount;
            }
        }
        else if (sizeof(INPUT64) == aInputBytes)
        {
            auto vInputs = reinterpret_cast<LPINPUT64>(aInputs);
            for (auto i = 0u; i < aInputCount; ++i)
            {
                auto& vInput = vInputs[i];
                if (INPUT_MOUSE == vInput.type)
                    vStatus = MouseInput(vInput.mi);
                else if (INPUT_KEYBOARD == vInput.type)
                    vStatus = KeyboardInput(vInput.ki);

                if (!NT_SUCCESS(vStatus))
                {
                    break;
                }

                ++vSendCount;
            }
        }
        else
        {
            vStatus = STATUS_INVALID_PARAMETER;
        }

        if (aConsumed) *aConsumed = vSendCount;

        return vStatus;
    }

}
