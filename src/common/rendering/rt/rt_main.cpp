#ifndef NOMINMAX
    #define NOMINMAX
#endif

#include "i_mainwindow.h"
#include "i_time.h"
#include "m_argv.h"
#include "win32rtvideo.h"

#include "base_sysfb.h"
#include "c_dispatch.h"
#include "hw_renderstate.h"
#include "g_levellocals.h"
#include "r_utility.h"
#include "v_draw.h"
#include "flatvertices.h"
#include "hw_bonebuffer.h"
#include "hw_lightbuffer.h"
#include "hw_skydome.h"
#include "hw_viewpointbuffer.h"
#include "i_modelvertexbuffer.h"
#include "p_lnspec.h"
#include "image.h"

#include "rt_state.h"

#include <filesystem>
#include <span>
#include <variant>
#include <ranges>
#include <unordered_set>


//
//
//
//
//
//

#define RG_USE_SURFACE_WIN32
#include <RTGL1/RTGL1.h>

RgInterface rt      = {};
FRtState    rtstate = {};

//
//
//
//
//
//

// clang-format off
template< typename T >
using ValueToCVarRef = 
    std::conditional_t< std::is_same_v< T, bool  >, FBoolCVarRef,
    std::conditional_t< std::is_same_v< T, int   >, FIntCVarRef,
    std::conditional_t< std::is_same_v< T, float >, FFloatCVarRef,
    void > > >;

template< typename T >
constexpr ECVarType ValueToCVarType = 
    std::is_same_v< T, bool  > ? ECVarType::CVAR_Bool :
    std::is_same_v< T, int   > ? ECVarType::CVAR_Int :
    std::is_same_v< T, float > ? ECVarType::CVAR_Float :
                                 ECVarType::CVAR_Dummy;

