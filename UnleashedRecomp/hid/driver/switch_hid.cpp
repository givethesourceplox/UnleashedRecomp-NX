#include <stdafx.h>
#include <hid/hid.h>
#include <kernel/xdm.h>
#include <os/logger.h>

#include <algorithm>
#include <cstring>
#include <switch.h>

namespace
{
    PadState g_pad{};
    XAMINPUT_GAMEPAD g_gamepad{};
    XAMINPUT_VIBRATION g_vibration{};
    HidVibrationDeviceHandle g_vibrationHandles[2]{};
    HidNpadIdType g_vibrationId = HidNpadIdType_No1;
    HidNpadStyleTag g_vibrationStyle = static_cast<HidNpadStyleTag>(0);
    int g_vibrationHandleCount = 0;
    bool g_vibrationInitialized = false;

    int16_t ScaleStickAxis(s32 value)
    {
        return static_cast<int16_t>(std::clamp(value, -32768, 32767));
    }

    void TranslateButton(u64 buttons, HidNpadButton source, uint16_t target, uint16_t& output)
    {
        if ((buttons & source) != 0)
            output |= target;
    }

    void SelectControllerDevice()
    {
        hid::g_inputDevice = hid::EInputDevice::Xbox;
        hid::g_inputDeviceController = hid::EInputDevice::Xbox;
        hid::g_inputDeviceExplicit = hid::EInputDeviceExplicit::SwitchPro;
    }

    bool SelectVibrationTarget(HidNpadIdType& id, HidNpadStyleTag& style, int& handleCount)
    {
        if (padIsHandheld(&g_pad))
        {
            id = HidNpadIdType_Handheld;
            style = HidNpadStyleTag_NpadHandheld;
            handleCount = 2;
            return true;
        }

        if (!padIsNpadActive(&g_pad, HidNpadIdType_No1))
            return false;

        const auto styleSet = hidGetNpadStyleSet(HidNpadIdType_No1);
        id = HidNpadIdType_No1;

        if ((styleSet & HidNpadStyleTag_NpadFullKey) != 0)
        {
            style = HidNpadStyleTag_NpadFullKey;
            handleCount = 2;
            return true;
        }

        if ((styleSet & HidNpadStyleTag_NpadJoyDual) != 0)
        {
            style = HidNpadStyleTag_NpadJoyDual;
            handleCount = 2;
            return true;
        }

        if ((styleSet & HidNpadStyleTag_NpadJoyLeft) != 0)
        {
            style = HidNpadStyleTag_NpadJoyLeft;
            handleCount = 1;
            return true;
        }

        if ((styleSet & HidNpadStyleTag_NpadJoyRight) != 0)
        {
            style = HidNpadStyleTag_NpadJoyRight;
            handleCount = 1;
            return true;
        }

        return false;
    }

    bool EnsureVibration()
    {
        HidNpadIdType id{};
        HidNpadStyleTag style{};
        int handleCount = 0;

        if (!SelectVibrationTarget(id, style, handleCount))
            return false;

        if (g_vibrationInitialized &&
            g_vibrationId == id &&
            g_vibrationStyle == style &&
            g_vibrationHandleCount == handleCount)
        {
            return true;
        }

        if (R_FAILED(hidInitializeVibrationDevices(g_vibrationHandles, handleCount, id, style)))
        {
            g_vibrationInitialized = false;
            g_vibrationHandleCount = 0;
            return false;
        }

        g_vibrationId = id;
        g_vibrationStyle = style;
        g_vibrationHandleCount = handleCount;
        g_vibrationInitialized = true;
        return true;
    }

