/* xinput_joy.c: the XInput pad laid out as the WinMM joystick the engine binds against. */
#include "unittest.h"

#include "xinput_joy.h"

int main(void)
{
    XINPUT_GAMEPAD pad;
    JOYINFOEX      out;
    JOYCAPSA       caps;

    ut_section("a pad at rest is centred");
    ZeroMemory(&pad, sizeof pad);
    xinput_joy_position(&pad, &out);
    ut_check(out.dwXpos == 32768 && out.dwYpos == 32767, "left stick centred");
    ut_check(out.dwUpos == 32768 && out.dwRpos == 32767, "right stick centred");
    ut_check(out.dwZpos == 32767, "triggers centred");
    ut_check(out.dwButtons == 0 && out.dwButtonNumber == 0, "no button");
    ut_check(out.dwPOV == 0xFFFF, "d-pad centred");

    ut_section("sticks: up is 0 on WinMM's vertical axes");
    pad.sThumbLX = 32767;
    pad.sThumbLY = 32767;
    pad.sThumbRX = -32768;
    pad.sThumbRY = -32768;
    xinput_joy_position(&pad, &out);
    ut_check(out.dwXpos == 65535, "left stick right is X max");
    ut_check(out.dwYpos == 0, "left stick up is Y 0");
    ut_check(out.dwUpos == 0, "right stick left is U 0");
    ut_check(out.dwRpos == 65535, "right stick down is R max");

    ut_section("buttons and d-pad");
    ZeroMemory(&pad, sizeof pad);
    pad.wButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_START |
                   XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_RIGHT;
    pad.bLeftTrigger = 255;
    xinput_joy_position(&pad, &out);
    ut_check(out.dwButtons == (1u | 4u | 0x80u), "A, X and Menu are buttons 1, 3 and 8");
    ut_check(out.dwButtonNumber == 3, "three pressed");
    ut_check(out.dwPOV == 4500, "up and right is 45 degrees");
    ut_check(out.dwZpos > 32767 + 30000, "left trigger pushes Z up");

    ut_section("capabilities match the real pad");
    xinput_joy_caps(&caps);
    ut_check(caps.wNumAxes == 5 && caps.wNumButtons == 16, "5 axes, 16 buttons");
    ut_check(caps.wCaps == 0x37, "Z, R, U and a 4-direction POV, as the real pad reports");

    return ut_summary("xinput_joy");
}
