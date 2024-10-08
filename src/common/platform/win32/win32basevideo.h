#pragma once

#if !HAVE_RT
#include "v_video.h"
#else
#include "i_video.h"
#endif

//==========================================================================
//
// 
//
//==========================================================================

class Win32BaseVideo : public IVideo
{
public:
	Win32BaseVideo();

	void DumpAdapters();

	HDC m_hDC;

protected:
	HMODULE hmRender;

	char m_DisplayDeviceBuffer[CCHDEVICENAME];
	char *m_DisplayDeviceName;
	HMONITOR m_hMonitor;

	HWND m_Window;

	void GetDisplayDeviceName();
public:
	virtual void Shutdown() = 0;

};