    void UpdateGamepadState()
    {
        padUpdate(&g_pad);

        const auto buttons = padGetButtons(&g_pad);
        const auto left = padGetStickPos(&g_pad, 0);
        const auto right = padGetStickPos(&g_pad, 1);

        std::memset(&g_gamepad, 0, sizeof(g_gamepad));

        TranslateButton(buttons, HidNpadButton_Up, XAMINPUT_GAMEPAD_DPAD_UP, g_gamepad.wButtons);
        TranslateButton(buttons, HidNpadButton_Down, XAMINPUT_GAMEPAD_DPAD_DOWN, g_gamepad.wButtons);
        TranslateButton(buttons, HidNpadButton_Left, XAMINPUT_GAMEPAD_DPAD_LEFT, g_gamepad.wButtons);
        TranslateButton(buttons, HidNpadButton_Right, XAMINPUT_GAMEPAD_DPAD_RIGHT, g_gamepad.wButtons);

        TranslateButton(buttons, HidNpadButton_Plus, XAMINPUT_GAMEPAD_START, g_gamepad.wButtons);
        TranslateButton(buttons, HidNpadButton_Minus, XAMINPUT_GAMEPAD_BACK, g_gamepad.wButtons);

        TranslateButton(buttons, HidNpadButton_StickL, XAMINPUT_GAMEPAD_LEFT_THUMB, g_gamepad.wButtons);
        TranslateButton(buttons, HidNpadButton_StickR, XAMINPUT_GAMEPAD_RIGHT_THUMB, g_gamepad.wButtons);

        TranslateButton(buttons, HidNpadButton_L, XAMINPUT_GAMEPAD_LEFT_SHOULDER, g_gamepad.wButtons);
        TranslateButton(buttons, HidNpadButton_R, XAMINPUT_GAMEPAD_RIGHT_SHOULDER, g_gamepad.wButtons);

        TranslateButton(buttons, HidNpadButton_B, XAMINPUT_GAMEPAD_A, g_gamepad.wButtons);
        TranslateButton(buttons, HidNpadButton_A, XAMINPUT_GAMEPAD_B, g_gamepad.wButtons);
        TranslateButton(buttons, HidNpadButton_Y, XAMINPUT_GAMEPAD_X, g_gamepad.wButtons);
        TranslateButton(buttons, HidNpadButton_X, XAMINPUT_GAMEPAD_Y, g_gamepad.wButtons);

        g_gamepad.sThumbLX = ScaleStickAxis(left.x);
        g_gamepad.sThumbLY = ScaleStickAxis(left.y);
        g_gamepad.sThumbRX = ScaleStickAxis(right.x);
        g_gamepad.sThumbRY = ScaleStickAxis(right.y);

        g_gamepad.bLeftTrigger = (buttons & HidNpadButton_ZL) != 0 ? UINT8_MAX : 0;
        g_gamepad.bRightTrigger = (buttons & HidNpadButton_ZR) != 0 ? UINT8_MAX : 0;

        if (buttons != 0 || left.x != 0 || left.y != 0 || right.x != 0 || right.y != 0)
            SelectControllerDevice();
    }

    void StopVibration()
    {
        if (!g_vibrationInitialized)
            return;

        HidVibrationValue values[2]{};
        for (int i = 0; i < g_vibrationHandleCount; i++)
        {
            values[i].freq_low = 160.0f;
            values[i].freq_high = 320.0f;
        }

        hidSendVibrationValues(g_vibrationHandles, values, g_vibrationHandleCount);
    }
}

void hid::Init()
{
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
    hidPermitVibration(true);
    SelectControllerDevice();
    LOGN("Switch HID initialized");
}

uint32_t hid::GetState(uint32_t dwUserIndex, XAMINPUT_STATE* pState)
{
    static uint32_t packet;

    if (pState == nullptr)
        return ERROR_BAD_ARGUMENTS;

    std::memset(pState, 0, sizeof(*pState));

    if (dwUserIndex != 0)
        return ERROR_DEVICE_NOT_CONNECTED;

    UpdateGamepadState();

    if (!padIsConnected(&g_pad))
        return ERROR_DEVICE_NOT_CONNECTED;

    pState->dwPacketNumber = packet++;
    pState->Gamepad = g_gamepad;
    return ERROR_SUCCESS;
}

uint32_t hid::SetState(uint32_t dwUserIndex, XAMINPUT_VIBRATION* pVibration)
{
    if (pVibration == nullptr)
        return ERROR_BAD_ARGUMENTS;

    if (dwUserIndex != 0)
        return ERROR_DEVICE_NOT_CONNECTED;

    UpdateGamepadState();

    if (!padIsConnected(&g_pad))
        return ERROR_DEVICE_NOT_CONNECTED;

    g_vibration = *pVibration;

    if (g_vibration.wLeftMotorSpeed == 0 && g_vibration.wRightMotorSpeed == 0)
    {
        StopVibration();
        return ERROR_SUCCESS;
    }

    if (!EnsureVibration())
        return ERROR_DEVICE_NOT_CONNECTED;

    const float lowAmp = std::clamp(g_vibration.wLeftMotorSpeed / 65535.0f, 0.0f, 1.0f);
    const float highAmp = std::clamp(g_vibration.wRightMotorSpeed / 65535.0f, 0.0f, 1.0f);
    HidVibrationValue values[2]{};

    for (int i = 0; i < g_vibrationHandleCount; i++)
    {
        values[i].amp_low = lowAmp;
        values[i].freq_low = 160.0f;
        values[i].amp_high = highAmp;
        values[i].freq_high = 320.0f;
    }

    hidSendVibrationValues(g_vibrationHandles, values, g_vibrationHandleCount);
    return ERROR_SUCCESS;
}

uint32_t hid::GetCapabilities(uint32_t dwUserIndex, XAMINPUT_CAPABILITIES* pCaps)
{
    if (pCaps == nullptr)
        return ERROR_BAD_ARGUMENTS;

    if (dwUserIndex != 0)
        return ERROR_DEVICE_NOT_CONNECTED;

    UpdateGamepadState();

    if (!padIsConnected(&g_pad))
        return ERROR_DEVICE_NOT_CONNECTED;

    std::memset(pCaps, 0, sizeof(*pCaps));

    pCaps->Type = XAMINPUT_DEVTYPE_GAMEPAD;
    pCaps->SubType = XAMINPUT_DEVSUBTYPE_GAMEPAD;
    pCaps->Gamepad = g_gamepad;
    pCaps->Vibration = g_vibration;

    return ERROR_SUCCESS;
}