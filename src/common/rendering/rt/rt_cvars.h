#pragma once

#include "c_cvars.h"

namespace cvar
{

EXTERN_CVAR( Float, rt_cpu_nocullradius )

EXTERN_CVAR( Float, rt_classic )

EXTERN_CVAR( Int, rt_upscale_dlss )
EXTERN_CVAR( Int, rt_upscale_fsr2 )
EXTERN_CVAR( Int, rt_framegen )

EXTERN_CVAR( Bool, rt_vsync )
EXTERN_CVAR( Bool, rt_hdr )

EXTERN_CVAR( Int, rt_shadowrays )
EXTERN_CVAR( Bool, rt_withplayer )
EXTERN_CVAR( Bool, rt_lerpmdlangle )

extern bool rt_available_dlss2;
extern bool rt_available_dlss3fg;
extern bool rt_available_fsr2;
extern bool rt_available_fsr3fg;
extern bool rt_available_dxgi;

extern const char* rt_failreason_dlss2;
extern const char* rt_failreason_dlss3fg;
extern const char* rt_failreason_fsr2;
extern const char* rt_failreason_fsr3fg;
extern const char* rt_failreason_dxgi;

extern bool rt_hdr_available;

extern bool rt_firststart;

}

extern int rt_cullmode;
#define rt_only_one_side_wall true
#define rt_nocull_flat true