#define RT_CVAR( name, default_value, description ) \
    ValueToCVarRef< decltype( default_value ) > name; \
    static FCVarDecl cvardecl_##name = { \
        &name, \
        ValueToCVarType< decltype( default_value ) >, \
        CVAR_GLOBALCONFIG | ( ( #name )[ 0 ] == '_' ? 0 : CVAR_ARCHIVE ), \
        #name, \
        CVarValue<ValueToCVarType< decltype( default_value ) >>( default_value ), \
        description, \
        nullptr, }; \
    extern FCVarDecl const *const cvardeclref_##name; \
    MSVC_VSEG FCVarDecl const *const cvardeclref_##name GCC_VSEG = &cvardecl_##name;

#define RT_CVAR_COLOR( name, default_value, description ) \
    CVARD( Color, ##name, default_value, CVAR_GLOBALCONFIG | CVAR_ARCHIVE, description )
// clang-format on


// clang-format off
namespace cvar
{
    // NOTE: if name start with '_' then the cvar won't be archived

    RT_CVAR( rt_cpu_cullmode,           0,      "[IMPACTS CPU PERFORMANCE HEAVILY] 0: BSP + all neighbor sectors of visible,  1 - original GZDoom's BSP/clip checks,  2: uploading whole map, no culling at all" )
    RT_CVAR( rt_cpu_nocullradius,       10.f,   "[IMPACTS CPU PERFORMANCE] Radius (in meters) in which culling must not be applied. Applicable with rt_cpu_cullmode=0" )

    RT_CVAR( rt_autoexport,             true,   "if true: if map's gltf doesn't exist on disk, export to gltf "
                                                "and process the map as if it's static (which improves performance / stability)" )
    RT_CVAR( rt_autoexport_light,       200.f,  "On auto export to gltf, apply this multiplier to the sector light intensities" ) 

    RT_CVAR( rt_classic,                0.f,    "[0.0,1.0] what portion of the screen to render with a classic mode" )
    RT_CVAR( rt_classic_mus,            true,   "if true, apply high pass filter to music when classic mode is enabled" )
    RT_CVAR( rt_classic_white,          10.f,   "white point for classic renderer" )
    RT_CVAR( rt_classic_llmax,          0.8f,    "max light level: remaps a gzdoom sector light level [0.0,rt_classic_llmax] range to [0.0,1.0]" )
    RT_CVAR( rt_classic_llpow,          5.f,    "power to apply to convert a gzdoom sector light level [0.0,1.0] to visible intensity" )

    RT_CVAR( rt_framegen,               0,      "enable frame generation via DirectX 12 and DXGI swapchain. DLSS3 if rt_upscale_dlss>0, FSR3 if rt_upscale_fsr2>0. "
                                                "Values:  0=off  1=on  -1=run frame generation logic, but skip presentation of the generated frame." )
    RT_CVAR( rt_dxgi,                   false,  "use DXGI (DirectX 12) swapchain to present to screen, better compatibility with Windows windowing system" )
    RT_CVAR( rt_vsync,                  false,  "vertical synchronization to prevent tearing" )
    RT_CVAR( rt_hdr,                    false,  "enable HDR output for display" )

    RT_CVAR( rt_fluid,                  true,   "enable fluid simulation (blood)" )
    RT_CVAR( rt_fluid_budget,         100000,   "(APPLIED ONLY after disabling rt_fluid) fluid simulation particle budget " )
    RT_CVAR( rt_fluid_pradius,          0.1f,   "(APPLIED ONLY after disabling rt_fluid) radis of one particle (in meters) for fluid simulation" )
    RT_CVAR( rt_fluid_gravity_x,        0.f,    "gravity vector for fluid (horizontal, X), in m/s^2" )
    RT_CVAR( rt_fluid_gravity_y,        0.f,    "gravity vector for fluid (horizontal, Y), in m/s^2" )
    RT_CVAR( rt_fluid_gravity_z,        -9.8f,  "gravity vector for fluid (vertical), in m/s^2" )
    RT_CVAR( rt_blood_color_r,          0.4f,   "color for blood fluid (Red)" )
    RT_CVAR( rt_blood_color_g,          0.0f,   "color for blood fluid (Green)" )
    RT_CVAR( rt_blood_color_b,          0.0f,   "color for blood fluid (Blue)" )
    
    RT_CVAR( rt_renderscale,            0.f,    "[0.2, 1.0] resolution scale")
    RT_CVAR( rt_upscale_dlss,           0,      "0 - off, 1 - quality, 2 - balanced, 3 - perf, 4 - ultra perf, 5 - DLSS with rt_renderscale, 6 - DLAA. "
                                                "This controls the DLSS upscaling (Super Resolution) but not the Frame Generation" )
    RT_CVAR( rt_upscale_fsr2,           0,      "0 - off, 1 - quality, 2 - balanced, 3 - perf, 4 - ultra perf, 5 - FSR2 with rt_renderscale, 6 - native. "
                                                "This controls the FSR3 / FSR2 upscaling (Super Resolution), but not the Frame Generation.")
    RT_CVAR( rt_sharpen,                0,      "image sharpening; 0 - auto, 1 - naive, 2 - AMD CAS, 3 - force disable" )

    RT_CVAR( rt_shadowrays,             4,      "max depth of shadow ray casts" )
    RT_CVAR( rt_withplayer,             true,   "enable player model for shadows, reflections etc" )
    RT_CVAR( rt_lerpmdlangle,           true,   "interpolate subtick rotation for replacements" )
    RT_CVAR( rt_spectre,                0,      "render spectres as: 0 - water, 1 - glass, 2 - mirror" )
    RT_CVAR( rt_spectre_invis1,         0,      "render first-person weapons, viewer invisibility as: 0 - water, 1 - glass, 2 - mirror" )
    RT_CVAR( rt_znear,                  0.07f,  "camera near plane (in meters); precision problems occur on a first-person weapons if too small (<=0.05)" )
    RT_CVAR( rt_zfar,                   2048.f, "camera far plane (in meters); precision problems occur on a first-person weapons if too large" )

    RT_CVAR( rt_normalmap_stren,        1.f,    "normal map influence" )
    RT_CVAR( rt_heightmap_stren,        1.f,    "height map influence" )
    RT_CVAR( rt_emis_mapboost,          200.f,  "indirect illumination emissiveness" )
    RT_CVAR( rt_emis_maxscrcolor,       8.f,    "burn on-screen emissive colors" )
    RT_CVAR( rt_emis_additive_dflt,     0.5f,   "emission value for objects with additive blending" )
    RT_CVAR( rt_smoothtextures,         false,  "enable linear texture filtering" )

    RT_CVAR( rt_tnmp_ev100_min,         2.f,    "min brightness for auto-exposure" )
    RT_CVAR( rt_tnmp_ev100_max,         7.7f,   "max brightness for auto-exposure" )
    RT_CVAR( rt_tnmp_saturation_r,      0.f,    "-1 desaturate, +1 over saturate" )
    RT_CVAR( rt_tnmp_saturation_g,      0.f,    "-1 desaturate, +1 over saturate" )
    RT_CVAR( rt_tnmp_saturation_b,      0.f,    "-1 desaturate, +1 over saturate" )
    RT_CVAR( rt_tnmp_crosstalk_r,       1.0f,   "how much to shift Red, when Green or Blue are intense; set one channel to 1.0, others to <= 1.0" )
    RT_CVAR( rt_tnmp_crosstalk_g,       0.7f,   "how much to shift Green, when Red or Blue are intense; set one channel to 1.0, others to <= 1.0" )
    RT_CVAR( rt_tnmp_crosstalk_b,       0.8f,   "how much to shift Blue, when Red or Green are intense; set one channel to 1.0, others to <= 1.0" )
    RT_CVAR( rt_tnmp_contrast,          0.1f,   "(only if rt_hdr is OFF) LDR contrast" )
    RT_CVAR( rt_hdr_contrast,           0.15f,  "(only if rt_hdr is ON) HDR contrast" )
    RT_CVAR( rt_hdr_saturation,         0.15f,  "(only if rt_hdr is ON) HDR saturation: -1 desaturate, +1 over saturate" )
    RT_CVAR( rt_hdr_brightness,         1.0f,   "(only if rt_hdr is ON) HDR brightess multiplier" )

    RT_CVAR( rt_sky,                    100.f,  "sky intensity")
    RT_CVAR( rt_sky_saturation,         1.f,    "sky saturation")
    RT_CVAR( rt_sky_stretch,            1.2f,   "how much to stretch the sky sphere along the vertical axis")
    RT_CVAR( rt_sky_always,             true,   "always submit sky geometry (even if it's not visible in primary view)")

    RT_CVAR( rt_decals,                 true,   "draw decals. NOTE: impacts CPU performance, as gzdoom requires a doom-wall to be fullyparsed to submit its decals :(")

    RT_CVAR( rt_lightlevel_min,            80,  "[replacements lights] min bound for translating gzdoom lightlevel to light intensity: if lightlevel below this, lights are multiplied by 0.0; must be >= 0" )
    RT_CVAR( rt_lightlevel_max,           230,  "[replacements lights] max bound for translating gzdoom lightlevel to light intensity: if lightlevel above this, lights are multiplied by 1.0; must be <= 255" )
    RT_CVAR( rt_lightlevel_exp,          2.0f,  "[replacements lights] exponent to apply when converting gzdoom lightlevel to light intensity" )

    RT_CVAR( rt_flsh,                   false,  "flashlight enable")
    RT_CVAR( rt_flsh_intensity,         200.f,  "flashlight intensity")
    RT_CVAR( rt_flsh_radius,            0.02f,  "flashlight source disk radius in meters")
    RT_CVAR( rt_flsh_angle,             35.f,   "flashlight width in degrees")
    RT_CVAR( rt_flsh_r,                 -0.3f,  "flashlight position offset - right (in meteres)")
    RT_CVAR( rt_flsh_u,                 -0.7f,  "flashlight position offset - up (in meteres)")
    RT_CVAR( rt_flsh_f,                 0.0f,   "flashlight position offset - forward (in meteres)")

    RT_CVAR( rt_sun,                    false,  "enable sun for debugging")
    RT_CVAR( rt_sun_intensity,          1000.f, "sun intensity")
    RT_CVAR( rt_sun_a,                  45.f,   "[-90, 90] sun altitude angle; how high it is from the horizon")
    RT_CVAR( rt_sun_b,                  0.f,    "[0, 360] sun azimuth angle; hotizontal angle, counter-clockwise")
    RT_CVAR_COLOR( rt_sun_color,      0xFFFFFF, "sun color (hex)")

    RT_CVAR( rt_reflrefr_depth,         8,      "max depth of reflect/refract") 
    RT_CVAR( rt_refr_glass,             1.52f,  "glass index of refraction") 
    RT_CVAR( rt_refr_water,             1.33f,  "water index of refraction") 
    RT_CVAR( rt_refr_thinwidth,         0.0f,   "approx. width of thin media, e.g. thin glass (in meters)") 
    RT_CVAR( rt_refl_thresh,            0.0f,   "assume mirror if roughness is less than this value") 

    RT_CVAR( rt_mzlflsh,                true,   "enable muzzle flash light source (activated on extralight)" )
    RT_CVAR( rt_mzlflsh_intensity,      100.f,  "muzzle flash intensity" )
    RT_CVAR_COLOR( rt_mzlflsh_color,  0xFF8C52, "muzzle flash color (hex)" )
    RT_CVAR( rt_mzlflsh_radius,         0.02f,  "muzzle flash light sphere radius (in meters)")
    RT_CVAR( rt_mzlflsh_offset,         0.6f,   "[0.0, 1.0] muzzle flash offset from the hit point, so the light would not be in a wall")
    RT_CVAR( rt_mzlflsh_f,              3.0f,   "muzzle flash light offset - forward (in meteres)" )
    RT_CVAR( rt_mzlflsh_u,              -0.9f,  "muzzle flash light offset - up (in meteres)" )

    RT_CVAR( rt_volume_type,            1,      "0 - none, 1 - volumetric, 2 - distance based" )
    RT_CVAR( rt_volume_far,             30.f,   "max distance of scattering volume (in meteres)" )
    RT_CVAR( rt_volume_scatter,         1.f,    "density of media" )
    RT_CVAR( rt_volume_ambient,         0.2f,   "ambient term" )
    RT_CVAR( rt_volume_lintensity,      1.f,    "intensity of lights for scattering" )
    RT_CVAR( rt_volume_lassymetry,      0.5f,   "scaterring phase function assymetry" )
    RT_CVAR( rt_volume_history,         8.f,    "max history length for scaterring accumulation (in frames)" )

    RT_CVAR( rt_water_r,                255,    "water color Red [0,255]" )
    RT_CVAR( rt_water_g,                255,    "water color Green [0,255]" )
    RT_CVAR( rt_water_b,                255,    "water color Blue [0,255]" )
    RT_CVAR( rt_water_wavestren,        3.f,    "normal map strength for water" )

    RT_CVAR( rt_bloom,                  true,   "enable bloom" )
    RT_CVAR( rt_bloom_scale,            1.f,    "multiplier for a calculated bloom" )
    RT_CVAR( rt_bloom_ev,               6.f,    "EV offset for bloom calculation input" )
    RT_CVAR( rt_bloom_threshold,        16.f,   "brightness threshold for bloom calculation input" )
    RT_CVAR( rt_bloom_dirt,             true,   "lens dirt enable" )
    RT_CVAR( rt_bloom_dirt_scale,       1.5f,   "lens dirt multiplier" )
    
    RT_CVAR( rt_ef_crt,                 false,  "CRT-monitor filter" )
    RT_CVAR( rt_ef_chraber,             0.15f,  "chromatic aberration intensity" )
    RT_CVAR( rt_ef_vhs,                 0.f,    "VHS filter intensity" )
    RT_CVAR( rt_ef_dither,              0.f,    "dithering filter intensity" )
    RT_CVAR( rt_ef_vintage,             0,      "[0, 7] vintage effects, disabled if rt_renderscale>0" ) // look RT_VINTAGE_* enum
    RT_CVAR( rt_ef_water,               true,   "warp screen while under water" )

    RT_CVAR( rt_pw_lightamp,            0,      "light amplification powerup type: 0 - night vision, 1 - thermal camera, 2 - flashlight" )

    RT_CVAR( rt_melt_duration,          1.5f,   "screen melt effect duration" )

    RT_CVAR( rt_wall_nomv,              1,      "0: motion vectors always,  1: use pegging flags to determine wall motion vectors,  2: always force no motion vectors on walls. "
                                                "This option is needed to fix illumination motion artifacts on lifts / crashers" )

    RT_CVAR( hack_initialframesskip,    true,   "skip initial a couple of frames on game launch; if not skipped, there might be a distracting flashing of the main window" )

    RT_CVAR( _rt_showexportable,        false,  "internal variable; only in debug" )

	// default, so when user launches a game with CRT/Vintage,
	// and after that changes to dlss/fsr2, then this value will be set to the cvars;
	// 2 = balanced; non-archived
    RT_CVAR( _rt_cachedpreset,          2,      "internal variable for menu UX" )

    bool rt_available_dlss2   = false;
    bool rt_available_dlss3fg = false;
    bool rt_available_fsr2    = false;
    bool rt_available_fsr3fg  = false;
    bool rt_available_dxgi    = false;

    const char* rt_failreason_dlss2   = nullptr;
    const char* rt_failreason_dlss3fg = nullptr;
    const char* rt_failreason_fsr2    = nullptr;
    const char* rt_failreason_fsr3fg  = nullptr;
    const char* rt_failreason_dxgi    = nullptr;

    bool rt_hdr_available = false;
    bool rt_fluid_available = false;

    bool rt_firststart = false;
}
// clang-format on

EXTERN_CVAR( Float, blood_fade_scalar );
EXTERN_CVAR( Float, pickup_fade_scalar );

//
//
//
//
//
//

const char* g_rt_cutscenename        = nullptr;
bool        g_rt_showfirststartscene = false;
int         g_rt_skipinitframes      = -10; // to prevent flashing when starting the game
bool        g_rt_forcenofocuschange  = true;
int         rt_cullmode              = 2; // 0 -- balanced,  1 -- original gzdoom,  2 -- none

extern float RT_CutsceneTime();
extern void  RT_ForceIntroCutsceneMusicStop();

extern void RT_CloseLauncherWindow();

auto RT_MakeUpRightForwardVectors( const DRotator& rotation ) -> std::tuple< RgFloat3D, RgFloat3D, RgFloat3D >;

namespace
{

void RG_CHECK( RgResult r )
{
    assert( ( r ) == RG_RESULT_SUCCESS );
}

#define RG_TRANSFORM_IDENTITY              \
    {                                      \
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 \
    }

constexpr auto ORIGINAL_DOOM_RESOLUTION_HEIGHT = 200;
constexpr auto ONEGAMEUNIT_IN_METERS           = 1.0f / 32.0f; // https://doomwiki.org/wiki/Map_unit

constexpr auto RG_PACKED_COLOR_WHITE = RgColor4DPacked32{ 0xFFFFFFFF };


enum
{
    RT_VINTAGE_OFF,
    RT_VINTAGE_CRT,
    RT_VINTAGE_VHS,
    RT_VINTAGE_VHS_CRT,
    RT_VINTAGE_200,
    RT_VINTAGE_200_DITHER,
    RT_VINTAGE_480,
    RT_VINTAGE_480_DITHER,
};


constexpr uint64_t FlashlightLightId  = 0xFFFFFFF + 0;
constexpr uint64_t SunLightId         = 0xFFFFFFF + 1;
constexpr uint64_t MuzzleFlashLightId = 0xFFFFFFF + 2;
constexpr uint64_t SectorLightId_Base = 0xFFFFFFF + 3;



const char* RT_GetMapName()
{
    if( g_rt_cutscenename && g_rt_cutscenename[ 0 ] != '\0' )
    {
        return g_rt_cutscenename;
    }

    if( primaryLevel && !primaryLevel->MapName.IsEmpty() )
    {
        static char mapname_lower[ 128 ];

        size_t i = 0;
        for( ; i < primaryLevel->MapName.Len() && i < std::size( mapname_lower ) - 1; i++ )
        {
            mapname_lower[ i ] = std::tolower( primaryLevel->MapName[ i ] );
        }
        mapname_lower[ std::min( i, std::size( mapname_lower ) - 1 ) ] = '\0';

        return mapname_lower;
    }

    if( g_rt_showfirststartscene )
    {
        // HACKHACK: do not show scene at the first frame: cutscene's firststart::draw is not called at that time :(
        static bool HACKHACK_firstframeskipped = false;
        if( !HACKHACK_firstframeskipped )
        {
            HACKHACK_firstframeskipped = true;
            return nullptr;
        }

        return "mainmenu";
    }

    return nullptr;
}

bool RT_ForceNoClassicMode()
{
    if( g_rt_cutscenename && g_rt_cutscenename[ 0 ] != '\0' )
    {
        return true;
    }
    if( g_rt_showfirststartscene )
    {
        return true;
    }
    return false;
}



constexpr auto RT_BIT( uint32_t b )
{
    return 1u << b;
}
enum rt_powerupflag_t
{
    RT_POWERUP_FLAG_BONUS_BIT          = RT_BIT( 1 ),
    RT_POWERUP_FLAG_BERSERK_BIT        = RT_BIT( 2 ),
    RT_POWERUP_FLAG_RADIATIONSUIT_BIT  = RT_BIT( 3 ),
    RT_POWERUP_FLAG_INVUNERABILITY_BIT = RT_BIT( 4 ),
    RT_POWERUP_FLAG_INVISIBILITY_BIT   = RT_BIT( 5 ),
    RT_POWERUP_FLAG_NIGHTVISION_BIT    = RT_BIT( 6 ),
    RT_POWERUP_FLAG_THERMALVISION_BIT  = RT_BIT( 7 ),
    RT_POWERUP_FLAG_FLASHLIGHT_BIT     = RT_BIT( 8 ),
};
uint32_t RT_CalcPowerupFlags();



constexpr float pi()
{
    return pi::pif();
}

constexpr float to_rad( float degrees )
{
    return degrees * ( pi() / 180.0f );
}

constexpr FVector3 gzvec3(const RgFloat3D &v)
{
    return { v.data[ 0 ], v.data[ 1 ], v.data[ 2 ] };
}

constexpr DVector3 gzvec3d(const RgFloat3D &v)
{
    return { v.data[ 0 ], v.data[ 1 ], v.data[ 2 ] };
}

template< typename T >
auto applygamma( T x ) = delete;
template<>
auto applygamma( float x )
{
    return std::clamp( x * x, 0.f, 1.f );
}
template<>
auto applygamma( uint8_t x )
{
    return static_cast< uint8_t >( applygamma( float( x ) / 255.f ) * 255.f );
}

auto rtcolor( const PalEntry& e ) -> RgColor4DPacked32
{
    return rt.rgUtilPackColorByte4D( e.r, e.g, e.b, e.a );
}

auto rtcolor( const FVector4PalEntry& e ) -> RgColor4DPacked32
{
    return rt.rgUtilPackColorFloat4D( e.r, e.g, e.b, e.a );
}

auto cvarcolor_to_rtcolor( const FColorCVarRef& cvarcolor ) -> RgColor4DPacked32
{
    uint32_t ba = *( cvarcolor );

    int r = RPART( ba );
    int g = GPART( ba );
    int b = BPART( ba );

    return rt.rgUtilPackColorByte4D( r, g, b, 255 );
}

float lightlevel_to_classic( float lightlevel )
{
    if( lightlevel < 0.0f )
    {
        return 1.0f;
    }
    
    float newrange = std::min( float( cvar ::rt_classic_llmax ), 1.0f );
    if( newrange <= 0.0f )
    {
        newrange = 1.0f;
    }

    lightlevel = std::clamp( lightlevel / newrange, 0.0f, 1.0f );
    return std::pow( lightlevel, float( cvar::rt_classic_llpow ) );
}

auto rtcolor_multiply( const FVector4PalEntry& e, const FVector4& b, bool forcealpha1 ) -> RgColor4DPacked32
{
    return rt.rgUtilPackColorFloat4D( e.r * b[ 0 ], //
                                      e.g * b[ 1 ],
                                      e.b * b[ 2 ],
                                      forcealpha1 ? 1.0f : e.a * b[ 3 ] );
}

auto rtcolor_bgr_alphagamma( const PalEntry& e ) -> RgColor4DPacked32
{
    return rt.rgUtilPackColorByte4D( e.b, e.g, e.r, applygamma( e.a ) );
}



class RTRenderState;

class RTFrameBuffer : public SystemBaseFrameBuffer
{
    using Super = SystemBaseFrameBuffer;

public:
    RTFrameBuffer( void* hMonitor, bool fullscreen );
    ~RTFrameBuffer() override;
    void InitializeState() override;
    void BeginFrame() override
    {
        SetViewportRects( nullptr );
        RT_BeginFrame();
        Super::BeginFrame();
    }
    void Update() override
    {
        this->Draw2D();
        twod->Clear();
        RT_DrawFrame();
        Super::Update();
    }
    void FirstEye() override;

    FRenderState*     RenderState() override;
    IVertexBuffer*    CreateVertexBuffer() override;
    IIndexBuffer*     CreateIndexBuffer() override;
    IDataBuffer*      CreateDataBuffer( int bindingpoint, bool ssbo, bool needsresize ) override;
    IHardwareTexture* CreateHardwareTexture( int numchannels ) override;

    void SetVSync( bool vsync ) override { m_vsync = vsync; }
    void SetTextureFilterMode() override {}
    void SetLevelMesh( hwrenderer::LevelMesh* mesh ) override {}

    void Draw2D() override;

public:
    void RT_MarkWasSky() { m_wassky = true; }

private:
    void RT_BeginFrame();
    void RT_DrawFrame();

private:
    RTRenderState* m_state{ nullptr };
    bool           m_vsync{ false };
    bool           m_wassky{ false };
};



class VectorAsBuffer : virtual public IBuffer
{
public:
    ~VectorAsBuffer() override = default;

    void SetSubData( size_t offset, size_t size, const void* data ) override
    {
        if( offset + size > m_buffer.size() )
        {
            m_buffer.resize( offset + size );
        }

        if( data )
        {
            memcpy( &m_buffer[ offset ], data, size );
        }

        buffersize = m_buffer.size();
        if( map )
        {
            map = m_buffer.data();
        }
    }
    void SetData( size_t size, const void* data, BufferUsageType type ) override
    {
        SetSubData( 0, size, data );
    }
    void* Lock( unsigned size ) override
    {
        SetSubData( 0, size, nullptr );
        return m_buffer.data();
    }
    void Unlock() override {}
    void Resize( size_t newsize ) override { m_buffer.resize( newsize ); }
    void Upload( size_t start, size_t size ) override {}
    void Map() override { map = m_buffer.data(); }
    void Unmap() override { map = nullptr; }
    void GPUDropSync() override {}
    void GPUWaitSync() override {}

protected:
    auto AccessBuffer() const { return std::span{ m_buffer }; }

private:
    std::vector< uint8_t > m_buffer;
};

class RTVertexBuffer
    : public IVertexBuffer
    , public VectorAsBuffer
{
    using Super            = VectorAsBuffer;
    using VertexTypeHolder = std::
        variant< std::monostate, FSkyVertex, FModelVertex, FFlatVertex, F2DDrawer::TwoDVertex >;

public:
    void SetFormat( int                           numBindingPoints,
                    int                           numAttributes,
                    size_t                        stride,
                    const FVertexBufferAttribute* attrs ) override
    {
        static_assert( sizeof( FSkyVertex ) != sizeof( FModelVertex ) );
        static_assert( sizeof( FSkyVertex ) != sizeof( FFlatVertex ) );
        static_assert( sizeof( FSkyVertex ) != sizeof( F2DDrawer::TwoDVertex ) );
        static_assert( sizeof( FModelVertex ) != sizeof( FFlatVertex ) );
        static_assert( sizeof( FModelVertex ) != sizeof( F2DDrawer::TwoDVertex ) );
        static_assert( sizeof( FFlatVertex ) != sizeof( F2DDrawer::TwoDVertex ) );

        if( numBindingPoints == 1 && numAttributes == 4 && stride == sizeof( FSkyVertex ) )
        {
            m_vertextype = FSkyVertex{};
        }
        else if( numBindingPoints == 2 && numAttributes == 8 && stride == sizeof( FModelVertex ) )
        {
            m_vertextype = FModelVertex{};
        }
        else if( numBindingPoints == 1 && numAttributes == 3 && stride == sizeof( FFlatVertex ) )
        {
            m_vertextype = FFlatVertex{};
        }
        else if( numBindingPoints == 1 && numAttributes == 3 &&
                 stride == sizeof( F2DDrawer::TwoDVertex ) )
        {
            m_vertextype = F2DDrawer::TwoDVertex{};
        }
        else
        {
            assert( 0 );
            m_vertextype = std::monostate{};
        }
        m_formatted.clear();
    }

    static void MakeFormatted( std::vector< RgPrimitiveVertex >& dst,
                               size_t                            targetCount,
                               std::span< const uint8_t >        srcbuf,
                               const VertexTypeHolder&           vertextype )
    {
        // TODO: mStreamData.uVertexColor for lightstyled?


        static auto gz_unpacknormal_x = []( uint32_t packedNormal ) -> float {
            int inx = ( packedNormal & 1023 );
            return float( inx ) / 512.0f;
        };
        static auto gz_unpacknormal_y = []( uint32_t packedNormal ) -> float {
            int iny = ( ( packedNormal >> 10 ) & 1023 );
            return float( iny ) / 512.0f;
        };
        static auto gz_unpacknormal_z = []( uint32_t packedNormal ) -> float {
            int inz = ( ( packedNormal >> 20 ) & 1023 );
            return float( inz ) / 512.0f;
        };

        static auto rg_packednormal_fallback = rt.rgUtilPackNormal( 0, 1, 0 );

        // make by type
        std::visit(
            [ & ]< typename T >( const T& ) {
                assert( srcbuf.size_bytes() % sizeof( T ) == 0 );

                dst.reserve( targetCount );
                for( size_t i = dst.size(); i < targetCount; i++ )
                {
                    static_assert( sizeof( decltype( srcbuf )::value_type ) == 1 );
                    const auto* ptr = &srcbuf[ i * sizeof( T ) ];

                    if constexpr( std::is_same_v< T, FSkyVertex > )
                    {
                        auto src = reinterpret_cast< const FSkyVertex* >( ptr );

                        dst.push_back( RgPrimitiveVertex{
                            .position     = { src->x * ONEGAMEUNIT_IN_METERS,
                                              src->y * ONEGAMEUNIT_IN_METERS,
                                              src->z * ONEGAMEUNIT_IN_METERS },
                            .normalPacked = rg_packednormal_fallback,
                            .texCoord     = { src->u, src->v },
                            .color        = rtcolor( src->color ),
                        } );
                    }
                    else if constexpr( std::is_same_v< T, FModelVertex > )
                    {
                        auto src = reinterpret_cast< const FModelVertex* >( ptr );

                        dst.push_back( RgPrimitiveVertex{
                            .position = { src->x * ONEGAMEUNIT_IN_METERS,
                                          src->y * ONEGAMEUNIT_IN_METERS,
                                          src->z * ONEGAMEUNIT_IN_METERS },
                            .normalPacked =
                                rt.rgUtilPackNormal( gz_unpacknormal_x( src->packedNormal ),
                                                     gz_unpacknormal_y( src->packedNormal ),
                                                     gz_unpacknormal_z( src->packedNormal ) ),
                            .texCoord = { src->u, src->v },
                            .color    = RG_PACKED_COLOR_WHITE,
                        } );
                    }
                    else if constexpr( std::is_same_v< T, FFlatVertex > )
                    {
                        auto src = reinterpret_cast< const FFlatVertex* >( ptr );

                        dst.push_back( RgPrimitiveVertex{
                            .position     = { src->x * ONEGAMEUNIT_IN_METERS,
                                              src->y * ONEGAMEUNIT_IN_METERS,
                                              src->z * ONEGAMEUNIT_IN_METERS },
                            .normalPacked = rg_packednormal_fallback,
                            .texCoord     = { src->u, src->v },
                            .color        = RG_PACKED_COLOR_WHITE,
                        } );
                    }
                    else if constexpr( std::is_same_v< T, F2DDrawer::TwoDVertex > )
                    {
                        auto src = reinterpret_cast< const F2DDrawer::TwoDVertex* >( ptr );

                        dst.push_back( RgPrimitiveVertex{
                            .position     = { src->x, src->y, src->z },
                            .normalPacked = rg_packednormal_fallback,
                            .texCoord     = { src->u, src->v },
                            .color        = rtcolor_bgr_alphagamma( src->color0 ),
                        } );
                    }
                    else
                    {
                        assert( 0 );
                    }
                }
            },
            vertextype );
    }

    auto AccessFormatted( uint32_t first, uint32_t count ) -> std::span< const RgPrimitiveVertex >
    {
        if( std::holds_alternative< std::monostate >( m_vertextype ) )
        {
            return {};
        }

        if( first + count > m_formatted.size() )
        {
            MakeFormatted( m_formatted, first + count, AccessBuffer(), m_vertextype );
        }

        assert( first + count <= m_formatted.size() );

        return std::span{
            &m_formatted[ first ],
            count,
        };
    }

    void SetData( size_t size, const void* data, BufferUsageType type ) override
    {
        m_formatted.clear();
        Super::SetData( size, data, type );
    }

    void SetSubData( size_t offset, size_t size, const void* data ) override
    {
        m_formatted.clear();
        Super::SetSubData( offset, size, data );
    }

    void Unmap() override
    {
        m_formatted.clear();
        Super::Unmap();
    }

    bool IsSky() const { return std::holds_alternative< FSkyVertex >( m_vertextype ); }
    bool IsUI() const { return std::holds_alternative< F2DDrawer::TwoDVertex >( m_vertextype ); }

private:
    VertexTypeHolder m_vertextype;

    std::vector< RgPrimitiveVertex > m_formatted;
};

class RTIndexBuffer
    : public IIndexBuffer
    , public VectorAsBuffer
{
    using IndexType = uint32_t;

public:
    auto AccessFormatted( uint32_t first, uint32_t count )
    {
        const auto rawbuf = AccessBuffer();
        // loose type check
        assert( rawbuf.size_bytes() % sizeof( IndexType ) == 0 );
        // alignment
        assert( uint64_t( rawbuf.data() ) % sizeof( IndexType ) == 0 );
        // overflow
        assert( sizeof( IndexType ) * ( first + count ) <= rawbuf.size_bytes() );

        return std::span{
            reinterpret_cast< const IndexType* >( rawbuf.data() ) + first,
            count,
        };
    }

    static auto CalcFirstVertexAndVertexCount( std::span< const IndexType > indices )
    {
        uint32_t imin = std::numeric_limits< uint32_t >::max();
        uint32_t imax = std::numeric_limits< uint32_t >::lowest();
        for( const auto& i : indices )
        {
            imin = std::min( imin, i );
            imax = std::max( imax, i );
        }
        return std::pair{
            imax > imin ? imin : 0,
            imax > imin ? imax - imin + 1 : 0,
        };
    }

    auto MakeWithNewFirstIndex( std::span< const IndexType > indices, IndexType newFirst )
    {
        m_cache.clear();
        m_cache.reserve( indices.size() );

        for( const auto& i : indices )
        {
            assert( i >= newFirst );
            m_cache.push_back( i - newFirst );
        }

        return m_cache;
    }

private:
    std::vector< IndexType > m_cache;
};



class RTHardwareTexture : public IHardwareTexture
{
public:
    // Empty, as it's only used for software renderer
    uint32_t CreateTexture( uint8_t*, int, int, int, bool, const char* ) override { return 0; }
    void     AllocateBuffer( int, int, int ) override {}
    uint8_t* MapBuffer() override { return nullptr; }

    void CreateIfWasnt( FGameTexture&       src,
                        int                 clampmode,
                        int                 translation,
                        int                 flags,
                        const FRenderStyle& renderStyle )
    {
        auto rtclamp_x = []( int clampmode ) {
            switch( clampmode )
            {
                case CLAMP_X:
                case CLAMP_XY:
                case CLAMP_XY_NOMIP:
                case CLAMP_NOFILTER_X:
                case CLAMP_NOFILTER_XY:
                case CLAMP_CAMTEX: return RG_SAMPLER_ADDRESS_MODE_CLAMP;
                default: return RG_SAMPLER_ADDRESS_MODE_REPEAT;
            }
        };
        auto rtclamp_y = []( int clampmode ) {
            switch( clampmode )
            {
                case CLAMP_Y:
                case CLAMP_XY:
                case CLAMP_XY_NOMIP:
                case CLAMP_NOFILTER_Y:
                case CLAMP_NOFILTER_XY:
                case CLAMP_CAMTEX: return RG_SAMPLER_ADDRESS_MODE_CLAMP;
                default: return RG_SAMPLER_ADDRESS_MODE_REPEAT;
            }
        };
        auto desaturateIfNeed = []( FTextureBuffer& data, int flags, const char* lumpname ) {
            // special case for the SmallFont...
            const bool isSTCFNFont = !( flags & CTF_Indexed ) && lumpname &&
                                     strlen( lumpname ) == 8 &&
                                     strncmp( lumpname, "STCFN", 5 ) == 0;
            if( isSTCFNFont )
            {
                for( int i = 0; i < data.mWidth; i++ )
                {
                    for( int j = 0; j < data.mHeight; j++ )
                    {
                        uint8_t* pix =
                            &data.mBuffer[ 4 *
                                           ( i * static_cast< uint64_t >( data.mHeight ) + j ) ];
                        const uint8_t gray = std::max( pix[ 0 ], std::max( pix[ 1 ], pix[ 2 ] ) );
                        pix[ 0 ] = pix[ 1 ] = pix[ 2 ] = gray;
                    }
                }
            }
        };
        auto calculateAlphaIfNeed = []( FTextureBuffer& data, bool redIsAlpha ) {
            if( redIsAlpha )
            {
                for( int i = 0; i < data.mWidth; i++ )
                {
                    for( int j = 0; j < data.mHeight; j++ )
                    {
                        uint8_t* pix =
                            &data.mBuffer[ 4 *
                                           ( i * static_cast< uint64_t >( data.mHeight ) + j ) ];

                        // alpha = red
                        pix[ 3 ] = pix[ 0 ];
                    }
                }
            }
        };

        if( m_created )
        {
            return;
        }

        m_created = true;
        m_name    = MakeTextureName( src );

        if( m_name.empty() || !src.GetTexture() )
        {
            assert( 0 );
            return;
        }

        auto texbuffer = src.GetTexture()->CreateTexBuffer( translation, flags | CTF_ProcessData );
        desaturateIfNeed( texbuffer, flags, fileSystem.GetFileShortName( src.GetSourceLump() ) );
        calculateAlphaIfNeed( texbuffer, renderStyle.Flags & STYLEF_RedIsAlpha );

        if( texbuffer.mWidth <= 0 || texbuffer.mHeight <= 0 )
        {
            assert( 0 );
            return;
        }

        const bool exportseparately = m_name.starts_with( "vx_" );

        auto details = RgOriginalTextureDetailsEXT{
            .sType  = RG_STRUCTURE_TYPE_ORIGINAL_TEXTURE_DETAILS_EXT,
            .pNext  = nullptr,
            .flags  = exportseparately ? RG_ORIGINAL_TEXTURE_INFO_FORCE_EXPORT_AS_EXTERNAL : 0u,
            .format = flags & CTF_Indexed ? RG_FORMAT_R8_SRGB : RG_FORMAT_B8G8R8A8_SRGB,
        };

        auto info = RgOriginalTextureInfo{
            .sType        = RG_STRUCTURE_TYPE_ORIGINAL_TEXTURE_INFO,
            .pNext        = &details,
            .pTextureName = m_name.c_str(),
            .pPixels      = texbuffer.mBuffer,
            .size         = { static_cast< uint32_t >( texbuffer.mWidth ),
                              static_cast< uint32_t >( texbuffer.mHeight ) },
            .filter       = RG_SAMPLER_FILTER_AUTO,
            .addressModeU = RG_SAMPLER_ADDRESS_MODE_REPEAT, //  rtclamp_x( clampmode ),
            .addressModeV = RG_SAMPLER_ADDRESS_MODE_REPEAT, //  rtclamp_y( clampmode ),
        };

        RgResult r = rt.rgProvideOriginalTexture( &info );
        RG_CHECK( r );
    }

    ~RTHardwareTexture() override
    {
// HACKHACK: TODO: why this is being called only on Release? (and destroying actually used textures)
#if 0
        RgResult r = rt.rgMarkOriginalTextureAsDeleted( m_name.c_str() );
        RG_CHECK( r );
#endif
    }

    auto GetRTName() const -> const char*
    {
        return m_created && !m_name.empty() ? m_name.c_str() : nullptr;
    }

private:
    static auto MakeTextureName( FGameTexture& fgametex ) -> std::string
    {
        // highest priority: FGameTexture name
        if( !fgametex.GetName().IsEmpty() )
        {
            return fgametex.GetName().GetChars();
        }

        // if no lump name, stringify the image ID;
        // this is undesirable for textures that require a replacement
        // (which are found by texname; and because ID is assigned at runtime,
        // replacements can't be found correctly)
        if( FTexture* ftex = fgametex.GetTexture() )
        {
            if( FImageSource* imgsrc = ftex->GetImage() )
            {
                // MSVC's std::string has 16 chars inlined,
                // so no allocation should happen
                return std::to_string( imgsrc->GetId() );
            }
        }

        assert( 0 );
        return {};
    }

private:
    bool        m_created{ false };
    std::string m_name{};
};


template< typename T >
void ApplyMat33ToVec3_row( const T row_mat[ 3 ][ 3 ], float ( &v )[ 3 ] )
{
    RgFloat3D r;
    for( int i = 0; i < 3; i++ )
    {
        r.data[ i ] = row_mat[ i ][ 0 ] * T( v[ 0 ] ) + row_mat[ i ][ 1 ] * T( v[ 1 ] ) +
                      row_mat[ i ][ 2 ] * T( v[ 2 ] );
    }
    v[ 0 ] = r.data[ 0 ];
    v[ 1 ] = r.data[ 1 ];
    v[ 2 ] = r.data[ 2 ];
}

template< typename T >
RgFloat4D ApplyMat44ToVec4( const T column_mat[ 4 ][ 4 ], const RgFloat4D& vs )
{
    const auto* v = vs.data;
    RgFloat4D   r;
    for( int i = 0; i < 4; i++ )
    {
        r.data[ i ] = column_mat[ 0 ][ i ] * T( v[ 0 ] ) + column_mat[ 1 ][ i ] * T( v[ 1 ] ) +
                      column_mat[ 2 ][ i ] * T( v[ 2 ] ) + column_mat[ 3 ][ i ] * T( v[ 3 ] );
    }
    return r;
}

template< typename T >
RgFloat4D ApplyMat44ToVec4( const T* column_mat, const RgFloat4D& vs )
{
    return ApplyMat44ToVec4< T >( reinterpret_cast< const T( * )[ 4 ] >( column_mat ), vs );
}

RgFloat3D FromHomogeneous( const RgFloat4D& v )
{
    return RgFloat3D{ v.data[ 0 ] / v.data[ 3 ],
                      v.data[ 1 ] / v.data[ 3 ],
                      v.data[ 2 ] / v.data[ 3 ] };
}

class RTRenderState : public FRenderState
{
public:
    explicit RTRenderState( RTFrameBuffer* parent ) : m_fb( parent ) {}
    virtual ~RTRenderState() = default;

    void RT_BeginFrame()
    {
        rtstate.reset();
        m_weaponDrawCallIndex = 0;
    }

    bool IsCurrentDrawIgnored() const
    {
        return rtstate.is< RtPrim::Ignored >() || mTextureMode == TM_FOGLAYER;
    }

    bool IsSpectre() const
    {
        switch( mRenderStyle.BlendOp )
        {
            case STYLEOP_Fuzz:
            case STYLEOP_FuzzOrAdd:
            case STYLEOP_FuzzOrSub:
            case STYLEOP_FuzzOrRevSub:
            case STYLEOP_Shadow: return true;
            default: return false;
        }
    }

    void Draw( int dt, int index, int count, bool apply = true ) override
    {
        if( IsCurrentDrawIgnored() )
        {
            return;
        }

        assert( count > 0 );

        const uint32_t* pIndices   = nullptr;
        uint32_t        indexCount = 0;

        bool islines = false;

        switch( dt )
        {
            case DT_Points: assert( 0 ); return;
            case DT_Lines: islines = true; break;
            case DT_Triangles:
                // indices are sequential, just use vertex array
                break;
            case DT_TriangleFan:
                rt.rgUtilScratchGetIndices(
                    RG_UTIL_IM_SCRATCH_TOPOLOGY_TRIANGLE_FAN, count, &pIndices, &indexCount );
                break;
            case DT_TriangleStrip:
                rt.rgUtilScratchGetIndices(
                    RG_UTIL_IM_SCRATCH_TOPOLOGY_TRIANGLE_STRIP, count, &pIndices, &indexCount );
                break;
            default: break;
        }

        auto vb = static_cast< RTVertexBuffer* >( mVertexBuffer );
        if( !vb )
        {
            assert( 0 );
            return;
        }
        assert( rtstate.is< RtPrim::Sky >() == vb->IsSky() );

        InternalDraw( vb->AccessFormatted( mVertexOffsets[ 0 ] + index, count ),
                      std::span{ pIndices, indexCount },
                      vb->IsUI(),
                      islines );
    }

    void DrawIndexed( int dt, int index, int count, bool apply = true ) override
    {
        if( IsCurrentDrawIgnored() )
        {
            return;
        }

        assert( dt == DT_Triangles );
        if( count <= 0 )
        {
            // E3M2 fails
            return;
        }

        auto vb = static_cast< RTVertexBuffer* >( mVertexBuffer );
        if( !vb )
        {
            assert( 0 );
            return;
        }
        assert( rtstate.is< RtPrim::Sky >() == vb->IsSky() );

        auto ib = static_cast< RTIndexBuffer* >( mIndexBuffer );
        if( !ib )
        {
            assert( 0 );
            return;
        }

        auto indices = ib->AccessFormatted( index, count );

        auto [ vertFirst, vertCount ] = RTIndexBuffer::CalcFirstVertexAndVertexCount( indices );

        InternalDraw( vb->AccessFormatted( mVertexOffsets[ 0 ] + vertFirst, vertCount ),
                      ib->MakeWithNewFirstIndex( indices, vertFirst ),
                      vb->IsUI() );
    }

    void ClearScreen() override {}
    bool SetDepthClamp( bool on ) override { return on; }
    void SetDepthMask( bool on ) override {}
    void SetDepthFunc( int func ) override {}
    void SetDepthRange( float min, float max ) override {}
    void SetColorMask( bool r, bool g, bool b, bool a ) override {}
    void SetStencil( int offs, int op, int flags = -1 ) override {}
    void SetCulling( int mode ) override {}
    void EnableClipDistance( int num, bool state ) override {}
    void Clear( int targets ) override {}
    void EnableStencil( bool on ) override {}
    void SetScissor( int x, int y, int w, int h ) override {}
    void SetViewport( int x, int y, int w, int h ) override
    {
        m_viewport = RgViewport{
            .x        = float( x ),
            .y        = float( y ),
            .width    = float( w ),
            .height   = float( h ),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
    }
    void EnableDepthTest( bool on ) override {}
    void EnableMultisampling( bool on ) override {}
    void EnableLineSmooth( bool on ) override {}
    void EnableDrawBuffers( int count, bool apply ) override {}

private:
    static bool IsPerspectiveMatrix( const float* m );
    static bool IsLikeIdentity( const float* m );
    static bool IsLikeIdentity( const double* m );

    // If need to calculate a transform at the sprite's bottom.
    bool RequiresTrueTransform() const
    {
        if( rtstate.is< RtPrim::ExportInstance >() )
        {
            // need to make a true one, since gzdoom doesn't provide a world transform
            return !mModelMatrixEnabled;
        }
        return false;
    }

    auto CalculateTrueTransformAndItsVerts( std::span< const RgPrimitiveVertex > originalVerts )
        -> std::pair< RgTransform, std::span< const RgPrimitiveVertex > >
    {
        assert( RequiresTrueTransform() );
        assert( originalVerts.size() == 4 ); // to find a non-sprite without model matrix
        assert( !mModelMatrixEnabled );      // means that vert positions are in a metric space

        // need to offset a bit, to prevent clipping with floor (for glass spectres)
        constexpr float CLIP_FIX_OFFSET = 0.005f;

        const float pivot[] = {
            rtstate.m_lastthingposition.X * ONEGAMEUNIT_IN_METERS,
            rtstate.m_lastthingposition.Y * ONEGAMEUNIT_IN_METERS,
            rtstate.m_lastthingposition.Z * ONEGAMEUNIT_IN_METERS + CLIP_FIX_OFFSET,
        };

        m_tempverts.clear();
        m_tempverts.assign( originalVerts.begin(), originalVerts.end() );

        // make relative to pivot
        for( uint32_t v = 0; v < originalVerts.size(); v++ )
        {
            m_tempverts[ v ].position[ 0 ] -= pivot[ 0 ];
            m_tempverts[ v ].position[ 1 ] -= pivot[ 1 ];
            m_tempverts[ v ].position[ 2 ] -= pivot[ 2 ];
        }

        // un-rotate the angle
        const auto [ pitch, yaw ] = rtstate.get_spriterotation();
        
#if 0 // reference
        Matrix3x4 m;
        m.MakeIdentity();
        m.Rotate( 0, 0, 1, to_deg( yaw ) );
        m.Rotate( 0, 1, 0, to_deg( pitch ) );
#else
        const float cos_pitch = std::cos( pitch );
        const float sin_pitch = std::sin( pitch );
        const float cos_yaw   = std::cos( yaw );
        const float sin_yaw   = std::sin( yaw );

        //     |  cos_pitch, 0, sin_pitch |   | cos_yaw, -sin_yaw, 0 |
        // m = |          0, 1,         0 | x | sin_yaw,  cos_yaw, 0 |
        //     | -sin_pitch, 0, cos_pitch |   |       0,       0,  1 |

        float m[ 3 ][ 3 ] = {
            { cos_yaw * cos_pitch, -sin_yaw, cos_yaw * sin_pitch },
            { sin_yaw * cos_pitch, cos_yaw, sin_yaw * sin_pitch },
            { -sin_pitch, 0, cos_pitch },
        };
#endif
        const float m_inv[ 3 ][ 3 ] = {
            { m[ 0 ][ 0 ], m[ 1 ][ 0 ], m[ 2 ][ 0 ] },
            { m[ 0 ][ 1 ], m[ 1 ][ 1 ], m[ 2 ][ 1 ] },
            { m[ 0 ][ 2 ], m[ 1 ][ 2 ], m[ 2 ][ 2 ] },
        };
        for( auto& v : m_tempverts )
        {
            ApplyMat33ToVec3_row( m_inv, v.position );
        }

        return {
            RgTransform{ {
                { m[ 0 ][ 0 ], m[ 0 ][ 1 ], m[ 0 ][ 2 ], pivot[ 0 ] },
                { m[ 1 ][ 0 ], m[ 1 ][ 1 ], m[ 1 ][ 2 ], pivot[ 1 ] },
                { m[ 2 ][ 0 ], m[ 2 ][ 1 ], m[ 2 ][ 2 ], pivot[ 2 ] },
            } },
            std::span{ m_tempverts },
        };
    }

    auto MakeTransform( bool isSky ) const -> RgTransform
    {
        assert( !RequiresTrueTransform() );

        // also converts to metric
        auto fromGzMatrix = []( const float* m ) {
            return RgTransform{ {
                { m[ 0 ], m[ 4 ], m[ 8 ], m[ 12 ] * ONEGAMEUNIT_IN_METERS },
                { m[ 1 ], m[ 5 ], m[ 9 ], m[ 13 ] * ONEGAMEUNIT_IN_METERS },
                { m[ 2 ], m[ 6 ], m[ 10 ], m[ 14 ] * ONEGAMEUNIT_IN_METERS },
            } };
        };

        // sky has view matrix that is different from main camera, apply it
        if( isSky )
        {
            auto l_unit = []( float f ) {
                return f > +0.5f   ? +1.0f //
                       : f < -0.5f ? -1.0f //
                                   : 0.0f;
            };

            auto skyToMainCameraIrregular =
                VSMatrix::smultMatrix( m_mainCameraView_Inverse, m_view );

            const float* irr = skyToMainCameraIrregular.get();

            const float skyToMainCamera[ 16 ] = {
                l_unit( irr[ 0 ] ), l_unit( irr[ 1 ] ), l_unit( irr[ 2 ] ),  0,
                l_unit( irr[ 4 ] ), l_unit( irr[ 5 ] ), l_unit( irr[ 6 ] ),  0,
                l_unit( irr[ 8 ] ), l_unit( irr[ 9 ] ), l_unit( irr[ 10 ] ), 0,
                irr[ 12 ],          irr[ 13 ],          irr[ 14 ],           1,
            };

            auto skyTransform = mModelMatrix;
            skyTransform.scale( 1, cvar::rt_sky_stretch, 1 );

            auto t = VSMatrix::smultMatrix( skyToMainCamera, skyTransform.get() );
            return fromGzMatrix( t.get() );
        }

        if( mModelMatrixEnabled )
        {
            return fromGzMatrix( mModelMatrix.get() );
        }

        return RG_TRANSFORM_IDENTITY;
    }

    auto MapLightLevel( int lightlevel ) -> float
    {
        assert( lightlevel <= 255 );
        int lmin = std::max< int >( cvar::rt_lightlevel_min, 0 );
        int lmax = std::min< int >( cvar::rt_lightlevel_max, 255 );

        if( lmin >= lmax )
        {
            return 0.0f;
        }
        if( lightlevel <= lmin )
        {
            return 0.0f;
        }
        if( lightlevel >= lmax )
        {
            return 1.0f;
        }
        float t = float( lightlevel - lmin ) / float( lmax - lmin );

        if( std::abs( cvar::rt_lightlevel_exp - 2.f ) < 0.01f )
        {
            return t * t;
        }
        if( std::abs( cvar::rt_lightlevel_exp - 1.f ) < 0.01f )
        {
            return t;
        }
        return std::powf( t, cvar::rt_lightlevel_exp );
    }

    auto MakeFirstPersonQuadInWorldSpace( std::span< const RgPrimitiveVertex > verts )
        -> std::pair< RgTransform, std::span< const RgPrimitiveVertex > >
    {
        if( verts.size() != 4 )
        {
            // assert( 0 );
            return { RgTransform{ RG_TRANSFORM_IDENTITY }, verts };
        }

        const auto  priority = m_weaponDrawCallIndex++;
        const float z        = 0.1f / float( 1 + priority );

        auto toPix = []( const RgPrimitiveVertex& vert ) {
            // because of MakeFormatted...
            return RgFloat2D{
                vert.position[ 0 ] / ONEGAMEUNIT_IN_METERS,
                vert.position[ 2 ] / ONEGAMEUNIT_IN_METERS,
            };
        };

        auto applyViewport = []( const RgViewport& vp, const RgFloat2D& vert ) {
            return RgFloat2D{
                vert.data[ 0 ] / float( vp.width ),
                vert.data[ 1 ] / float( vp.height ),
            };
        };

        // screen space [0,1]
        RgFloat2D scr01[] = {
            applyViewport( m_viewport, toPix( verts[ 0 ] ) ),
            applyViewport( m_viewport, toPix( verts[ 1 ] ) ),
            applyViewport( m_viewport, toPix( verts[ 2 ] ) ),
            applyViewport( m_viewport, toPix( verts[ 3 ] ) ),
        };

        // remap [0,1] to [-1,1] clip space
        RgFloat4D clipspace[] = {
            RgFloat4D{ scr01[ 0 ].data[ 0 ] * 2 - 1, scr01[ 0 ].data[ 1 ] * 2 - 1, z, 1.0f },
            RgFloat4D{ scr01[ 1 ].data[ 0 ] * 2 - 1, scr01[ 1 ].data[ 1 ] * 2 - 1, z, 1.0f },
            RgFloat4D{ scr01[ 2 ].data[ 0 ] * 2 - 1, scr01[ 2 ].data[ 1 ] * 2 - 1, z, 1.0f },
            RgFloat4D{ scr01[ 3 ].data[ 0 ] * 2 - 1, scr01[ 3 ].data[ 1 ] * 2 - 1, z, 1.0f },
        };

        // inverse projection to transform clip space -> view space
        RgFloat4D viewspace[] = {
            ApplyMat44ToVec4( m_mainCameraProjection_Inverse, clipspace[ 0 ] ),
            ApplyMat44ToVec4( m_mainCameraProjection_Inverse, clipspace[ 1 ] ),
            ApplyMat44ToVec4( m_mainCameraProjection_Inverse, clipspace[ 2 ] ),
            ApplyMat44ToVec4( m_mainCameraProjection_Inverse, clipspace[ 3 ] ),
        };

#if 0
        // inverse view to transform view space -> world space
        RgFloat3D worldspace[] = {
            FromHomogeneous( ApplyMat44ToVec4( m_mainCameraView_Inverse, viewspace[ 0 ] ) ),
            FromHomogeneous( ApplyMat44ToVec4( m_mainCameraView_Inverse, viewspace[ 1 ] ) ),
            FromHomogeneous( ApplyMat44ToVec4( m_mainCameraView_Inverse, viewspace[ 2 ] ) ),
            FromHomogeneous( ApplyMat44ToVec4( m_mainCameraView_Inverse, viewspace[ 3 ] ) ),
        };

        m_tempverts.clear();
        m_tempverts.assign( verts.begin(), verts.end() );
        for( uint32_t i = 0; i < std::size( worldspace ); i++ )
        {
            // because of m_mainCameraView_Inverse, m_mainCameraProjection_Inverse,
            // vi_world already have ONEGAMEUNIT_IN_METERS applied
            m_tempverts[ i ].position[ 0 ] = worldspace[ i ].data[ 0 ];
            m_tempverts[ i ].position[ 1 ] = worldspace[ i ].data[ 1 ];
            m_tempverts[ i ].position[ 2 ] = worldspace[ i ].data[ 2 ];
        }
        return m_tempverts;
#else

        // treat m_mainCameraView_Inverse as the transform
        const float* t = m_mainCameraView_Inverse;
        
        auto transform = RgTransform{ {
            { t[ 0 ], t[ 4 ], t[ 8 ], t[ 12 ] },
            { t[ 1 ], t[ 5 ], t[ 9 ], t[ 13 ] },
            { t[ 2 ], t[ 6 ], t[ 10 ], t[ 14 ] },
        } };

        m_tempverts.clear();
        m_tempverts.assign( verts.begin(), verts.end() );
        for( uint32_t i = 0; i < std::size( viewspace ); i++ )
        {
            double w = viewspace[ i ].data[ 3 ];
            w        = std::max( w, 0.00000001 );

            // because of m_mainCameraView_Inverse, m_mainCameraProjection_Inverse,
            // vi_world already have ONEGAMEUNIT_IN_METERS applied
            m_tempverts[ i ].position[ 0 ] = float( viewspace[ i ].data[ 0 ] / w );
            m_tempverts[ i ].position[ 1 ] = float( viewspace[ i ].data[ 1 ] / w );
            m_tempverts[ i ].position[ 2 ] = float( viewspace[ i ].data[ 2 ] / w );
        }
        return { transform, m_tempverts };
#endif
    }

    void InternalDraw( std::span< const RgPrimitiveVertex > verts,
                       std::span< const uint32_t >          indices,
                       const bool                           isUI,
                       const bool                           islines = false )
    {
        assert( RG_PACKED_COLOR_WHITE == rt.rgUtilPackColorByte4D( 255, 255, 255, 255 ) );

        if( islines && !isUI )
        {
            assert( 0 );
            return;
        }

        if( verts.empty() )
        {
            assert( 0 );
            return;
        }

        const char* texname = nullptr;
        if( mTextureEnabled && mMaterial.mMaterial )
        {
            if( FGameTexture* gametex = mMaterial.mMaterial->sourcetex )
            {
                if( FTexture* base = gametex->GetTexture() )
                {
                    if( auto hwtex = static_cast< RTHardwareTexture* >( base->GetHardwareTexture(
                            mMaterial.mTranslation, mMaterial.mMaterial->GetScaleFlags() ) ) )
                    {
                        hwtex->CreateIfWasnt( *gametex,
                                              mMaterial.mClampMode,
                                              mMaterial.mTranslation,
                                              mMaterial.mMaterial->GetScaleFlags(),
                                              mRenderStyle );
                        texname = hwtex->GetRTName();
                    }
                }
            }
        }

        if( !texname && !isUI && !rtstate.is< RtPrim::Sky >() &&
            !rtstate.is< RtPrim::SkyVisibility >() )
        {
            // assert( 0 );
        }

        // TODO: apply texture matrix on gpu
        if( mTextureMatrixEnabled )
        {
            m_tempverts.clear();
            m_tempverts.assign( verts.begin(), verts.end() );

            auto applyTexMatrix = [ & ]( float u, float v ) {
                auto m = [ & ]( int i, int j ) {
                    return mTextureMatrix.get()[ i + j * 4 ];
                };

                return std::pair{
                    m( 0, 0 ) * u + m( 1, 0 ) * v,
                    m( 0, 1 ) * u + m( 1, 1 ) * v,
                };
            };

            for( RgPrimitiveVertex& v : m_tempverts )
            {
                std::tie( v.texCoord[ 0 ], v.texCoord[ 1 ] ) =
                    applyTexMatrix( v.texCoord[ 0 ], v.texCoord[ 1 ] );
            }

            verts = m_tempverts;
        }

        if( rtstate.is< RtPrim::Sky >() && texname )
        {
            m_fb->RT_MarkWasSky();
        }

        RgTransform transform;
        if( rtstate.is< RtPrim::FirstPerson >() )
        {
            std::tie( transform, verts ) = MakeFirstPersonQuadInWorldSpace( verts );
        }
        else if( RequiresTrueTransform() )
        {
            std::tie( transform, verts ) = CalculateTrueTransformAndItsVerts( verts );
        }
        else
        {
            transform = MakeTransform( rtstate.is< RtPrim::Sky >() );
        }

        auto ui = RgMeshPrimitiveSwapchainedEXT{
            .sType       = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_SWAPCHAINED_EXT,
            .pNext       = nullptr,
            .flags       = islines ? uint32_t{ RG_MESH_PRIMITIVE_SWAPCHAINED_DRAW_AS_LINES } : 0,
            .pViewport   = &m_viewport,
            .pView       = m_view,
            .pProjection = m_projection,
            .pViewProjection = nullptr,
        };

        auto l_makeInstanceFlags = [ & ]() -> RgMeshInfoFlags {
            if( rtstate.is< RtPrim::FirstPersonViewer >() )
            {
                return RG_MESH_FIRST_PERSON_VIEWER;
            }
            if( rtstate.is< RtPrim::FirstPerson >() )
            {
                return RG_MESH_FIRST_PERSON;
            }
            return 0;
        };

        auto l_makeSpectreFlags = [ & ]() -> RgMeshInfoFlags {
            if( IsSpectre() )
            {
                bool firstperson = rtstate.is< RtPrim::FirstPersonViewer >() ||
                                   rtstate.is< RtPrim::FirstPerson >();

                // suppress inter-reflection on spectres
                RgMeshInfoFlags fs = firstperson ? 0 : RG_MESH_FORCE_IGNORE_REFRACT_AFTER;

                int mode = firstperson ? *cvar::rt_spectre_invis1 : *cvar::rt_spectre;
                switch( mode )
                {
                    case 1: return fs | RG_MESH_FORCE_GLASS;
                    case 2: return fs | RG_MESH_FORCE_MIRROR;
                    default: return fs | RG_MESH_FORCE_WATER;
                }
            }
            return 0;
        };

        auto mesh = RgMeshInfo{
            .sType = RG_STRUCTURE_TYPE_MESH_INFO,
            .pNext = nullptr,
            .flags =
                l_makeInstanceFlags() | l_makeSpectreFlags() |
                ( rtstate.is< RtPrim::ExportInstance >() ? RG_MESH_EXPORT_AS_SEPARATE_FILE : 0 ),
            .uniqueObjectID = rtstate.get_uniqueid(),
            .pMeshName      = rtstate.is< RtPrim::ExportMap >() ? RT_GetMapName()
                              : rtstate.is< RtPrim::ExportInstance >()
                                  ? rtstate.get_exportinstance_name()
                                  : nullptr,
            .transform      = transform,
            .isExportable =
                rtstate.is< RtPrim::ExportMap >() || rtstate.is< RtPrim::ExportInstance >(),
            .animationTime        = 0.0f,
            .localLightsIntensity = MapLightLevel( rtstate.m_lightlevel ),
        };

        auto makePrimFlags = [ this, &verts ]( bool isUI ) -> RgMeshPrimitiveFlags {
            if( isUI )
            {
                return RG_MESH_PRIMITIVE_TRANSLUCENT;
            }
            if( rtstate.is< RtPrim::Decal >() )
            {
                assert( verts.size() == 4 );
                return RG_MESH_PRIMITIVE_DECAL;
            }
            if( rtstate.is< RtPrim::SkyVisibility >() )
            {
                return RG_MESH_PRIMITIVE_SKY_VISIBILITY;
            }
            if( rtstate.is< RtPrim::Sky >() )
            {
                return RG_MESH_PRIMITIVE_SKY | RG_MESH_PRIMITIVE_TRANSLUCENT;
            }
            if( rtstate.is< RtPrim::Particle >() )
            {
                return RG_MESH_PRIMITIVE_TRANSLUCENT;
            }
            if( rtstate.is< RtPrim::Mirror >() )
            {
                return RG_MESH_PRIMITIVE_MIRROR;
            }
            if( rtstate.is< RtPrim::Glass >() )
            {
                return RG_MESH_PRIMITIVE_GLASS;
            }

            RgMeshPrimitiveFlags add;
            switch( int( cvar::rt_wall_nomv ) )
            {
                case 0: add = 0; break;
                case 2: add = RG_MESH_PRIMITIVE_NO_MOTION_VECTORS; break;
                default:
                    add = rtstate.is< RtPrim::NoMotionVectors >()
                              ? RG_MESH_PRIMITIVE_NO_MOTION_VECTORS
                              : 0;
                    break;
            }

            return ( mAlphaThreshold > 0 ? RG_MESH_PRIMITIVE_ALPHA_TESTED : 0 ) | add;
        };

        // HACKHACK: replacements are ignored if a prim is rasterized, force alpha=1.0
        const bool forcealpha1 = ( mesh.flags & RG_MESH_FORCE_GLASS ) ||
                                 ( mesh.flags & RG_MESH_FORCE_MIRROR ) ||
                                 ( mesh.flags & RG_MESH_FORCE_WATER );

        auto prim = RgMeshPrimitiveInfo{
            .sType = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_INFO,
            .pNext = isUI ? &ui : nullptr,
            .flags = makePrimFlags( isUI ) | RG_MESH_PRIMITIVE_FORCE_EXACT_NORMALS |
                     ( rtstate.is< RtPrim::ExportInvertNormals >()
                           ? RG_MESH_PRIMITIVE_EXPORT_INVERT_NORMALS
                           : 0 ),
            .primitiveIndexInMesh = rtstate.next_primitiveindex(),
            .pVertices            = verts.data(),
            .vertexCount          = static_cast< uint32_t >( verts.size() ),
            .pIndices             = indices.empty() ? nullptr : indices.data(),
            .indexCount           = static_cast< uint32_t >( indices.size() ),
            .pTextureName         = texname,
            .textureFrame         = 0,
            .color =
                rtcolor_multiply( mStreamData.uObjectColor, mStreamData.uVertexColor, forcealpha1 ),
            .emissive =
                ( mRenderStyle.BlendOp == STYLEOP_Add && mRenderStyle.DestAlpha == STYLEALPHA_One )
                    ? cvar::rt_emis_additive_dflt
                    : 0.f,
            .classicLight = lightlevel_to_classic( mLightParms[ 3 ] ),
        };

#ifndef NDEBUG
        if( cvar::_rt_showexportable )
        {
            if( !rtstate.is< RtPrim::ExportMap >() && !isUI )
            {
                return;
            }
        }
#endif

        RgResult r = rt.rgUploadMeshPrimitive( &mesh, &prim );
        RG_CHECK( r );
    }

public:
    void RT_SetMatrices( const VSMatrix& view, const VSMatrix& proj )
    {
        // TODO: only calculate when UI mode;
        //       can those UI elements be with perspective matrix?

        // clang-format off
        constexpr static float vkcorrection[] = {
            1,  0,    0, 0,
            0, -1,    0, 0,
            0,  0, 0.5f, 0,
            0,  0, 0.5f, 1,
        };
        // clang-format on

        auto correctedProj = VSMatrix::smultMatrix( vkcorrection, proj.get() );
        memcpy( m_projection, correctedProj.get(), sizeof( float ) * 16 );
        memcpy( m_view, view.get(), sizeof( float ) * 16 );
    }

    void RT_AddMainCamera( const FRenderViewpoint& viewpoint )
    {
        const auto [ up, right, forward ] = RT_MakeUpRightForwardVectors( viewpoint.Angles );

        const float pixelstretch =
            viewpoint.ViewLevel ? viewpoint.ViewLevel->info->pixelstretch : 1.0f;

        const auto aspectRatio = r_viewwindow.WidescreenRatio;
        const auto fovRatio    = r_viewwindow.WidescreenRatio >= 1.3f ? 1.333333f : aspectRatio;

        const auto fovy = static_cast< float >(
            2.0 * std::atan( std::tan( viewpoint.FieldOfView.Radians() / 2.0 ) /
                             static_cast< double >( fovRatio ) ) );


        auto readback = RgCameraInfoReadbackEXT{
            .sType = RG_STRUCTURE_TYPE_CAMERA_INFO_READ_BACK_EXT,
        };

        auto info = RgCameraInfo{
            .sType       = RG_STRUCTURE_TYPE_CAMERA_INFO,
            .pNext       = &readback,
            .flags       = 0,
            .position    = { float( viewpoint.Pos.X ) * ONEGAMEUNIT_IN_METERS,
                             float( viewpoint.Pos.Y ) * ONEGAMEUNIT_IN_METERS,
                             float( viewpoint.Pos.Z ) * ONEGAMEUNIT_IN_METERS },
            .up          = up,
            .right       = right,
            .fovYRadians = fovy,
            .aspect      = aspectRatio * pixelstretch,
            .cameraNear  = cvar::rt_znear,
            .cameraFar   = cvar::rt_zfar,
        };

        RgResult r = rt.rgUploadCamera( &info );
        RG_CHECK( r );


        // for first-person weapons
        memcpy( m_mainCameraView_Inverse, readback.viewInverse, 16 * sizeof( float ) );
        memcpy( m_mainCameraProjection_Inverse, readback.projectionInverse, 16 * sizeof( float ) );
        static_assert( sizeof m_mainCameraView_Inverse == sizeof readback.viewInverse );
        static_assert( sizeof m_mainCameraProjection_Inverse == sizeof readback.projectionInverse );


        RT_AddFlashlight( info.position, forward, up, right );
        RT_AddMuzzleFlash( viewpoint.ViewActor, viewpoint.extralight, info.position, forward, up );
    }

    void RT_AddFlashlight( const RgFloat3D& basePosition,
                           const RgFloat3D& forward,
                           const RgFloat3D& up,
                           const RgFloat3D& right )
    {
        auto enabled = []() {
            if( cvar::rt_pw_lightamp == 2 )
            {
                if( RT_CalcPowerupFlags() & RT_POWERUP_FLAG_FLASHLIGHT_BIT )
                {
                    return true;
                }
            }
            if( cvar::rt_flsh )
            {
                return true;
            }
            return false;
        };

        if( !enabled() )
        {
            return;
        }

        auto pos = gzvec3( basePosition );
        {
            pos += gzvec3( up ) * cvar::rt_flsh_u;
            pos += gzvec3( right ) * cvar::rt_flsh_r;
            pos += gzvec3( forward ) * cvar::rt_flsh_f;
        }

        auto target = gzvec3( basePosition ) + 20 * gzvec3( forward );
        auto dir    = ( target - pos ).Unit();

        auto spot = RgLightSpotEXT{
            .sType      = RG_STRUCTURE_TYPE_LIGHT_SPOT_EXT,
            .pNext      = nullptr,
            .color      = RG_PACKED_COLOR_WHITE,
            .intensity  = cvar::rt_flsh_intensity,
            .position   = { pos.X, pos.Y, pos.Z },
            .direction  = { dir.X, dir.Y, dir.Z },
            .radius     = cvar::rt_flsh_radius,
            .angleOuter = to_rad( cvar::rt_flsh_angle ),
            .angleInner = 0,
        };

        auto light = RgLightInfo{
            .sType        = RG_STRUCTURE_TYPE_LIGHT_INFO,
            .pNext        = &spot,
            .uniqueID     = FlashlightLightId,
            .isExportable = false,
        };

        RgResult r = rt.rgUploadLight( &light );
        RG_CHECK( r );
    }

    void RT_AddMuzzleFlash( AActor*          viewactor,
                            int              extralight,
                            const RgFloat3D& basePosition,
                            const RgFloat3D& forward,
                            const RgFloat3D& up )
    {
        if( extralight <= 0 || !cvar::rt_mzlflsh || !viewactor || !viewactor->Sector )
        {
            return;
        }

        auto desiredPos = gzvec3( basePosition );
        {
            desiredPos += gzvec3( up ) * cvar::rt_mzlflsh_u;
            desiredPos += gzvec3( forward ) * cvar::rt_mzlflsh_f;
        }

        FVector3 pos;
        {
            // metric to game units
            auto units_desiredPos   = DVector3{ desiredPos } / double{ ONEGAMEUNIT_IN_METERS };
            auto units_basePosition = gzvec3d( basePosition ) / double{ ONEGAMEUNIT_IN_METERS };

            auto dir = units_desiredPos - units_basePosition;
            auto len = dir.Length();

            if( len > 0.01 )
            {
                dir /= len;

                float hitT = 1.0f;

                FTraceResults trace;
                if( Trace( units_basePosition,
                           viewactor->Sector,
                           dir,
                           len,
                           0,
                           0,
                           viewactor,
                           trace,
                           TRACE_NoSky ) )
                {
                    if( trace.HitType != TRACE_HitNone )
                    {
                        hitT = float( ( trace.HitPos - units_basePosition ).Length() / len );
                        // hit point must be between base and desired positions
                        assert( hitT >= 0 && hitT <= 1 );
                    }
                }

                hitT *= std::clamp( float( cvar::rt_mzlflsh_offset ), 0.0f, 1.0f );

                // lerp
                pos = gzvec3( basePosition ) + hitT * ( desiredPos - gzvec3( basePosition ) );
            }
            else
            {
                pos = gzvec3( basePosition );
            }
        }

        auto sph = RgLightSphericalEXT{
            .sType     = RG_STRUCTURE_TYPE_LIGHT_SPHERICAL_EXT,
            .pNext     = nullptr,
            .color     = cvarcolor_to_rtcolor( cvar::rt_mzlflsh_color ),
            .intensity = cvar::rt_mzlflsh_intensity,
            .position  = { pos.X, pos.Y, pos.Z },
            .radius    = cvar::rt_mzlflsh_radius,
        };

        auto light = RgLightInfo{
            .sType        = RG_STRUCTURE_TYPE_LIGHT_INFO,
            .pNext        = &sph,
            .uniqueID     = MuzzleFlashLightId,
            .isExportable = false,
        };

        RgResult r = rt.rgUploadLight( &light );
        RG_CHECK( r );
    }

private:
    RgViewport m_viewport{};
    float      m_view[ 16 ]{};
    float      m_projection[ 16 ]{};

    float m_mainCameraView_Inverse[ 16 ]{};
    float m_mainCameraProjection_Inverse[ 16 ]{};

    uint32_t m_weaponDrawCallIndex{ 0 }; // to z-sort weapon sprites

    std::vector< RgPrimitiveVertex > m_tempverts{};

public:
    RTFrameBuffer* m_fb{ nullptr };
};



class RTDataBuffer
    : public IDataBuffer
    , public VectorAsBuffer
{
    void BindRange( FRenderState* state, size_t start, size_t length ) override
    {
        auto hwstate = static_cast< RTRenderState* >( state );

        // ugly way to fetch viewpoint info
        if( this == hwstate->m_fb->mViewpoints->DataBuffer() )
        {
            const HWViewpointUniforms& vp = hwstate->m_fb->mViewpoints->FetchViewpoint( start );
            hwstate->RT_SetMatrices( vp.mViewMatrix, vp.mProjectionMatrix );
        }
    }
};



void RT_Print( const char* pMessage, RgMessageSeverityFlags flags, void* pUserData )
{
    if( !pMessage )
    {
        DPrintf( DMSG_ERROR, "RT_Print: pMessage is NULL\n" );
        return;
    }

    if( flags & RG_MESSAGE_SEVERITY_ERROR )
    {
        DPrintf( DMSG_ERROR, "%s\n", pMessage );

#ifdef WIN32
        static bool g_breakOnError = true;
        if( g_breakOnError )
        {
            auto msg = std::string_view{ pMessage };
            auto str = std::format( "{}{}\n"
                                    "\n\'Abort\' to exit the game."
                                    "\n\'Retry\' to skip only this error message."
                                    "\n\'Ignore\' to ignore all such error messages.",
                                    msg,
                                    msg.ends_with( '.' ) ? "" : "." );

            int ok = MessageBoxA( nullptr,
                                  str.c_str(), // null-terminated
                                  "Renderer Error",
                                  MB_ABORTRETRYIGNORE | MB_DEFBUTTON2 | MB_ICONERROR );
            switch( ok )
            {
                case IDIGNORE: g_breakOnError = false; break;
                case IDRETRY: break;
                case IDABORT:
                default: exit( -1 );
            }
        }
#endif
    }
    else if( flags & RG_MESSAGE_SEVERITY_WARNING )
    {
        DPrintf( DMSG_WARNING, "%s\n", pMessage );
    }
    else if( flags & RG_MESSAGE_SEVERITY_INFO )
    {
        DPrintf( DMSG_NOTIFY, "%s\n", pMessage );
    }
    else
    {
        DPrintf( DMSG_SPAMMY, "%s\n", pMessage );
    }
}

} // anonymous namespace



//
//
//
//
//
//



RG_D3D12CORE_HELPER( "rt/" )

Win32RTVideo::Win32RTVideo()
{
    extern std::atomic_bool g_continueMain;
    extern std::atomic_bool g_forceLnchThreadStop;
    while( !g_continueMain )
    {
    }
    if( g_forceLnchThreadStop.load() )
    {
        exit( 1 );
    }

    // warn if no needed dll-s
    if( !Args->CheckParm( "-nodllcheck" ) )
    {
        enum rt_feature_flag_t
        {
            RT_FEATURE_FSR2     = 1,
            RT_FEATURE_FSR3_FG  = 2,
            RT_FEATURE_DLSS2    = 4,
            RT_FEATURE_DLSS3_FG = 8,
        };

        const std::pair< std::filesystem::path, int > dlls[] = {
            { "rt/bin/D3D12Core.dll", RT_FEATURE_FSR3_FG | RT_FEATURE_DLSS3_FG },
            { "rt/bin/nvngx_dlss.dll", RT_FEATURE_DLSS2 },
            { "rt/bin/nvngx_dlssg.dll", RT_FEATURE_DLSS3_FG },
            { "rt/bin/NvLowLatencyVk.dll", RT_FEATURE_DLSS3_FG },
            { "rt/bin/sl.dlss.dll", RT_FEATURE_DLSS3_FG },
            { "rt/bin/sl.dlss_g.dll", RT_FEATURE_DLSS3_FG },
            { "rt/bin/sl.reflex.dll", RT_FEATURE_DLSS3_FG },
            { "rt/bin/sl.common.dll", RT_FEATURE_DLSS3_FG },
            { "rt/bin/sl.interposer.dll", RT_FEATURE_DLSS3_FG },
            { "rt/bin/ffx_fsr2_x64.dll", RT_FEATURE_FSR2 },
            { "rt/bin/ffx_fsr3_x64.dll", RT_FEATURE_FSR3_FG },
            { "rt/bin/ffx_fsr3upscaler_x64.dll", RT_FEATURE_FSR3_FG },
            { "rt/bin/ffx_frameinterpolation_x64.dll", RT_FEATURE_FSR3_FG },
            { "rt/bin/ffx_opticalflow_x64.dll", RT_FEATURE_FSR3_FG },
            { "rt/bin/ffx_backend_dx12_x64.dll", RT_FEATURE_FSR3_FG },
            { "rt/bin/ffx_backend_vk_x64.dll", RT_FEATURE_FSR2 | RT_FEATURE_FSR3_FG },
        };

        auto failedPaths    = std::string{};
        int  failedFeatures = 0;
        for( const auto& [ dll, feature ] : dlls )
        {
            if( !exists( dll ) )
            {
                failedPaths += "    " + dll.filename().string() + '\n';
                failedFeatures |= feature;
            }
        }

        if( !failedPaths.empty() )
        {
            auto msg = std::string{};

            if( failedFeatures == 0 )
            {
                msg = "Some features will NOT be available!";
            }
            else
            {
                msg = "These features will NOT be available!\n";
                // clang-format off
                if( failedFeatures & RT_FEATURE_DLSS3_FG) msg += "    NVIDIA DLSS3 (AI Frame Generation)\n";
                if( failedFeatures & RT_FEATURE_DLSS2   ) msg += "    NVIDIA DLSS2 (AI Upscaling)\n";
                if( failedFeatures & RT_FEATURE_FSR3_FG ) msg += "    AMD FSR 3 (Frame Generation)\n";
                if( failedFeatures & RT_FEATURE_FSR2    ) msg += "    AMD FSR 2 (Upscaling)\n";
                // clang-format on
            }

            msg += "\nBecause the folder \'rt/bin/\' doesn't contain:\n";
            msg += failedPaths;
            msg += "\n(To suppress this warning, use \'-nodllcheck\' argument)";

            MessageBoxA( nullptr, msg.c_str(), "DLL check failure", MB_ICONEXCLAMATION | MB_OK );

            // TODO: add a link to the download page
            int r = MessageBoxA( nullptr,
                                 "\'GZDoom: Ray Traced\' will NOT have a full feature set...\n\n"
                                 "Are you sure you want to continue?",
                                 "DLL check failure",
                                 MB_ICONEXCLAMATION | MB_YESNO );
            if( r != IDYES )
            {
                exit( -1 );
            }
        }
    }

    rt = RgInterface{};

#ifdef WIN32
    auto win32Info = RgWin32SurfaceCreateInfo{
        .hinstance = GetModuleHandle( NULL ),
        .hwnd      = mainwindow.GetHandle(),
    };
#else
    RgXlibSurfaceCreateInfo x11Info = { .dpy    = wmInfo.info.x11.display,
                                        .window = wmInfo.info.x11.window };
#endif

    auto info = RgInstanceCreateInfo
    {
        .sType = RG_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pNext = NULL,

        .version = RG_RTGL_VERSION_API, .sizeOfRgInterface = sizeof( RgInterface ),

        .pAppName = "GZDoom", .pAppGUID = "8cbd354f-38d3-4173-92b9-c16b5a210b37",

#if WIN32
        .pWin32SurfaceInfo = &win32Info,
#else
        .pXlibSurfaceCreateInfo  = &x11Info,
#endif

        .pOverrideFolderPath = "rt/",

        .pfnPrint = RT_Print, .pUserPrintData = nullptr,
        .allowedMessages =
            Args->CheckParm( "-rtdebug" )
                ? RgMessageSeverityFlags{ RG_MESSAGE_SEVERITY_VERBOSE | RG_MESSAGE_SEVERITY_INFO |
                                          RG_MESSAGE_SEVERITY_WARNING | RG_MESSAGE_SEVERITY_ERROR }
                : RgMessageSeverityFlags{ 0 },

        .primaryRaysMaxAlbedoLayers = 1, .indirectIlluminationMaxAlbedoLayers = 1,

        .replacementsMaxVertexCount = 32 * 1024 * 1024, .dynamicMaxVertexCount = 2 * 1024 * 1024,

        .rayCullBackFacingTriangles = 0,
        .allowTexCoordLayer1 = false, .allowTexCoordLayer2 = false, .allowTexCoordLayer3 = false,

        .lightmapTexCoordLayerIndex = 1,

        .rasterizedMaxVertexCount = 1 << 20, .rasterizedMaxIndexCount = 1 << 21,
        .rasterizedVertexColorGamma = true,

        .rasterizedSkyCubemapSize = 256,

        .textureSamplerForceMinificationFilterLinear = true,
        .textureSamplerForceNormalMapFilterLinear    = true,

        .pbrTextureSwizzling = RG_TEXTURE_SWIZZLING_NULL_ROUGHNESS_METALLIC,

        .effectWipeIsUsed = true,

        .worldUp = { 0, 0, 1 }, .worldForward = { 0, 1, 0 }, .worldScale = 1.0f,

        .importedLightIntensityScaleDirectional = 1.0f / 50,
        .importedLightIntensityScaleSphere      = 1.0f / 500,
        .importedLightIntensityScaleSpot        = 1.0f / 500,
    };

#ifndef NDEBUG
    constexpr bool isdebug = true;
#else
    constexpr bool isdebug = false;
#endif

    RgResult r = rgLoadLibraryAndCreate( &info, isdebug, &rt, nullptr );
    if( r != RG_RESULT_SUCCESS )
    {
        auto msg = std::string{ "RgResult code: " };

        switch( r )
        {
            case RG_RESULT_CANT_FIND_DYNAMIC_LIBRARY:
                msg = isdebug ? "Can't find \'rt/bin/debug/RTGL1.dll\' file"
                               : "Can't find \'rt/bin/RTGL1.dll\' file";
                break;
            case RG_RESULT_CANT_FIND_ENTRY_FUNCTION_IN_DYNAMIC_LIBRARY:
                msg =
                    isdebug
                        ? "Can't find rgCreateInstance function in \'rt/bin/debug/RTGL1.dll\'"
                        : "Can't find rgCreateInstance function in \'rt/bin/RTGL1.dll\'";
                break;

            // clang-format off
            case RG_RESULT_NOT_INITIALIZED:                     msg += "RG_RESULT_NOT_INITIALIZED";                     break;
            case RG_RESULT_ALREADY_INITIALIZED:                 msg += "RG_RESULT_ALREADY_INITIALIZED";                 break;
            case RG_RESULT_GRAPHICS_API_ERROR:                  msg += "RG_RESULT_GRAPHICS_API_ERROR";                  break;
            case RG_RESULT_INTERNAL_ERROR:                      msg += "RG_RESULT_INTERNAL_ERROR";                      break;
            case RG_RESULT_CANT_FIND_SUPPORTED_PHYSICAL_DEVICE: msg += "RG_RESULT_CANT_FIND_SUPPORTED_PHYSICAL_DEVICE"; break;
            case RG_RESULT_FRAME_WASNT_STARTED:                 msg += "RG_RESULT_FRAME_WASNT_STARTED";                 break;
            case RG_RESULT_FRAME_WASNT_ENDED:                   msg += "RG_RESULT_FRAME_WASNT_ENDED";                   break;
            case RG_RESULT_WRONG_FUNCTION_CALL:                 msg += "RG_RESULT_WRONG_FUNCTION_CALL";                 break;
            case RG_RESULT_WRONG_FUNCTION_ARGUMENT:             msg += "RG_RESULT_WRONG_FUNCTION_ARGUMENT";             break;
            case RG_RESULT_WRONG_STRUCTURE_TYPE:                msg += "RG_RESULT_WRONG_STRUCTURE_TYPE";                break;
            case RG_RESULT_ERROR_CANT_FIND_HARDCODED_RESOURCES: msg += "RG_RESULT_ERROR_CANT_FIND_HARDCODED_RESOURCES"; break;
            case RG_RESULT_ERROR_CANT_FIND_SHADER:              msg += "RG_RESULT_ERROR_CANT_FIND_SHADER";              break;
            case RG_RESULT_ERROR_MEMORY_ALIGNMENT:              msg += "RG_RESULT_ERROR_MEMORY_ALIGNMENT";              break;
            case RG_RESULT_ERROR_NO_VULKAN_EXTENSION:           msg += "RG_RESULT_ERROR_NO_VULKAN_EXTENSION";           break;
                // clang-format on

            default: msg += std::to_string( r ); break;
        }

        MessageBoxA(
            nullptr, msg.c_str(), "Failed to initialize RT renderer", MB_ICONEXCLAMATION | MB_OK );
        exit( -1 );
    }

    // on first start, try to set DLSS, if available
    if( cvar::rt_firststart )
    {
        if( rt.rgUtilIsUpscaleTechniqueAvailable( RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS, //
                                                  RG_FRAME_GENERATION_MODE_OFF,
                                                  nullptr ) )
        {
            cvar::rt_upscale_dlss = 2;
            cvar::rt_upscale_fsr2 = 0;
            cvar::rt_ef_vintage   = 0;
        }
        else if( rt.rgUtilIsUpscaleTechniqueAvailable( RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2, //
                                                       RG_FRAME_GENERATION_MODE_OFF,
                                                       nullptr ) )
        {
            cvar::rt_upscale_dlss = 0;
            cvar::rt_upscale_fsr2 = 2;
            cvar::rt_ef_vintage   = 0;
        }
        else
        {
            cvar::rt_upscale_dlss = 0;
            cvar::rt_upscale_fsr2 = 0;
            cvar::rt_ef_vintage   = RT_VINTAGE_480_DITHER;
        }
    }
}

DFrameBuffer* Win32RTVideo::CreateFrameBuffer()
{
    return new RTFrameBuffer{ m_hMonitor, vid_fullscreen };
}

void Win32RTVideo::Shutdown()
{
    if( !rt.rgDestroyInstance )
    {
        return;
    }

    RgResult r = rt.rgDestroyInstance();
    if( r != RG_RESULT_SUCCESS )
    {
        MessageBoxA(
            nullptr, "rgDestroyAndUnloadLibrary has failed", "Fail", MB_ICONEXCLAMATION | MB_OK );
        exit( -1 );
    }

    rt = {};
}

#ifdef _WIN32
std::atomic< HWND > g_msgbox_parent{};
#endif

void RT_ShowWarningMessageBox( const char* msg )
{
#ifdef _WIN32
    MessageBoxA( g_msgbox_parent.load(), msg, "Warning - Ray Tracing", MB_ICONEXCLAMATION | MB_OK );
#else
    assert( 0 );
#endif
}

//
//
//

auto RT_GetCurrentTime() -> double
{
    auto ns = []() {
        using namespace std::chrono;
        return duration_cast< nanoseconds >( steady_clock::now().time_since_epoch() ).count();
    };

    static int64_t startupTimeNS = ns();
    return static_cast< double >( ns() - startupTimeNS ) / 1000000000.0;
}

auto RT_GetVramUsage( bool* ok ) -> const char*
{
    const RgUtilMemoryUsage vram = rt.rgUtilRequestMemoryUsage();

    if( ok )
    {
        // < 80% is ok
        *ok = ( vram.vramUsed <= 0.8 * vram.vramTotal );
    }

    static char buf[ 64 ];
    snprintf( buf,
              std::size( buf ),
              "%d / %d MB",
              int( std::round( double( vram.vramUsed ) / 1024 / 1024 ) ),
              int( std::round( double( vram.vramTotal ) / 1024 / 1024 ) ) );

    buf[ std::size( buf ) - 1 ] = '\0';
    return buf;
}

namespace
{

RgExtent2D RT_GetCurrentWindowSize()
{
    return {
        static_cast< uint32_t >( screen->GetWidth() ),
        static_cast< uint32_t >( screen->GetHeight() ),
    };
}

void RT_ResolutionToRtgl( RgStartFrameRenderResolutionParams* dst, const RgExtent2D winsize )
{
    const auto aspect =
        static_cast< double >( winsize.width ) / static_cast< double >( winsize.height );

    if( cvar::rt_renderscale > 0.2f )
    {
        auto scale = std::clamp( double( *cvar::rt_renderscale ), 0.2, 1.0 );

        dst->customRenderSize.width    = static_cast< uint32_t >( winsize.width * scale );
        dst->customRenderSize.height   = static_cast< uint32_t >( winsize.height * scale );
        dst->pixelizedRenderSizeEnable = false;

        return;
    }
    else
    {
        if( int{ cvar::rt_ef_vintage } != RT_VINTAGE_OFF )
        {
            uint32_t h_pixelized = 0;
            uint32_t h_render    = 0;

            switch( int{ cvar::rt_ef_vintage } )
            {
                case RT_VINTAGE_200:
                case RT_VINTAGE_200_DITHER:
                    h_pixelized = 200;
                    h_render    = 400;
                    break;

                case RT_VINTAGE_480:
                case RT_VINTAGE_480_DITHER:
                    h_pixelized = 480;
                    h_render    = 600;
                    break;

                case RT_VINTAGE_CRT:
                case RT_VINTAGE_VHS:
                case RT_VINTAGE_VHS_CRT:
                    h_pixelized = 480;
                    h_render    = 480;
                    break;

                default:
                    cvar::rt_ef_vintage            = 0;
                    dst->customRenderSize          = winsize;
                    dst->pixelizedRenderSizeEnable = false;
                    return;
            }

            assert( h_render > 0 && h_pixelized > 0 );

            dst->pixelizedRenderSize.height = h_pixelized;
            dst->pixelizedRenderSize.width  = static_cast< uint32_t >( h_pixelized * aspect );
            dst->pixelizedRenderSizeEnable  = true;
            dst->customRenderSize.height    = h_render;
            dst->customRenderSize.width     = static_cast< uint32_t >( h_render * aspect );

            return;
        }
    }

    dst->customRenderSize          = winsize;
    dst->pixelizedRenderSizeEnable = false;
}

auto RT_GetSharpenTechniqueFromCvar( bool dlssOrFsr2 ) -> RgRenderSharpenTechnique
{
    switch( cvar::rt_sharpen )
    {
        case 3: return RG_RENDER_SHARPEN_TECHNIQUE_NONE;
        case 2: return RG_RENDER_SHARPEN_TECHNIQUE_AMD_CAS;
        case 1: return RG_RENDER_SHARPEN_TECHNIQUE_NAIVE;
        default: {
            if( dlssOrFsr2 )
            {
                return RG_RENDER_SHARPEN_TECHNIQUE_AMD_CAS;
            }
            // to accentuate a chunky look, because of the linear (not nearest) downscale mode
            switch( cvar::rt_ef_vintage )
            {
                case RT_VINTAGE_CRT:
                case RT_VINTAGE_VHS:
                case RT_VINTAGE_VHS_CRT: return RG_RENDER_SHARPEN_TECHNIQUE_NAIVE;
                case RT_VINTAGE_200:
                case RT_VINTAGE_200_DITHER:
                case RT_VINTAGE_480:
                case RT_VINTAGE_480_DITHER: return RG_RENDER_SHARPEN_TECHNIQUE_AMD_CAS;
                default: return RG_RENDER_SHARPEN_TECHNIQUE_NONE;
            }
        }
    }
}

void RT_UpscaleCvarsToRtgl( RgStartFrameRenderResolutionParams* pDst )
{
    cvar::rt_available_dlss2 =
        rt.rgUtilIsUpscaleTechniqueAvailable( RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS,
                                              RG_FRAME_GENERATION_MODE_OFF,
                                              &cvar::rt_failreason_dlss2 );
    cvar::rt_available_dlss3fg =
        rt.rgUtilIsUpscaleTechniqueAvailable( RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS,
                                              RG_FRAME_GENERATION_MODE_ON,
                                              &cvar::rt_failreason_dlss3fg );
    cvar::rt_available_fsr2 =
        rt.rgUtilIsUpscaleTechniqueAvailable( RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2,
                                              RG_FRAME_GENERATION_MODE_OFF,
                                              &cvar::rt_failreason_fsr2 );
    cvar::rt_available_fsr3fg =
        rt.rgUtilIsUpscaleTechniqueAvailable( RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2,
                                              RG_FRAME_GENERATION_MODE_ON,
                                              &cvar::rt_failreason_fsr3fg );
    cvar::rt_available_dxgi = rt.rgUtilDXGIAvailable( &cvar::rt_failreason_dxgi );

    const RgFeatureFlags features = rt.rgUtilGetSupportedFeatures();

    cvar::rt_hdr_available   = ( features & RG_FEATURE_HDR );
    cvar::rt_fluid_available = ( features & RG_FEATURE_FLUID );

    int nvDlss = cvar::rt_available_dlss2 || cvar::rt_available_dlss3fg //
                     ? int( cvar::rt_upscale_dlss )
                     : 0;
    int amdFsr = cvar::rt_available_fsr2 || cvar::rt_available_fsr3fg //
                     ? int( cvar::rt_upscale_fsr2 )
                     : 0;

    switch( nvDlss )
    {
        case 1:
            // start with Quality
            pDst->upscaleTechnique = RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS;
            pDst->resolutionMode   = RG_RENDER_RESOLUTION_MODE_QUALITY;
            break;
        case 2:
            pDst->upscaleTechnique = RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS;
            pDst->resolutionMode   = RG_RENDER_RESOLUTION_MODE_BALANCED;
            break;
        case 3:
            pDst->upscaleTechnique = RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS;
            pDst->resolutionMode   = RG_RENDER_RESOLUTION_MODE_PERFORMANCE;
            break;
        case 4:
            pDst->upscaleTechnique = RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS;
            pDst->resolutionMode   = RG_RENDER_RESOLUTION_MODE_ULTRA_PERFORMANCE;
            break;

        case 5:
            // use DLSS with rt_renderscale
            pDst->upscaleTechnique = RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS;
            pDst->resolutionMode   = RG_RENDER_RESOLUTION_MODE_CUSTOM;
            break;

        case 6:
            pDst->upscaleTechnique = RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS;
            pDst->resolutionMode   = RG_RENDER_RESOLUTION_MODE_NATIVE_AA;
            break;

        default: nvDlss = 0; break;
    }

    switch( amdFsr )
    {
        case 1:
            pDst->upscaleTechnique = RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2;
            pDst->resolutionMode   = RG_RENDER_RESOLUTION_MODE_QUALITY;
            break;
        case 2:
            pDst->upscaleTechnique = RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2;
            pDst->resolutionMode   = RG_RENDER_RESOLUTION_MODE_BALANCED;
            break;
        case 3:
            pDst->upscaleTechnique = RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2;
            pDst->resolutionMode   = RG_RENDER_RESOLUTION_MODE_PERFORMANCE;
            break;
        case 4:
            pDst->upscaleTechnique = RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2;
            pDst->resolutionMode   = RG_RENDER_RESOLUTION_MODE_ULTRA_PERFORMANCE;
            break;

        case 5:
            // use FSR2 with rt_renderscale
            pDst->upscaleTechnique = RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2;
            pDst->resolutionMode   = RG_RENDER_RESOLUTION_MODE_CUSTOM;
            break;

        case 6:
            pDst->upscaleTechnique = RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2;
            pDst->resolutionMode   = RG_RENDER_RESOLUTION_MODE_NATIVE_AA;
            break;

        default: amdFsr = 0; break;
    }

    // both disabled
    if( nvDlss == 0 && amdFsr == 0 )
    {
        pDst->upscaleTechnique = RG_RENDER_UPSCALE_TECHNIQUE_NEAREST;
        pDst->resolutionMode   = RG_RENDER_RESOLUTION_MODE_CUSTOM;
        pDst->frameGeneration  = RG_FRAME_GENERATION_MODE_OFF;
    }
    else
    {
        if( ( nvDlss != 0 && cvar::rt_available_dlss3fg ) ||
            ( amdFsr != 0 && cvar::rt_available_fsr3fg ) )
        {
            switch( cvar::rt_framegen )
            {
                case -1: pDst->frameGeneration = RG_FRAME_GENERATION_MODE_WITHOUT_GENERATED; break;
                case 1: pDst->frameGeneration = RG_FRAME_GENERATION_MODE_ON; break;
                default: pDst->frameGeneration = RG_FRAME_GENERATION_MODE_OFF; break;
            }
        }
        else
        {
            pDst->frameGeneration = RG_FRAME_GENERATION_MODE_OFF;
        }
    }

    pDst->sharpenTechnique = RT_GetSharpenTechniqueFromCvar( amdFsr || nvDlss );
}

template< typename T >
    requires( std::is_same_v< T, int > )
uint32_t safe_uint( T x )
{
    return static_cast< uint32_t >( std::max< int >( x, 0 ) );
}

} // anonymous namespace

//
//
//

RTFrameBuffer::RTFrameBuffer( void* hMonitor, bool fullscreen )
    : SystemBaseFrameBuffer( hMonitor, fullscreen ), m_state{ new RTRenderState{ this } }
{
}
RTFrameBuffer::~RTFrameBuffer()
{
    delete m_state;
    delete mVertexData;
    delete mSkyData;
    delete mViewpoints;
    delete mLights;
    delete mBones;
}
void RTFrameBuffer::InitializeState()
{
    m_state      = new RTRenderState{ this };
    vendorstring = "RT";
    mVertexData  = new FFlatVertexBuffer( GetWidth(), GetHeight(), screen->mPipelineNbr );
    mSkyData     = new FSkyVertexBuffer;
    mViewpoints  = new HWViewpointBuffer( screen->mPipelineNbr );
    mLights      = new FLightBuffer( screen->mPipelineNbr );
    mBones       = new BoneBuffer( screen->mPipelineNbr );
}

void RTFrameBuffer::FirstEye()
{
    m_state->RT_AddMainCamera( r_viewpoint );
    Super::FirstEye();
}

FRenderState* RTFrameBuffer::RenderState()
{
    return m_state;
}
IVertexBuffer* RTFrameBuffer::CreateVertexBuffer()
{
    return new RTVertexBuffer{};
}
IIndexBuffer* RTFrameBuffer::CreateIndexBuffer()
{
    return new RTIndexBuffer{};
}
IDataBuffer* RTFrameBuffer::CreateDataBuffer( int bindingpoint, bool ssbo, bool needsresize )
{
    return new RTDataBuffer{};
}
IHardwareTexture* RTFrameBuffer::CreateHardwareTexture( int numchannels )
{
    return new RTHardwareTexture{};
}
void RTFrameBuffer::Draw2D()
{
    ::Draw2D( twod, *m_state );
}

//
//
//

namespace
{
constexpr auto remap01( float v, float newmin, float newmax )
{
    assert( newmax > newmin );
    return newmin + std::clamp( v, 0.f, 1.f ) * ( newmax - newmin );
}

auto RT_GetPlayer() -> player_t*
{
    return players[ consoleplayer ].camera ? players[ consoleplayer ].camera->player : nullptr;
}

auto RT_DamageIntensity() -> std::optional< float >
{
    // for reference https://doom.fandom.com/wiki/Comparison_of_Doom_monsters
    constexpr float maxdmg = 100.f;

    if( auto player = RT_GetPlayer() )
    {
        if( player->damagecount > 0 )
        {
            float dmg01 =
                std::clamp( static_cast< float >( player->damagecount ) / maxdmg, 0.f, 1.f );

            // smaller damage should also have effect
            dmg01 = sqrt( dmg01 );

            assert( dmg01 > 0.005f );
            return dmg01;
        }
    }
    return {};
}

uint32_t RT_CalcPowerupFlags()
{
    auto player = RT_GetPlayer();
    if( !player )
    {
        return 0;
    }

    uint32_t powerups = 0;

    for( AActor* in = player->mo->Inventory; in; in = in->Inventory )
    {
        if( in->IsKindOf( NAME_PowerStrength ) )
        {
            if( rtstate.m_berserkBlend > 10 )
            {
                powerups |= RT_POWERUP_FLAG_BERSERK_BIT;
            }
        }
        else if( in->IsKindOf( NAME_PowerIronFeet ) )
        {
            powerups |= RT_POWERUP_FLAG_RADIATIONSUIT_BIT;
        }
        else if( in->IsKindOf( NAME_PowerInvulnerable ) )
        {
            powerups |= RT_POWERUP_FLAG_INVUNERABILITY_BIT;
        }
        else if( in->IsKindOf( NAME_PowerLightAmp ) )
        {
            switch( *cvar::rt_pw_lightamp )
            {
                case 1: powerups |= RT_POWERUP_FLAG_THERMALVISION_BIT; break;
                case 2: powerups |= RT_POWERUP_FLAG_FLASHLIGHT_BIT; break;
                default: powerups |= RT_POWERUP_FLAG_NIGHTVISION_BIT; break;
            }
        }
        else if( in->IsKindOf( NAME_PowerInvisibility ) )
        {
            powerups |= RT_POWERUP_FLAG_INVISIBILITY_BIT;
        }

        // NAME_PowerTargeter
        // NAME_PowerWeaponLevel2
        // NAME_PowerFlight
        // NAME_PowerSpeed
        // NAME_PowerTorch
        // NAME_PowerHighJump
        // NAME_PowerReflection
        // NAME_PowerDrain
        // NAME_PowerScanner
        // NAME_PowerDoubleFiringSpeed
        // NAME_PowerInfiniteAmmo
        // NAME_PowerBuddha
    }

    if( player->bonuscount > 0 )
    {
        powerups |= RT_POWERUP_FLAG_BONUS_BIT;
    }

    return powerups;
}
} // anonymous namespace

//
//
//

static bool   g_resetposteffects = false;
static bool   g_resetfluid       = false;
static bool   g_melt_requested   = false;
static double g_melt_endtime     = -1;
bool          g_noinput_onstart  = true;

bool   g_cpu_latency_get = false;
double g_cpu_latency     = 0;

static void RT_DrawTitle();
static void RT_ClearTitles();
static void RT_InjectTitleIntoDoomMap( const char* mapname );

void RT_OnLevelLoad( const char* mapname)
{
    g_resetposteffects = true;
    g_resetfluid       = true;
    RT_ClearTitles();
    RT_InjectTitleIntoDoomMap( mapname );
    RT_ForceIntroCutsceneMusicStop();
}

void RT_RequestMelt()
{
    // HACKHACK: suppress melting when getting into the first start
    {
        static bool first = true;
        if( first )
        {
            first = false; 
            return;
        }
    }
    g_melt_requested = true;
}

bool RT_IsMeltActive()
{
    return g_melt_endtime > 0 && RT_GetCurrentTime() < g_melt_endtime;
}
bool RT_IgnoreUserInput()
{
    return RT_IsMeltActive() || g_noinput_onstart;
}

static double CalcCpuLatency()
{
    static double   g_lprevtime            = RT_GetCurrentTime();
    static double   g_lprevlatencies[ 30 ] = {};
    static uint32_t g_lprevi               = 0;

    double lcurtime = RT_GetCurrentTime();

    g_lprevlatencies[ g_lprevi ] = lcurtime - g_lprevtime;

    g_lprevi    = ( g_lprevi + 1 ) % std::size( g_lprevlatencies );
    g_lprevtime = lcurtime;

    double sum = 0;
    int    cnt = 0;
    for( double t : g_lprevlatencies )
    {
        if( t > 0 )
        {
            sum += t;
            cnt++;
        }
    }

    return cnt > 0 ? sum / cnt : 0;
}

namespace
{
template< typename T >
T smoothstep( T edge0, T edge1, T x )
{
    T t = std::clamp( ( x - edge0 ) / ( edge1 - edge0 ), T( 0 ), T( 1 ) );
    return t * t * ( T( 3 ) - T( 2 ) * t );
}

namespace classic_toggle
{
    constexpr double Duration  = 0.75;
    double           g_timeend = 0.0;

    float                  g_source = 0.0f;
    std::optional< float > g_target = {};

    CCMD( rt_classic_toggle )
    {
        g_timeend = RT_GetCurrentTime() + Duration;
        g_source  = std::clamp< float >( cvar::rt_classic, 0, 1 );

        if( g_target )
        {
            g_target = g_target.value() > 0 ? 0.f : 1.f;
        }
        else
        {
            g_target = cvar::rt_classic > 0 ? 0.f : 1.f;
        }
    }

    void Animate()
    {
        if( g_target )
        {
            double dt = g_timeend - RT_GetCurrentTime();
            if( dt <= 0 )
            {
                cvar::rt_classic = *g_target;
                g_target         = {};
                return;
            }

            double ratio = 1 - std::clamp( dt / Duration, 0.0, 1.0 );
            ratio        = smoothstep( 0.0, 1.0, ratio );

            cvar::rt_classic = std::lerp( g_source, *g_target, static_cast< float >( ratio ) );
        }
    }
} // namespace classic_toggle

auto g_sectorlightlevels = std::vector< uint8_t >{};

void RT_MakeLightstyles()
{
    if( !primaryLevel || primaryLevel->sectors.Size() == 0 )
    {
        g_sectorlightlevels.clear();
        return;
    }
    g_sectorlightlevels.resize( primaryLevel->sectors.Size() );

    for( uint32_t i = 0; i < primaryLevel->sectors.Size(); i++ )
    {
        g_sectorlightlevels[ i ] = uint8_t( std::clamp( //
            primaryLevel->sectors[ i ].GetLightLevel(),
            0,
            255 ) );
    }
}

void RT_UploadExportableSectorLights()
{
    assert( g_sectorlightlevels.size() == primaryLevel->sectors.Size() );

    for( uint32_t i = 0; i < primaryLevel->sectors.Size(); i++ )
    {
        const sector_t& sector = primaryLevel->sectors[ i ];

        float z;
        {
            auto zfloor   = float( sector.floorplane.ZatPoint( sector.centerspot ) );
            auto zceiling = float( sector.ceilingplane.ZatPoint( sector.centerspot ) );

            // if too thin
            if( std::abs( zfloor - zceiling ) < 0.1f )
            {
                bool important = ( sector.special == Light_Phased ) ||
                                 ( sector.special == LightSequenceStart ) ||
                                 ( sector.special == LightSequenceSpecial1 ) ||
                                 ( sector.special == LightSequenceSpecial2 ) ||
                                 ( sector.special == dLight_Flicker ) ||
                                 ( sector.special == dLight_StrobeFast ) ||
                                 ( sector.special == dLight_StrobeSlow ) ||
                                 ( sector.special == dLight_Strobe_Hurt ) ||
                                 ( sector.special == dLight_Glow ) ||
                                 ( sector.special == dLight_StrobeSlowSync ) ||
                                 ( sector.special == dLight_StrobeFastSync ) ||
                                 ( sector.special == dLight_FireFlicker ) ||
                                 ( sector.special == sLight_Strobe_Hurt ) ||
                                 ( sector.special == Light_OutdoorLightning ) ||
                                 ( sector.special == Light_IndoorLightning1 ) ||
                                 ( sector.special == Light_IndoorLightning2 );
                if( !important )
                {
                    continue;
                }
            }

            z = ( zfloor + zceiling ) / 2;
        }

        const auto center = FVector3{
            float( sector.centerspot.X ),
            float( sector.centerspot.Y ),
            z,
        };

        auto adt = RgLightAdditionalEXT{
            .sType      = RG_STRUCTURE_TYPE_LIGHT_ADDITIONAL_EXT,
            .pNext      = nullptr,
            .flags      = RG_LIGHT_ADDITIONAL_LIGHTSTYLE,
            .lightstyle = int( i ), // references g_sectorlightlevels
            .hashName   = "",
        };

        auto lsph = RgLightSphericalEXT{
            .sType     = RG_STRUCTURE_TYPE_LIGHT_SPHERICAL_EXT,
            .pNext     = &adt,
            .color     = RG_PACKED_COLOR_WHITE,
            .intensity = cvar::rt_autoexport_light,
            .position  = { center.X * ONEGAMEUNIT_IN_METERS,
                           center.Y * ONEGAMEUNIT_IN_METERS,
                           center.Z * ONEGAMEUNIT_IN_METERS },
            .radius    = 0.05f,
        };

        auto linfo = RgLightInfo{
            .sType        = RG_STRUCTURE_TYPE_LIGHT_INFO,
            .pNext        = &lsph,
            .uniqueID     = SectorLightId_Base + i,
            .isExportable = true, // so we can write in the gltf
        };

        RgResult r = rt.rgUploadLight( &linfo );
        RG_CHECK( r );
    }
}

}

//
//
//

void RTFrameBuffer::RT_BeginFrame()
{
    // HACKHACK begin
    if( g_rt_skipinitframes == -10 )
    {
        g_rt_skipinitframes = cvar::hack_initialframesskip ? 0 : 2;
    }
    if( g_rt_skipinitframes >= 0 )
    {
        if( g_rt_skipinitframes == 0 )
        {
            RT_CloseLauncherWindow(); // renderer is ready, close launcher window
            PositionWindow( IsFullscreen() );
            g_rt_forcenofocuschange = false;
        }
        --g_rt_skipinitframes;
    }
    // HACKHACK end


    m_state->RT_BeginFrame();

    classic_toggle::Animate();
    
    auto resolution_params = RgStartFrameRenderResolutionParams{
        .sType             = RG_STRUCTURE_TYPE_START_FRAME_RENDER_RESOLUTION_PARAMS,
        .pNext             = nullptr,
        .preferDxgiPresent = cvar::rt_available_dxgi ? cvar::rt_dxgi : false,
    };
    RT_ResolutionToRtgl( &resolution_params, RT_GetCurrentWindowSize() );
    RT_UpscaleCvarsToRtgl( &resolution_params );

    RT_MakeLightstyles();

    auto fluid_params = RgStartFrameFluidParams{
        .sType          = RG_STRUCTURE_TYPE_START_FRAME_FLUID_PARAMS,
        .pNext          = &resolution_params,
        .enabled        = cvar::rt_fluid_available ? cvar::rt_fluid : false,
        .reset          = g_resetfluid,
        .gravity        = { cvar::rt_fluid_gravity_x, //
                            cvar::rt_fluid_gravity_y,
                            cvar::rt_fluid_gravity_z },
        .color          = { cvar::rt_blood_color_r, //
                            cvar::rt_blood_color_g,
                            cvar::rt_blood_color_b },
        .particleBudget = uint32_t( std::max( 0, int( cvar::rt_fluid_budget ) ) ),
        .particleRadius = cvar::rt_fluid_pradius,
    };

    RgStaticSceneStatusFlags staticscene_status = 0;

    auto info = RgStartFrameInfo{
        .sType                  = RG_STRUCTURE_TYPE_START_FRAME_INFO,
        .pNext                  = &fluid_params,
        .pMapName               = RT_GetMapName(),
        .ignoreExternalGeometry = false,
        .vsync                  = cvar::rt_vsync,
        .hdr                    = cvar::rt_hdr_available ? cvar::rt_hdr : false,
        .allowMapAutoExport     = cvar::rt_autoexport,
        .lightmapScreenCoverage = RT_ForceNoClassicMode() ? 0.0f : cvar::rt_classic,
        .lightstyleValuesCount  = uint32_t( g_sectorlightlevels.size() ),
        .pLightstyleValues8     = g_sectorlightlevels.data(),
        .pResultStaticSceneStatus = &staticscene_status,
        .staticSceneAnimationTime = g_rt_cutscenename ? RT_CutsceneTime() : 0,
    };
    g_resetfluid = false;

    RgResult r = rt.rgStartFrame( &info );
    RG_CHECK( r );


    auto l_clm = [ staticscene_status ]() {
        if( staticscene_status & RG_STATIC_SCENE_STATUS_EXPORT_STARTED )
        {
            return 2; // no cull as we need to upload all geometry for the first time
        }
        if( staticscene_status & RG_STATIC_SCENE_STATUS_NEW_SCENE_STARTED )
        {
            return 2; // touch everything, to upload all resources
        }
        if( !( staticscene_status & RG_STATIC_SCENE_STATUS_LOADED ) )
        {
            return 2; // no static scene, upload everything
        }
        switch( int( cvar::rt_cpu_cullmode ) )
        {
            case 1: return 1;
            case 2: return 2;
            default: return 0;
        }
    };

    rt_cullmode = l_clm();
}

void RTFrameBuffer::RT_DrawFrame()
{
    const double   curtime      = RT_GetCurrentTime();
    const uint32_t powerupflags = RT_CalcPowerupFlags();

    RT_DrawTitle();

    if( bool{ cvar::rt_sun } && float{ cvar::rt_sun_intensity } > 0 )
    {
        float altitude = to_rad( float{ cvar::rt_sun_a } );
        float azimuth  = to_rad( float{ cvar::rt_sun_b } );

        float theta = std::clamp( pi() / 2 - altitude, 0.f, pi() );
        float phi   = std::fmod( azimuth, pi() * 2 );

        // negate, direction from the sun, not towards the sun
        auto dir = RgFloat3D{
            -sin( theta ) * cos( phi ),
            -sin( theta ) * sin( phi ),
            -cos( theta ),
        };

        auto s = RgLightDirectionalEXT{
            .sType                  = RG_STRUCTURE_TYPE_LIGHT_DIRECTIONAL_EXT,
            .pNext                  = nullptr,
            .color                  = cvarcolor_to_rtcolor( cvar::rt_sun_color ),
            .intensity              = float{ cvar::rt_sun_intensity },
            .direction              = dir,
            .angularDiameterDegrees = 0.5f,
        };

        auto i = RgLightInfo{
            .sType        = RG_STRUCTURE_TYPE_LIGHT_INFO,
            .pNext        = &s,
            .uniqueID     = SunLightId,
            .isExportable = false,
        };

        RgResult r = rt.rgUploadLight( &i );
        RG_CHECK( r );
    }

    RT_UploadExportableSectorLights();

    auto tm_params = RgDrawFrameTonemappingParams{
        .sType                = RG_STRUCTURE_TYPE_DRAW_FRAME_TONEMAPPING_PARAMS,
        .pNext                = nullptr,
        .disableEyeAdaptation = false,
        .ev100Min             = cvar::rt_tnmp_ev100_min,
        .ev100Max             = cvar::rt_tnmp_ev100_max,
        .luminanceWhitePoint  = cvar::rt_classic_white,
        .saturation           = { cvar::rt_tnmp_saturation_r,
                                  cvar::rt_tnmp_saturation_g,
                                  cvar::rt_tnmp_saturation_b },
        .crosstalk            = { cvar::rt_tnmp_crosstalk_r,
                                  cvar::rt_tnmp_crosstalk_g,
                                  cvar::rt_tnmp_crosstalk_b },
        .contrast             = cvar::rt_tnmp_contrast,
        .hdrBrightness        = cvar::rt_hdr_brightness,
        .hdrContrast          = cvar::rt_hdr_contrast,
        .hdrSaturation        = { cvar::rt_hdr_saturation,
                                  cvar::rt_hdr_saturation,
                                  cvar::rt_hdr_saturation },
    };

    auto reflrefr_params = RgDrawFrameReflectRefractParams{
        .sType                   = RG_STRUCTURE_TYPE_DRAW_FRAME_REFLECT_REFRACT_PARAMS,
        .pNext                   = &tm_params,
        .maxReflectRefractDepth  = safe_uint( *cvar::rt_reflrefr_depth ),
        .typeOfMediaAroundCamera = RG_MEDIA_TYPE_VACUUM,
        .indexOfRefractionGlass  = cvar::rt_refr_glass,
        .indexOfRefractionWater  = cvar::rt_refr_water,
        .waterWaveSpeed          = 0.05f,                    // for partial_invisibility
        .waterWaveNormalStrength = cvar::rt_water_wavestren, // for partial_invisibility
        .waterColor              = { std::clamp( *cvar::rt_water_r / 255.f, 0.f, 1.f ),
                                     std::clamp( *cvar::rt_water_g / 255.f, 0.f, 1.f ),
                                     std::clamp( *cvar::rt_water_b / 255.f, 0.f, 1.f ) },
        .waterWaveTextureDerivativesMultiplier = 1.0f,
        .waterTextureAreaScale                 = 1.0f,
        .portalNormalTwirl                     = false,
    };

    auto sky_params = RgDrawFrameSkyParams{
        .sType              = RG_STRUCTURE_TYPE_DRAW_FRAME_SKY_PARAMS,
        .pNext              = &reflrefr_params,
        .skyType            = m_wassky ? RG_SKY_TYPE_RASTERIZED_GEOMETRY : RG_SKY_TYPE_COLOR,
        .skyColorDefault    = { 0, 0, 0 },
        .skyColorMultiplier = cvar::rt_sky,
        .skyColorSaturation = cvar::rt_sky_saturation,
        .skyViewerPosition  = { 0, 0, 0 },
    };

    auto volumetrics_params = RgDrawFrameVolumetricParams{
        .sType                   = RG_STRUCTURE_TYPE_DRAW_FRAME_VOLUMETRIC_PARAMS,
        .pNext                   = &sky_params,
        .enable                  = cvar::rt_volume_type != 0,
        .maxHistoryLength        = cvar::rt_volume_type == 1 ? cvar::rt_volume_history : 0.f,
        .useSimpleDepthBased     = cvar::rt_volume_type == 2,
        .volumetricFar           = cvar::rt_volume_far,
        .ambientColor            = { cvar::rt_volume_ambient,
                                     cvar::rt_volume_ambient,
                                     cvar::rt_volume_ambient },
        .scaterring              = cvar::rt_volume_scatter,
        .assymetry               = cvar::rt_volume_lassymetry,
        .useIlluminationVolume   = false,
        .fallbackSourceColor     = { 0, 0, 0 },
        .fallbackSourceDirection = { 0, -1, 0 },
        .lightMultiplier         = cvar::rt_volume_lintensity,
        .allowTintUnderwater     = false,
        .underwaterColor         = {},
    };

    auto texture_params = RgDrawFrameTexturesParams{
        .sType = RG_STRUCTURE_TYPE_DRAW_FRAME_TEXTURES_PARAMS,
        .pNext = &volumetrics_params,
        .dynamicSamplerFilter =
            cvar::rt_smoothtextures ? RG_SAMPLER_FILTER_LINEAR : RG_SAMPLER_FILTER_NEAREST,
        .normalMapStrength      = cvar::rt_normalmap_stren,
        .emissionMapBoost       = cvar::rt_emis_mapboost,
        .emissionMaxScreenColor = cvar::rt_emis_maxscrcolor,
        .minRoughness           = cvar::rt_refl_thresh,
        .heightMapDepth         = 0.02f * cvar::rt_heightmap_stren,
    };

    float dirtscale = ( ( powerupflags & RT_POWERUP_FLAG_RADIATIONSUIT_BIT ) ||
                        ( powerupflags & RT_POWERUP_FLAG_NIGHTVISION_BIT ) )
                          ? 15.f
                          : cvar::rt_bloom_dirt_scale;

    auto bloom_params = RgDrawFrameBloomParams{
        .sType             = RG_STRUCTURE_TYPE_DRAW_FRAME_BLOOM_PARAMS,
        .pNext             = &texture_params,
        .inputEV           = cvar::rt_bloom_ev,
        .inputThreshold    = cvar::rt_bloom_threshold,
        .bloomIntensity    = cvar::rt_bloom ? cvar ::rt_bloom_scale : 0.f,
        .lensDirtIntensity = cvar::rt_bloom_dirt ? dirtscale : 0.f,
    };

    auto illum_params = RgDrawFrameIlluminationParams{
        .sType                              = RG_STRUCTURE_TYPE_DRAW_FRAME_ILLUMINATION_PARAMS,
        .pNext                              = &bloom_params,
        .maxBounceShadows                   = safe_uint( *cvar::rt_shadowrays ),
        .enableSecondBounceForIndirect      = true,
        .cellWorldSize                      = 2.0f,
        .directDiffuseSensitivityToChange   = 1.0f,
        .indirectDiffuseSensitivityToChange = 0.75f,
        .specularSensitivityToChange        = 1.0f,
        .polygonalLightSpotlightFactor      = 2.0f,
        .lightUniqueIdIgnoreFirstPersonViewerShadows = &FlashlightLightId,
    };

    auto ef_wipe = RgPostEffectWipe{
        .stripWidth = 1.0f / 320.0f,
        .beginNow   = cvar::rt_melt_duration > 0.05f ? g_melt_requested : false,
        .duration   = cvar::rt_melt_duration > 0.05f ? cvar::rt_melt_duration : 0.0f,
    };
    g_melt_requested = false;

    if( ef_wipe.beginNow )
    {
        g_melt_endtime = curtime + static_cast< double >( ef_wipe.duration );
    }
    if( g_melt_endtime > 0 && curtime > g_melt_endtime )
    {
        g_melt_endtime = -1;
    }

    auto ef_radialblur = RgPostEffectRadialBlur{
        .isActive              = powerupflags & RT_POWERUP_FLAG_BERSERK_BIT,
        .transitionDurationIn  = 0.4f,
        .transitionDurationOut = 3.0f,
    };

    bool chrabr_from_powerup = ( powerupflags & RT_POWERUP_FLAG_NIGHTVISION_BIT ) ||
                               ( powerupflags & RT_POWERUP_FLAG_THERMALVISION_BIT ) ||
                               ( powerupflags & RT_POWERUP_FLAG_BERSERK_BIT );

    auto ef_chrabr = RgPostEffectChromaticAberration{
        .isActive              = chrabr_from_powerup || cvar::rt_ef_chraber > 0.f,
        .transitionDurationIn  = 0,
        .transitionDurationOut = 0,
        .intensity             = chrabr_from_powerup ? 1.2f : cvar::rt_ef_chraber,
    };
    // smooth out manually (because it's a constant active effect, i.e. without switching isActive)
    {
        constexpr auto Duration     = 0.5f;
        static double  begin_time   = curtime;
        static float   last_value   = ef_chrabr.intensity;
        static float   begin_value  = ef_chrabr.intensity;
        static float   target_value = ef_chrabr.intensity;

        if( std::abs( target_value - ef_chrabr.intensity ) > 0.001f )
        {
            begin_time   = curtime;
            begin_value  = last_value;
            target_value = ef_chrabr.intensity;
        }

        // if( begin_time <= curtime && curtime <= begin_time + double( Duration ) )
        {
            const float t = std::clamp( float( curtime - begin_time ) / Duration, 0.0f, 1.0f );
            ef_chrabr.intensity = std::lerp( begin_value, target_value, t );
        }
        last_value = ef_chrabr.intensity;
    }

    auto ef_invbw = RgPostEffectInverseBlackAndWhite{
        .isActive              = powerupflags & RT_POWERUP_FLAG_INVUNERABILITY_BIT,
        .transitionDurationIn  = 1.0f,
        .transitionDurationOut = 1.5f,
    };

    auto ef_hueshift = RgPostEffectHueShift{
        .isActive              = powerupflags & RT_POWERUP_FLAG_THERMALVISION_BIT,
        .transitionDurationIn  = 0.5f,
        .transitionDurationOut = 0.5f,
    };

    auto ef_nightvision = RgPostEffectNightVision{
        .isActive              = powerupflags & RT_POWERUP_FLAG_NIGHTVISION_BIT,
        .transitionDurationIn  = 0.5f,
        .transitionDurationOut = 0.5f,
    };

    auto ef_distortedsides = RgPostEffectDistortedSides{
        .isActive              = powerupflags & RT_POWERUP_FLAG_RADIATIONSUIT_BIT,
        .transitionDurationIn  = 1.0f,
        .transitionDurationOut = 1.0f,
    };

    // static, so prev state's transition durations
    // are preserved across frames, when flags are removed
    static auto ef_tint = RgPostEffectColorTint{};
    {
        ef_tint.isActive = false;

        if( auto dmg = RT_DamageIntensity() )
        {
            ef_tint = RgPostEffectColorTint{
                .isActive              = true,
                .transitionDurationIn  = 0.0f,
                .transitionDurationOut = remap01( *dmg, 0.5f, 1.7f ),
                .intensity             = remap01( *dmg, 1.5f, 3.0f ) * blood_fade_scalar,
                .color                 = { 1.f, 0.f, 0.f },
            };
        }
        else if( powerupflags & RT_POWERUP_FLAG_RADIATIONSUIT_BIT )
        {
            ef_tint = RgPostEffectColorTint{
                .isActive              = true,
                .transitionDurationIn  = 1.0f,
                .transitionDurationOut = 1.0f,
                .intensity             = 1.0f,
                .color                 = { 0.2f, 1.f, 0.4f },
            };
        }
        else if( powerupflags & RT_POWERUP_FLAG_BONUS_BIT )
        {
            ef_tint = RgPostEffectColorTint{
                .isActive              = true,
                .transitionDurationIn  = 0.0f,
                .transitionDurationOut = 0.7f,
                .intensity             = 0.5f * pickup_fade_scalar,
                .color                 = { 1.f, 0.91f, 0.42f },
            };
        }
    }

    const int vintage_crt = int{ cvar::rt_ef_vintage } == RT_VINTAGE_CRT ||
                            int{ cvar::rt_ef_vintage } == RT_VINTAGE_VHS_CRT;
    const int vintage_vhs = int{ cvar::rt_ef_vintage } == RT_VINTAGE_VHS ||
                            int{ cvar::rt_ef_vintage } == RT_VINTAGE_VHS_CRT;
    const int vintage_dither = int{ cvar::rt_ef_vintage } == RT_VINTAGE_200_DITHER ||
                               int{ cvar::rt_ef_vintage } == RT_VINTAGE_480_DITHER;

    auto ef_crt = RgPostEffectCRT{
        .isActive = vintage_crt || cvar::rt_ef_crt,
    };

    auto ef_vhs = RgPostEffectVHS{
        .isActive              = vintage_vhs || cvar::rt_ef_vhs > 0.f,
        .transitionDurationIn  = 0,
        .transitionDurationOut = 0,
        .intensity             = vintage_vhs ? 0.9f : float{ cvar::rt_ef_vhs },
    };

    auto ef_dither = RgPostEffectDither{
        .isActive              = vintage_dither || cvar::rt_ef_dither > 0.f,
        .transitionDurationIn  = 0,
        .transitionDurationOut = 0,
        .intensity             = vintage_dither ? 0.8f : float{ cvar::rt_ef_dither },
    };

    // some of the power-up effects need to be reset
    auto post_params = RgDrawFramePostEffectsParams{
        .sType                 = RG_STRUCTURE_TYPE_DRAW_FRAME_POST_EFFECTS_PARAMS,
        .pNext                 = &illum_params,
        .pWipe                 = &ef_wipe,
        .pRadialBlur           = g_resetposteffects ? nullptr : &ef_radialblur,
        .pChromaticAberration  = &ef_chrabr,
        .pInverseBlackAndWhite = g_resetposteffects ? nullptr : &ef_invbw,
        .pHueShift             = g_resetposteffects ? nullptr : &ef_hueshift,
        .pNightVision          = g_resetposteffects ? nullptr : &ef_nightvision,
        .pDistortedSides       = g_resetposteffects ? nullptr : &ef_distortedsides,
        .pColorTint            = g_resetposteffects ? nullptr : &ef_tint,
        .pCRT                  = &ef_crt,
        .pVHS                  = &ef_vhs,
        .pDither               = &ef_dither,
    };

    auto info = RgDrawFrameInfo{
        .sType            = RG_STRUCTURE_TYPE_DRAW_FRAME_INFO,
        .pNext            = &post_params,
        .rayLength        = GetZFar() * ONEGAMEUNIT_IN_METERS,
        .presentPrevFrame = false,
        .currentTime      = curtime,
    };

    RgResult r = rt.rgDrawFrame( &info );
    RG_CHECK( r );

    if( g_cpu_latency_get )
    {
        g_cpu_latency = CalcCpuLatency();
    }

    // reset for next frame
    {
        m_wassky           = false;
        g_resetposteffects = false;
    }
}

//
//
//

bool RTRenderState::IsPerspectiveMatrix( const float* m )
{
    return std::abs( m[ 15 ] ) < std::numeric_limits< float >::epsilon();
}

bool RTRenderState::IsLikeIdentity( const float* m )
{
    auto areSimilar = []( float a, float b ) {
        return std::abs( a - b ) < 0.0000001f;
    };
    for( int a = 0; a < 4; a++ )
    {
        for( int b = 0; b < 4; b++ )
        {
            if( !areSimilar( m[ a * 4 + b ], ( a == b ? 1.0f : 0.0f ) ) )
            {
                return false;
            }
        }
    }
    return true;
}
bool RTRenderState::IsLikeIdentity( const double* m )
{
    auto areSimilar = []( double a, double b ) {
        return std::abs( a - b ) < 0.0000001;
    };
    for( int a = 0; a < 4; a++ )
    {
        for( int b = 0; b < 4; b++ )
        {
            if( !areSimilar( m[ a * 4 + b ], ( a == b ? 1.0 : 0.0 ) ) )
            {
                return false;
            }
        }
    }
    return true;
}

auto RT_MakeUpRightForwardVectors( const DRotator& rotation ) -> std::tuple< RgFloat3D, RgFloat3D, RgFloat3D >
{
    // based on HWDrawInfo::SetViewMatrix
    RgFloat3D up, right, forward;

    auto pitch = rotation.Pitch;
    // RT: invert yaw
    auto yaw  = FAngle::fromDeg( -( 270.0 - rotation.Yaw.Degrees() ) );
    auto roll = rotation.Roll;

    auto view = VSMatrix{ 1 };
    view.rotate( float( yaw.Degrees() ), 0, 0, 1 );   // around up
    view.rotate( float( pitch.Degrees() ), 1, 0, 0 ); // around right
    view.rotate( float( roll.Degrees() ), 0, 1, 0 );  // around forward
    const float* v = view.get();

    auto v100 = RgFloat3D{ -v[ 0 ], -v[ 1 ], -v[ 2 ] };
    auto v010 = RgFloat3D{ -v[ 4 ], -v[ 5 ], -v[ 6 ] };
    auto v001 = RgFloat3D{ v[ 8 ], v[ 9 ], v[ 10 ] };

    up      = v001;
    right   = v100;
    forward = v010;

    return { up, right, forward };
}

void RT_ForceCamera( const FVector3 position, const DRotator& rotation, float fovYDegrees )
{
    if( !rt.rgUploadCamera )
    {
        return;
    }

    const auto [ up, right, forward ] = RT_MakeUpRightForwardVectors( rotation );

    const float aspect = screen && screen->GetWidth() > 0 && screen->GetHeight() > 0
                             ? float( screen->GetWidth() ) / float( screen->GetHeight() )
                             : ( 16.f / 9.f );

    auto info = RgCameraInfo{
        .sType       = RG_STRUCTURE_TYPE_CAMERA_INFO,
        .pNext       = nullptr,
        .flags       = 0,
        .position    = { position[ 0 ], position[ 1 ], position[ 2 ] },
        .up          = up,
        .right       = right,
        .fovYRadians = fovYDegrees * pi::pif() / 180.0f,
        .aspect      = aspect,
        .cameraNear  = cvar::rt_znear,
        .cameraFar   = cvar::rt_zfar,
    };

    RgResult r = rt.rgUploadCamera( &info );
    assert( r == RG_RESULT_SUCCESS );
}

// A hack to access special+tag by a linenum
extern std::vector< std::pair< int, int > > rt_linesToSpecialAndTag;

extern auto RT_GetStairsSectors( int tag, line_t* line ) -> std::vector< int >;

namespace
{

std::unordered_set< int > g_tagsSafeToIgnore{};
std::unordered_set< int > g_stairsSectors{};

void RT_CacheTagsAndSpecials()
{
    if( !primaryLevel )
    {
        g_tagsSafeToIgnore.clear();
        g_stairsSectors.clear();
    }

    assert( rt_linesToSpecialAndTag.size() == primaryLevel->lines.size() );

    // 1 tag can be referenced by N specials
    // this is the mapping from tag to its list of specials
    std::unordered_map< int, std::unordered_set< int > > tagToSpecial{};
    for( const auto& [ special, tag ] : rt_linesToSpecialAndTag )
    {
        // tag < 0 -- ignored
        // tag = 0 -- has different behavior
        if( tag > 0 )
        {
            tagToSpecial[ tag ].emplace( special );
        }
    }

    // specials that do not move the geometry, so we can export it
    auto l_isSafeToIgnoreSpecial = []( int spec ) {
        switch( spec )
        {
            case Teleport:
            case Teleport_NoStop:
            case Teleport_NoFog:
            case Light_RaiseByValue:
            case Light_LowerByValue:
            case Light_ChangeToValue:
            case Light_Stop:
            case Light_MinNeighbor:
            case Light_MaxNeighbor:
            case Light_StrobeDoom: return true;
            default: return false;
        }
    };

    // make a list 
    std::unordered_set< int > tagsSafeToIgnore{};
    for( const auto& [ tag, specials ] : tagToSpecial )
    {
        // if no specials on a tag, it's safe
        if( specials.empty() )
        {
            assert( !tagsSafeToIgnore.contains( tag ) );
            tagsSafeToIgnore.emplace( tag );
            continue;
        }

        // if only one special on this tag
        if( specials.size() == 1 )
        {
            // and it's a safe special
            int spec = *specials.begin();
            if( l_isSafeToIgnoreSpecial( spec ) )
            {
                assert( !tagsSafeToIgnore.contains( tag ) );
                tagsSafeToIgnore.emplace( tag );
                continue;
            }
        }

        // surely, we can expand to specials.size() >= 2 (e.g. 1 tag is used for Teleport and Light_Stop => we can ignore),
        // but let's play safely for now..
    }

    g_tagsSafeToIgnore = std::move( tagsSafeToIgnore );


    assert( g_stairsSectors.empty() );
    for( uint32_t i = 0; i < rt_linesToSpecialAndTag.size(); i++ )
    {
        const auto& [ special, tag ] = rt_linesToSpecialAndTag[ i ];

        const auto sectornums = RT_GetStairsSectors( tag, &primaryLevel->lines[ i ] );
        g_stairsSectors.insert( sectornums.begin(), sectornums.end() );
    }
}


// NOTE: only linedef->special, and not sector->special, as it has only light change effects,
// sector that move has tag or one of its lines marked as lift/door/etc (linedef->special)


// If some line specials have tag==0,
// then line's backsector is a target of the special's action
bool IsTaggedByTag0( const line_t* linedef, const sector_t* target )
{
    if( !linedef || !primaryLevel )
    {
        return false;
    }

    // only backsectors
    if( linedef->backsector != target )
    {
        return false;
    }

    // tag == 0
    if( !primaryLevel->tagManager.RT_LineHasZeroTag( linedef ) )
    {
        return false;
    }

    switch( linedef->special )
    {
        // case ACS_Execute:
        // case ACS_ExecuteAlways:
        // case ACS_ExecuteWithResult:
        // case ACS_LockedExecute:
        // case ACS_LockedExecuteDoor:
        // case ACS_Suspend:
        // case ACS_Terminate:
        // case Autosave:
        case Ceiling_CrushAndRaise:
        case Ceiling_CrushAndRaiseA:
        case Ceiling_CrushAndRaiseDist:
        case Ceiling_CrushAndRaiseSilentA:
        case Ceiling_CrushAndRaiseSilentDist:
        case Ceiling_CrushRaiseAndStay:
        case Ceiling_CrushRaiseAndStayA:
        case Ceiling_CrushRaiseAndStaySilA:
        case Ceiling_CrushStop:
        case Ceiling_LowerAndCrush:
        case Ceiling_LowerAndCrushDist:
        case Ceiling_LowerByTexture:
        case Ceiling_LowerByValue:
        case Ceiling_LowerByValueTimes8:
        case Ceiling_LowerInstant:
        case Ceiling_LowerToFloor:
        case Ceiling_LowerToHighestFloor:
        case Ceiling_LowerToLowest:
        case Ceiling_LowerToNearest:
        case Ceiling_MoveToValue:
        case Ceiling_MoveToValueAndCrush:
        case Ceiling_MoveToValueTimes8:
        case Ceiling_RaiseByTexture:
        case Ceiling_RaiseByValue:
        case Ceiling_RaiseByValueTimes8:
        case Ceiling_RaiseInstant:
        case Ceiling_RaiseToHighest:
        case Ceiling_RaiseToHighestFloor:
        case Ceiling_RaiseToLowest:
        case Ceiling_RaiseToNearest:
        case Ceiling_Stop:
        case Ceiling_ToFloorInstant:
        case Ceiling_ToHighestInstant:
        case Ceiling_Waggle:
        // case ChangeCamera:
        // case ChangeSkill:
        // case ClearForceField:
        // case DamageThing:
        case Door_Animated:
        case Door_AnimatedClose:
        case Door_Close:
        case Door_CloseWaitOpen:
        case Door_LockedRaise:
        case Door_Open:
        case Door_Raise:
        case Door_WaitClose:
        case Door_WaitRaise:
        case Elevator_LowerToNearest:
        case Elevator_MoveToFloor:
        case Elevator_RaiseToNearest:
        // case Exit_Normal:
        // case Exit_Secret:
        // case ExtraFloor_LightOnly:
        case Floor_CrushStop:
        case Floor_Donut:
        case Floor_LowerByTexture:
        case Floor_LowerByValue:
        case Floor_LowerByValueTimes8:
        case Floor_LowerInstant:
        case Floor_LowerToHighest:
        case Floor_LowerToHighestEE:
        case Floor_LowerToLowest:
        case Floor_LowerToLowestCeiling:
        case Floor_LowerToLowestTxTy:
        case Floor_LowerToNearest:
        case Floor_MoveToValue:
        case Floor_MoveToValueAndCrush:
        case Floor_MoveToValueTimes8:
        case Floor_RaiseAndCrush:
        case Floor_RaiseAndCrushDoom:
        case Floor_RaiseByTexture:
        case Floor_RaiseByValue:
        case Floor_RaiseByValueTimes8:
        case Floor_RaiseByValueTxTy:
        case Floor_RaiseInstant:
        case Floor_RaiseToCeiling:
        case Floor_RaiseToHighest:
        case Floor_RaiseToLowest:
        case Floor_RaiseToLowestCeiling:
        case Floor_RaiseToNearest:
        case Floor_Stop:
        case Floor_ToCeilingInstant:
        case Floor_TransferNumeric:
        case Floor_TransferTrigger:
        case Floor_Waggle:
        case FloorAndCeiling_LowerByValue:
        case FloorAndCeiling_LowerRaise:
        case FloorAndCeiling_RaiseByValue:
        // case ForceField:
        // case FS_Execute:
        case Generic_Ceiling:
        case Generic_Crusher:
        case Generic_Crusher2:
        case Generic_Door:
        case Generic_Floor:
        case Generic_Lift:
        case Generic_Stairs:
        // case GlassBreak:
        // case HealThing:
        // case Light_ChangeToValue:
        // case Light_Fade:
        // case Light_Flicker:
        // case Light_ForceLightning:
        // case Light_Glow:
        // case Light_LowerByValue:
        // case Light_MaxNeighbor:
        // case Light_MinNeighbor:
        // case Light_RaiseByValue:
        // case Light_Stop:
        // case Light_Strobe:
        // case Light_StrobeDoom:
        // case Line_AlignCeiling:
        // case Line_AlignFloor:
        // case Line_Horizon:
        // case Line_Mirror:
        // case Line_QuickPortal:
        // case Line_SetAutomapFlags:
        // case Line_SetAutomapStyle:
        // case Line_SetBlocking:
        // case Line_SetHealth:
        // case Line_SetIdentification:
        // case Line_SetPortal:
        // case Line_SetPortalTarget:
        // case Line_SetTextureOffset:
        // case Line_SetTextureScale:
        // case NoiseAlert:
        case Pillar_Build:
        case Pillar_BuildAndCrush:
        case Pillar_Open:
        // case Plane_Align:
        // case Plane_Copy:
        case Plat_DownByValue:
        case Plat_DownWaitUpStay:
        case Plat_DownWaitUpStayLip:
        case Plat_PerpetualRaise:
        case Plat_PerpetualRaiseLip:
        case Plat_RaiseAndStayTx0:
        case Plat_Stop:
        case Plat_ToggleCeiling:
        case Plat_UpByValue:
        case Plat_UpByValueStayTx:
        case Plat_UpNearestWaitDownStay:
        case Plat_UpWaitDownStay:
        // case PointPush_SetForce:
        // case Polyobj_DoorSlide:
        // case Polyobj_DoorSwing:
        // case Polyobj_ExplicitLine:
        // case Polyobj_Move:
        // case Polyobj_MoveTimes8:
        // case Polyobj_MoveTo:
        // case Polyobj_MoveToSpot:
        // case Polyobj_OR_Move:
        // case Polyobj_OR_MoveTimes8:
        // case Polyobj_OR_MoveTo:
        // case Polyobj_OR_MoveToSpot:
        // case Polyobj_OR_RotateLeft:
        // case Polyobj_OR_RotateRight:
        // case Polyobj_RotateLeft:
        // case Polyobj_RotateRight:
        // case Polyobj_StartLine:
        // case Polyobj_Stop:
        // case Polyobj_StopSound:
        // case Radius_Quake:
        // case Scroll_Ceiling:
        // case Scroll_Floor:
        // case Scroll_Texture_Both:
        // case Scroll_Texture_Down:
        // case Scroll_Texture_Left:
        // case Scroll_Texture_Model:
        // case Scroll_Texture_Offsets:
        // case Scroll_Texture_Right:
        // case Scroll_Texture_Up:
        // case Scroll_Wall:
        // case Sector_Attach3dMidtex:
        // case Sector_ChangeFlags:
        // case Sector_ChangeSound:
        // case Sector_CopyScroller:
        // case Sector_Set3DFloor:
        // case Sector_SetCeilingGlow:
        // case Sector_SetCeilingPanning:
        // case Sector_SetCeilingScale:
        // case Sector_SetCeilingScale2:
        // case Sector_SetColor:
        // case Sector_SetContents:
        // case Sector_SetCurrent:
        // case Sector_SetDamage:
        // case Sector_SetFade:
        // case Sector_SetFloorGlow:
        // case Sector_SetFloorPanning:
        // case Sector_SetFloorScale:
        // case Sector_SetFloorScale2:
        // case Sector_SetFriction:
        // case Sector_SetGravity:
        // case Sector_SetHealth:
        // case Sector_SetLink:
        // case Sector_SetPlaneReflection:
        // case Sector_SetPortal:
        // case Sector_SetRotation:
        // case Sector_SetTranslucent:
        // case Sector_SetWind:
        // case SendToCommunicator:
        // case SetGlobalFogParameter:
        // case SetPlayerProperty:
        case Stairs_BuildDown:
        case Stairs_BuildDownDoom:
        case Stairs_BuildDownDoomSync:
        case Stairs_BuildDownSync:
        case Stairs_BuildUp:
        case Stairs_BuildUpDoom:
        case Stairs_BuildUpDoomCrush:
        case Stairs_BuildUpDoomSync:
        case Stairs_BuildUpSync:
        // case StartConversation:
        // case Static_Init:
        // case Teleport:
        // case Teleport_EndGame:
        // case Teleport_Line:
        // case Teleport_NewMap:
        // case Teleport_NoFog:
        // case Teleport_NoStop:
        // case Teleport_ZombieChanger:
        // case TeleportGroup:
        // case TeleportInSector:
        // case TeleportOther:
        // case Thing_Activate:
        // case Thing_ChangeTID:
        // case Thing_Damage:
        // case Thing_Deactivate:
        // case Thing_Destroy:
        // case Thing_Hate:
        // case Thing_Move:
        // case Thing_Projectile:
        // case Thing_ProjectileAimed:
        // case Thing_ProjectileGravity:
        // case Thing_ProjectileIntercept:
        // case Thing_Raise:
        // case Thing_Remove:
        // case Thing_SetConversation:
        // case Thing_SetGoal:
        // case Thing_SetSpecial:
        // case Thing_SetTranslation:
        // case Thing_Spawn:
        // case Thing_SpawnFacing:
        // case Thing_SpawnNoFog:
        // case Thing_Stop:
        // case ThrustThing:
        // case ThrustThingZ:
        case Transfer_CeilingLight:
        case Transfer_FloorLight:
        case Transfer_Heights:
        case Transfer_WallLight:
            // case TranslucentLine:
            // case UsePuzzleItem:
            return true;
        default: return false;
    }
}

bool RT_IsSectorMovable( const sector_t* sector )
{
    if( !sector )
    {
        return false;
    }

    auto isTaggedExplicitly = []( const sector_t& s ) {
        if( !primaryLevel )
        {
            return false;
        }

        if( g_stairsSectors.contains( s.Index() ) )
        {
            return true;
        }

        auto l_safeToIgnoreTag = [ & ]( int tag ) {
            return g_tagsSafeToIgnore.contains( tag );
        };

        // if there's at least one NON-safe tag on this sector, it's tagged
        const auto sectorTags = primaryLevel->tagManager.RT_GetAllSectorTags( &s );
        return !std::ranges::all_of( sectorTags, l_safeToIgnoreTag );
    };

    auto isTaggedImplicitly = []( const sector_t& s ) {
        for( const line_t* l : s.Lines )
        {
            if( IsTaggedByTag0( l, &s ) )
            {
                return true;
            }
        }
        return false;
    };

    return isTaggedExplicitly( *sector ) || isTaggedImplicitly( *sector );
}

bool RT_IsTexAnimated( int texnum, const std::vector< bool >& animatedTexnums )
{
    if( texnum < 0 || static_cast< uint32_t >( texnum ) >= animatedTexnums.size() )
    {
        assert( 0 );
        return false;
    }
    return animatedTexnums[ texnum ];
}

bool RT_IsSectorExportable( const sector_t*            sector,
                            bool                       ceiling,
                            const std::vector< bool >& animatedTexnums )
{
    if( !sector )
    {
        assert( 0 );
        return false;
    }

    // e.g. nukage, lava
    bool isAnimated = RT_IsTexAnimated(
        sector->GetTexture( ceiling ? sector_t::ceiling : sector_t::floor ).GetIndex(),
        animatedTexnums );

    return !isAnimated && !RT_IsSectorMovable( sector );
}

bool RT_IsWallExportable( const seg_t* seg, const std::vector< bool >& animatedTexnums )
{
    if( !seg )
    {
        assert( 0 );
        return false;
    }

    // e.g. switches
    auto isAnimated = [ &animatedTexnums ]( const side_t* side ) {
        if( side )
        {
            return RT_IsTexAnimated( side->GetTexture( 0 ).GetIndex(), animatedTexnums ) ||
                   RT_IsTexAnimated( side->GetTexture( 1 ).GetIndex(), animatedTexnums ) ||
                   RT_IsTexAnimated( side->GetTexture( 2 ).GetIndex(), animatedTexnums );
        }
        return false;
    };

    auto isAdjacentSectorMovable = []( const seg_t& s ) {
        if( s.linedef )
        {
            return RT_IsSectorMovable( s.linedef->backsector ) ||
                   RT_IsSectorMovable( s.linedef->frontsector );
        }
        return true;
    };

    return !isAnimated( seg->sidedef ) && !isAdjacentSectorMovable( *seg );
}

enum
{
    RT_WALL_PEGGED_TOP    = 1,
    RT_WALL_PEGGED_BOTTOM = 2,
};

// Pegged texture moves with a Sector that moves
uint8_t RT_WallPeggedFlags( const seg_t* seg )
{
    if( !seg || !seg->linedef )
    {
        return false;
    }

    // if double sided
    if( seg->backsector )
    {
        int fs = RT_WALL_PEGGED_TOP | RT_WALL_PEGGED_BOTTOM;

        if( seg->linedef->flags & ML_DONTPEGTOP )
        {
            fs = ( fs & ~( RT_WALL_PEGGED_TOP ) );
        }

        if( seg->linedef->flags & ML_DONTPEGBOTTOM )
        {
            fs = ( fs & ~( RT_WALL_PEGGED_BOTTOM ) );
        }
        
        return uint8_t( fs );
    }
    else
    {
        // one sided always pegged
        return RT_WALL_PEGGED_TOP | RT_WALL_PEGGED_BOTTOM;
    }
}

auto rt_sectorCeilingExportable = std::vector< bool >{};
auto rt_sectorFloorExportable   = std::vector< bool >{};
auto rt_wallExportable          = std::vector< bool >{};
auto rt_wallPegged              = std::vector< uint8_t >{};

} // anonymous namespace

void RT_BakeExportables( const std::vector< bool >& animatedTexnums )
{
    rt_sectorCeilingExportable.clear();
    rt_sectorFloorExportable.clear();
    rt_wallExportable.clear();
    rt_wallPegged.clear();
    g_tagsSafeToIgnore.clear();
    g_stairsSectors.clear();

    if( !primaryLevel )
    {
        return;
    }

    RT_CacheTagsAndSpecials();

    rt_sectorCeilingExportable.resize( primaryLevel->sectors.Size(), false );
    rt_sectorFloorExportable.resize( primaryLevel->sectors.Size(), false );
    for( uint32_t i = 0; i < primaryLevel->sectors.Size(); i++ )
    {
        rt_sectorCeilingExportable[ i ] =
            RT_IsSectorExportable( &primaryLevel->sectors[ i ], true, animatedTexnums );
        rt_sectorFloorExportable[ i ] =
            RT_IsSectorExportable( &primaryLevel->sectors[ i ], false, animatedTexnums );
    }

    rt_wallExportable.resize( primaryLevel->segs.Size(), false );
    for( uint32_t i = 0; i < primaryLevel->segs.Size(); i++ )
    {
        rt_wallExportable[ i ] = RT_IsWallExportable( &primaryLevel->segs[ i ], animatedTexnums );
    }

    rt_wallPegged.resize( primaryLevel->segs.Size(), false );
    for( uint32_t i = 0; i < primaryLevel->segs.Size(); i++ )
    {
        rt_wallPegged[ i ] = RT_WallPeggedFlags( &primaryLevel->segs[ i ] );
    }
}

bool RT_IsSectorExportable2( int sectornum, bool ceiling )
{
    if( sectornum >= 0 )
    {
        const auto& arr = ceiling ? rt_sectorCeilingExportable : rt_sectorFloorExportable;

        if( sectornum < int( arr.size() ) )
        {
            return arr[ sectornum ];
        }
    }
    return false;
}

bool RT_IsSectorExportable( const sector_t* sector, bool ceiling )
{
    if( sector )
    {
        return RT_IsSectorExportable2( sector->sectornum, ceiling );
    }
    return false;
}

bool RT_IsWallExportable( const seg_t* seg )
{
    if( seg && seg->segnum >= 0 )
    {
        const auto segnum = static_cast< uint32_t >( seg->segnum );

        if( segnum < rt_wallExportable.size() )
        {
            return rt_wallExportable[ segnum ];
        }
    }
    return false;
}

bool RT_IsWallNoMotionVectors( const seg_t* seg, side_t::ETexpart part )
{
    if( part == side_t::top || part == side_t::bottom )
    {
        if( seg && seg->segnum >= 0 && uint32_t( seg->segnum ) < rt_wallPegged.size() )
        {
            if( part == side_t::top )
            {
                // inverse logic, as top grows from bottom to up
                return !( ( rt_wallPegged[ seg->segnum ] ) & RT_WALL_PEGGED_TOP );
            }
            else
            {
                return ( rt_wallPegged[ seg->segnum ] ) & RT_WALL_PEGGED_BOTTOM;
            }
        }
    }
    return true;
}


void RT_SpawnFluid( int             count,
                    const FVector3& position,
                    const FVector3& velocity,
                    float           dispersionDegrees )
{
    if( count <= 0 || !cvar::rt_fluid_available || !cvar::rt_fluid )
    {
        return;
    }
    count = std::min( count, 10000 );

    if( rt.rgSpawnFluid )
    {
        auto info = RgSpawnFluidInfo{
            .sType                  = RG_STRUCTURE_TYPE_SPAWN_FLUID_INFO,
            .pNext                  = nullptr,
            .position               = { float( position.X ) * ONEGAMEUNIT_IN_METERS,
                                        float( position.Y ) * ONEGAMEUNIT_IN_METERS,
                                        float( position.Z ) * ONEGAMEUNIT_IN_METERS },
            .radius                 = 0.05f,
            .velocity               = { float( velocity.X ) * ONEGAMEUNIT_IN_METERS,
                                        float( velocity.Y ) * ONEGAMEUNIT_IN_METERS,
                                        float( velocity.Z ) * ONEGAMEUNIT_IN_METERS },
            .dispersionVelocity     = 0.8f,
            .dispersionAngleDegrees = dispersionDegrees,
            .count                  = uint32_t( count ),
        };

        RgResult r = rt.rgSpawnFluid( &info );
        RG_CHECK( r );
    }
}

void RT_RegisterFullscreenImage( const char* texture )
{
    if( !texture || texture[ 0 ] == '\0' )
    {
        return;
    }

    constexpr uint8_t empty[] = { 0, 0, 0, 0 };

    auto info = RgOriginalTextureInfo{
        .sType        = RG_STRUCTURE_TYPE_ORIGINAL_TEXTURE_INFO,
        .pNext        = nullptr,
        .pTextureName = texture,
        .pPixels      = empty,
        .size         = { 1, 1 },
        .filter       = RG_SAMPLER_FILTER_LINEAR,
        .addressModeU = RG_SAMPLER_ADDRESS_MODE_CLAMP,
        .addressModeV = RG_SAMPLER_ADDRESS_MODE_CLAMP,
    };

    RgResult r = rt.rgProvideOriginalTexture( &info );
    RG_CHECK( r );
}

void RT_DeleteFullscreenImage( const char* texture )
{
    if( !texture || texture[ 0 ] == '\0' )
    {
        return;
    }

    RgResult r = rt.rgMarkOriginalTextureAsDeleted( texture );
    RG_CHECK( r );
}

void RT_DrawFullscreenImage( const char* texture,
                             float       opacity,
                             FVector4    background_color,
                             FVector4    foreground_color,
                             float       splitef = 0,
                             float       scale   = 1 )
{
    if( !texture || texture[ 0 ] == '\0' )
    {
        return;
    }

    if( opacity < 0.001f )
    {
        return;
    }

    static constexpr uint32_t indices[] = { 0, 1, 2, 2, 3, 0 };

    static constexpr RgPrimitiveVertex verts_fullscreen[] = {
        { .position = { -1, +1, 0 }, .texCoord = { 0, 1 }, .color = 0xFFFFFFFF },
        { .position = { -1, -1, 0 }, .texCoord = { 0, 0 }, .color = 0xFFFFFFFF },
        { .position = { +1, -1, 0 }, .texCoord = { 1, 0 }, .color = 0xFFFFFFFF },
        { .position = { +1, +1, 0 }, .texCoord = { 1, 1 }, .color = 0xFFFFFFFF },
    };

    RgPrimitiveVertex verts_16by9[] = {
        verts_fullscreen[ 0 ],
        verts_fullscreen[ 1 ],
        verts_fullscreen[ 2 ],
        verts_fullscreen[ 3 ],
    };

    {
        const RgExtent2D wnd = RT_GetCurrentWindowSize();

        float xwin = ( float )wnd.width / ( float )wnd.height;
        float ximg = 16.0f / 9.0f;

        float tx, ty;
        if( ximg < xwin )
        {
            tx = ximg / xwin;
            ty = 1.0f;
        }
        else
        {
            tx = 1.0f;
            ty = xwin / ximg;
        }

#define VectorSet2( ptr, x, y ) \
    ( ptr )[ 0 ] = ( x );      \
    ( ptr )[ 1 ] = ( y )

        tx = ( 1 - 1 / tx ) / 2;
        ty = ( 1 - 1 / ty ) / 2;

        VectorSet2( verts_16by9[ 0 ].texCoord, tx, 1 - ty );
        VectorSet2( verts_16by9[ 1 ].texCoord, tx, ty );
        VectorSet2( verts_16by9[ 2 ].texCoord, 1 - tx, ty );
        VectorSet2( verts_16by9[ 3 ].texCoord, 1 - tx, 1 - ty );
    }

    // scale
    {
        for( RgPrimitiveVertex& v : verts_16by9 )
        {
            v.texCoord[ 0 ] = ( ( v.texCoord[ 0 ] - 0.5f ) / scale ) + 0.5f;
            v.texCoord[ 1 ] = ( ( v.texCoord[ 1 ] - 0.5f ) / scale ) + 0.5f;
        }
    }

    constexpr static float viewproj[ 16 ] = {
        1, 0, 0, 0, //
        0, 1, 0, 0, //
        0, 0, 1, 0, //
        0, 0, 0, 1, //
    };

    auto l_drawcolor = []( const RgPrimitiveVertex( &verts )[ 4 ],
                           RgColor4DPacked32        color ) {
        auto ui = RgMeshPrimitiveSwapchainedEXT{
            .sType           = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_SWAPCHAINED_EXT,
            .pNext           = nullptr,
            .flags           = 0,
            .pViewport       = nullptr,
            .pView           = nullptr,
            .pProjection     = nullptr,
            .pViewProjection = viewproj,
        };

        auto prim = RgMeshPrimitiveInfo{
            .sType                = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_INFO,
            .pNext                = &ui,
            .flags                = RG_MESH_PRIMITIVE_TRANSLUCENT,
            .primitiveIndexInMesh = 0,
            .pVertices            = verts,
            .vertexCount          = uint32_t( std::size( verts ) ),
            .pIndices             = indices,
            .indexCount           = std::size( indices ),
            .pTextureName         = nullptr,
            .textureFrame         = 0,
            .color                = color,
            .emissive             = 0,
            .classicLight         = 1.0f,
        };

        RgResult r = rt.rgUploadMeshPrimitive( nullptr, &prim );
        RG_CHECK( r );
    };

    // back color
    if( background_color.W > 0 )
    {
        l_drawcolor( verts_fullscreen,
                     rt.rgUtilPackColorFloat4D( background_color.X, //
                                                background_color.Y,
                                                background_color.Z,
                                                background_color.W ) );
    }

    if( splitef > 0 )
    {
        RgPrimitiveVertex half[ 4 ];
        static_assert( sizeof( half ) == sizeof( verts_fullscreen ) );

        // left, rises top -> bottom
        {
            memcpy( half, verts_fullscreen, sizeof( verts_fullscreen ) );
            VectorSet2( half[ 0 ].position, -1, +1 );
            VectorSet2( half[ 1 ].position, -1, std::lerp( 1, -1, splitef ) );
            VectorSet2( half[ 2 ].position, 0, std::lerp( 1, -1, splitef ) );
            VectorSet2( half[ 3 ].position, 0, +1 );
            l_drawcolor( half, RG_PACKED_COLOR_WHITE );
        }
        // right, rises bottom -> top
        {
            memcpy( half, verts_fullscreen, sizeof( verts_fullscreen ) );
            VectorSet2( half[ 0 ].position, 0, std::lerp( -1, 1, splitef ) );
            VectorSet2( half[ 1 ].position, 0, -1 );
            VectorSet2( half[ 2 ].position, +1, -1 );
            VectorSet2( half[ 3 ].position, +1, std::lerp( -1, 1, splitef ) );
            l_drawcolor( half, RG_PACKED_COLOR_WHITE );
        }
    }

    // image
    {
        auto ui = RgMeshPrimitiveSwapchainedEXT{
            .sType           = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_SWAPCHAINED_EXT,
            .pNext           = nullptr,
            .flags           = 0,
            .pViewport       = nullptr,
            .pView           = nullptr,
            .pProjection     = nullptr,
            .pViewProjection = viewproj,
        };

        auto prim = RgMeshPrimitiveInfo{
            .sType                = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_INFO,
            .pNext                = &ui,
            .flags                = RG_MESH_PRIMITIVE_TRANSLUCENT,
            .primitiveIndexInMesh = 0,
            .pVertices            = verts_16by9,
            .vertexCount          = std::size( verts_16by9 ),
            .pIndices             = indices,
            .indexCount           = std::size( indices ),
            .pTextureName         = texture,
            .textureFrame         = 0,
            .color                = rt.rgUtilPackColorFloat4D( 1.0f, 1.0f, 1.0f, opacity ),
            .emissive             = 0,
            .classicLight         = 1.0f,
        };

        RgResult r = rt.rgUploadMeshPrimitive( nullptr, &prim );
        RG_CHECK( r );
    }

    // foreground color
    if( foreground_color.W > 0 )
    {
        l_drawcolor( verts_fullscreen,
                     rt.rgUtilPackColorFloat4D( foreground_color.X, //
                                                foreground_color.Y,
                                                foreground_color.Z,
                                                foreground_color.W ) );
    }

    #undef VectorSet2
}

extern FSoundID T_FindSound( const char* name );

static int         g_title_begintick{ -1 };
static int         g_title_endtick{ -1 };
static int         g_title_fadeouttics{ 0 };
static std::string g_title_requested{};
static std::string g_title_uploaded{};
static bool        g_title_soundplayed{ false };

void RT_StartTitleImage( const char* imagepath,
                         int         begin_maptime,
                         int         end_maptime,
                         int         fadeout_tics )
{
    if( !imagepath || imagepath[ 0 ] == '\0' )
    {
        g_title_requested.clear();
        g_title_endtick     = -1;
        g_title_begintick   = -1;
        g_title_fadeouttics = 0;
        g_title_soundplayed = false;
        return;
    }

    g_title_requested   = imagepath;
    g_title_begintick   = begin_maptime;
    g_title_endtick     = end_maptime;
    g_title_fadeouttics = fadeout_tics;
    g_title_soundplayed = false;
}

static void RT_DrawTitle()
{
    if( g_title_requested.empty() )
    {
        RT_ClearTitles();
        return;
    }

    if( level.sectors.Size() <= 0 )
    {
        RT_ClearTitles();
        return;
    }

    if( level.maptime >= g_title_endtick )
    {
        RT_ClearTitles();
        return;
    }

    // upload texture
    if( g_title_uploaded != g_title_requested )
    {
        if( !g_title_uploaded.empty() )
        {
            RT_DeleteFullscreenImage( g_title_uploaded.c_str() );
        }

        RT_RegisterFullscreenImage( g_title_requested.c_str() );
        g_title_uploaded = g_title_requested;
    }

    if( g_title_begintick > 0 )
    {
        if( level.maptime < g_title_begintick )
        {
            return;
        }
    }

    float alpha = 1.0f;
    if( g_title_fadeouttics > 0 )
    {
        int ticksleft = g_title_endtick - level.maptime;
        if( ticksleft < g_title_fadeouttics )
        {
            alpha = float( ticksleft ) / float( g_title_fadeouttics );

            // gamma
            alpha = alpha * alpha;
        }
    }

    RT_DrawFullscreenImage( g_title_uploaded.c_str(), //
                            alpha,
                            { 0, 0, 0, alpha * 0.3f },
                            { 0, 0, 0, 0 } );
    
    if( !g_title_soundplayed )
    {
        g_title_soundplayed = true;

        if( soundEngine )
        {
            // HACKHACK
            if( g_title_uploaded == "title/iconofsin" )
            {
                return;
            }

            FSoundID sound = T_FindSound( "sounds/cutscene/boom.ogg" );
            soundEngine->StartSound(
                SOURCE_None, nullptr, nullptr, CHAN_AUTO, CHANF_UI, sound, 1.0f, ATTN_NONE );
        }
    }
}

static void RT_ClearTitles()
{
    if( !g_title_uploaded.empty() )
    {
        RT_DeleteFullscreenImage( g_title_uploaded.c_str() );
    }
    g_title_requested.clear();
    g_title_uploaded.clear();
    g_title_begintick   = -1;
    g_title_endtick     = -1;
    g_title_fadeouttics = 0;
    g_title_soundplayed = false;
}

extern bool rt_isdoom2;

static void RT_InjectTitleIntoDoomMap( const char* mapname )
{
    if( !rt_isdoom2 )
    {
        return;
    }
    
    if( !mapname || mapname[ 0 ] == '\0' )
    {
        return;
    }

    const char* titlename = nullptr;
    {
        if( stricmp( mapname, "map12" ) == 0 )
        {
            titlename = "title/ep2";
        }
        else if( stricmp( mapname, "map21" ) == 0 )
        {
            titlename = "title/ep3";
        }
    }

    if( !titlename )
    {
        return;
    }

    constexpr int BEGIN_TICS    = int( 1.5f * TICRATE );
    constexpr int DURATION_TICS = int( 5.0f * TICRATE );
    constexpr int FADEOUT_TICS  = int( 3.0f * TICRATE );

    RT_StartTitleImage( titlename, BEGIN_TICS, BEGIN_TICS + DURATION_TICS, FADEOUT_TICS );
}
