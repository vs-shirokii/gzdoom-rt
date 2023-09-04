#pragma once

#include "win32basevideo.h"

class Win32RTVideo : public Win32BaseVideo
{
public:
    Win32RTVideo();
    DFrameBuffer* CreateFrameBuffer() override;
    void Shutdown() override;
};
