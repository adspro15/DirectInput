#pragma once

//////////////////////////////////////////////////////////////////////////
// Mouse

#define MOUSEEVENTF_MOVE            0x0001 /* mouse move */
#define MOUSEEVENTF_LEFTDOWN        0x0002 /* left button down */
#define MOUSEEVENTF_LEFTUP          0x0004 /* left button up */
#define MOUSEEVENTF_RIGHTDOWN       0x0008 /* right button down */
#define MOUSEEVENTF_RIGHTUP         0x0010 /* right button up */
#define MOUSEEVENTF_MIDDLEDOWN      0x0020 /* middle button down */
#define MOUSEEVENTF_MIDDLEUP        0x0040 /* middle button up */
#define MOUSEEVENTF_XDOWN           0x0080 /* x button down */
#define MOUSEEVENTF_XUP             0x0100 /* x button down */
#define MOUSEEVENTF_WHEEL           0x0800 /* wheel button rolled */
#define MOUSEEVENTF_HWHEEL          0x1000 /* hwheel button rolled */
#define MOUSEEVENTF_MOVE_NOCOALESCE 0x2000 /* do not coalesce mouse moves */
#define MOUSEEVENTF_VIRTUALDESK     0x4000 /* map to entire virtual desktop */
#define MOUSEEVENTF_ABSOLUTE        0x8000 /* absolute move */

template<typename T = ULONG_PTR>
struct $MOUSEINPUT
{
    LONG    dx;
    LONG    dy;
    ULONG   mouseData;
    ULONG   dwFlags;
    ULONG   time;
    T       dwExtraInfo;
};

using MOUSEINPUT    = $MOUSEINPUT<>;
using PMOUSEINPUT   = MOUSEINPUT *;
using LPMOUSEINPUT  = MOUSEINPUT *;

using MOUSEINPUT32  = $MOUSEINPUT<ULONG>;
using PMOUSEINPUT32 = MOUSEINPUT32 *;
using LPMOUSEINPUT32= MOUSEINPUT32 *;

using MOUSEINPUT64  = $MOUSEINPUT<ULONGLONG>;
using PMOUSEINPUT64 = MOUSEINPUT64 *;
using LPMOUSEINPUT64= MOUSEINPUT64 *;

//////////////////////////////////////////////////////////////////////////
// Keyboard

#define KEYEVENTF_EXTENDEDKEY 0x0001
#define KEYEVENTF_KEYUP       0x0002
#define KEYEVENTF_UNICODE     0x0004
#define KEYEVENTF_SCANCODE    0x0008

template<typename T = ULONG_PTR>
struct $KEYBDINPUT {
    USHORT  wVk;
    USHORT  wScan;
    ULONG   dwFlags;
    ULONG   time;
    T       dwExtraInfo;
};

using KEYBDINPUT    = $KEYBDINPUT<>;
using PKEYBDINPUT   = KEYBDINPUT * ;
using LPKEYBDINPUT  = KEYBDINPUT * ;

using KEYBDINPUT32  = $KEYBDINPUT<ULONG>;
using PKEYBDINPUT32 = KEYBDINPUT32 * ;
using LPKEYBDINPUT32= KEYBDINPUT32 * ;

using KEYBDINPUT64  = $KEYBDINPUT<ULONGLONG>;
using PKEYBDINPUT64 = KEYBDINPUT64 * ;
using LPKEYBDINPUT64= KEYBDINPUT64 * ;

//////////////////////////////////////////////////////////////////////////
// Other

typedef struct tagHARDWAREINPUT {
    ULONG       uMsg;
    USHORT      wParamL;
    USHORT      wParamH;
} HARDWAREINPUT, *PHARDWAREINPUT, FAR* LPHARDWAREINPUT;

#define INPUT_MOUSE     0
#define INPUT_KEYBOARD  1
#define INPUT_HARDWARE  2

template<typename T = ULONG_PTR>
struct $INPUT {
    ULONG   type;

    union
    {
        $MOUSEINPUT<T>  mi;
        $KEYBDINPUT<T>  ki;
        HARDWAREINPUT   hi;
    };
};

using INPUT     = $INPUT<>;
using PINPUT    = INPUT * ;
using LPINPUT   = INPUT * ;

using INPUT32   = $INPUT<ULONG>;
using PINPUT32  = INPUT32 * ;
using LPINPUT32 = INPUT32 * ;

using INPUT64   = $INPUT<ULONGLONG>;
using PINPUT64  = INPUT64 * ;
using LPINPUT64 = INPUT64 * ;

namespace DirectInput
{

    auto Initialize()
        -> NTSTATUS;

    auto SendInput(UINT32 aInputCount, LPINPUT aInputs, UINT32 aInputBytes, UINT32* aConsumed)
        -> NTSTATUS;
}
