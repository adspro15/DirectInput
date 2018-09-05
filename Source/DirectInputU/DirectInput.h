#pragma once

namespace DirectInput
{

    auto Initialize()
        -> HRESULT;

    auto SendInput(UINT32 aCount, LPINPUT aInputs, UINT32 aBytes)
        -> UINT;
}
