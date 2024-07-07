#include "rt_cvars.h"
#include "rt_helpers.h"

#include "i_time.h"
#include "texturemanager.h"
#include "v_draw.h"
#include "v_font.h"

extern void RT_FirstStartDone();
extern bool g_noinput_onstart;

#pragma region VM Boilerplate
#include "actor.h"
#include "vm.h"

#define MAKE_VM_WRAPPER( Type, StateType )                                          \
    class Type : public DObject                                                     \
    {                                                                               \
    public:                                                                         \
        Type() = default;                                                           \
        bool Responder( FInputEvent* ev )                                           \
        {                                                                           \
            return ev ? input( m_state, *ev ) : false;                              \
        }                                                                           \
        bool Ticker()                                                               \
        { /* if tick() returns true, then finish */                                 \
            return !tick( m_state );                                                \
        }                                                                           \
        void Start()                                                                \
        {                                                                           \
            start( m_state );                                                       \
        }                                                                           \
        void Drawer()                                                               \
        {                                                                           \
            draw( m_state );                                                        \
        }                                                                           \
        void OnDestroy() override                                                   \
        {                                                                           \
            destroy( m_state );                                                     \
            Super::OnDestroy();                                                     \
        }                                                                           \
                                                                                    \
    private:                                                                        \
        StateType m_state{};                                                        \
        DECLARE_CLASS( Type, DObject )                                              \
    };                                                                              \
    IMPLEMENT_CLASS( Type, false, false ) DEFINE_ACTION_FUNCTION( Type, Responder ) \
    {                                                                               \
        PARAM_SELF_PROLOGUE( Type );                                                \
        PARAM_POINTER( evt, FInputEvent );                                          \
        ACTION_RETURN_BOOL( self->Responder( evt ) );                               \
    }                                                                               \
    DEFINE_ACTION_FUNCTION( Type, Ticker )                                          \
    {                                                                               \
        PARAM_SELF_PROLOGUE( Type );                                                \
        ACTION_RETURN_BOOL( self->Ticker() );                                       \
    }                                                                               \
    DEFINE_ACTION_FUNCTION( Type, Start )                                           \
    {                                                                               \
        PARAM_SELF_PROLOGUE( Type );                                                \
        self->Start();                                                              \
        return 0;                                                                   \
    }                                                                               \
    DEFINE_ACTION_FUNCTION( Type, Drawer )                                          \
    {                                                                               \
        PARAM_SELF_PROLOGUE( Type );                                                \
        self->Drawer();                                                             \
        return 0;                                                                   \
    }
#pragma endregion

#include <array>


// -------------- //


extern const char* g_rt_cutscenename;
static auto        g_cstime_start  = std::optional< double >{};
static auto        g_cstime_paused = std::optional< double >{};

extern bool g_rt_showfirststartscene;
bool        g_rt_showfirststartscene_untiemouse{ false };

extern void RT_DrawSettingDescription( std::string_view rtkey, bool forFirstStartMenu );
extern void RT_ForceCamera( const FVector3 position, const DRotator& rotation, float fovYDegrees );

bool RT_ForceCaptureMouse()
{
    // capture mouse in cutscenes
    if( g_rt_cutscenename && g_rt_cutscenename[ 0 ] != '\0' )
    {
        if( g_cstime_paused )
        {
            return false;
        }
        return true;
    }
    if( g_rt_showfirststartscene )
    {
        if( !g_rt_showfirststartscene_untiemouse )
        {
            return true;
        }
    }
    return false;
}


#define RT_HOOK_INTRO 1
#define RT_INTRO_SKIPPABLE 0

namespace
{
namespace intro
{
    constexpr auto SkipDuration = 1.0f;

    struct state_t
    {
#if RT_INTRO_SKIPPABLE
        float  m_skipProgress{ 0 };
        double m_prevtime{ 0 };
        int    m_skipButtonPressed{ 0 };
#endif
    };

    void cstime_pause()
    {
        assert( !g_cstime_paused );
        g_cstime_paused = RT_GetCurrentTime();
    }
    void cstime_continue()
    {
        assert( g_cstime_paused );

        if( g_cstime_start && g_cstime_paused )
        {
            double paused_duration = RT_GetCurrentTime() - *g_cstime_paused;
            if( paused_duration > 0 )
            {
                g_cstime_start = *g_cstime_start + paused_duration;
            }
        }
        g_cstime_paused = std::nullopt;
    }
    double cstime_now()
    {
        const double realnow = RT_GetCurrentTime();

        if( !g_cstime_start )
        {
            assert( 0 );
            g_cstime_start = realnow;
        }

        double sec = realnow - *g_cstime_start;

        // if paused, override with the time to the pause time point
        if( g_cstime_paused )
        {
            sec = *g_cstime_paused - *g_cstime_start;
        }

        assert( sec >= 0 );
        return std::max( 0.0, sec );
    }

    void start( state_t& state )
    {
        g_rt_cutscenename = "cs_intro";
        g_cstime_start    = RT_GetCurrentTime();
        g_cstime_paused   = {};
        cvar::rt_classic  = 0; // cutscenes work on replacements, no classic mode available...
    }

    void destroy( state_t& state )
    {
        g_rt_cutscenename = nullptr;
    }

    bool input( state_t& state, const FInputEvent& ev )
    {
        // pause
        if( !g_cstime_paused )
        {
            if( ev.KeyScan == KEY_ESCAPE || ev.KeyScan == KEY_BACKSPACE ||
                // ev.KeyScan == KEY_LCTRL || ev.KeyScan == KEY_RCTRL ||
                // ev.KeyScan == KEY_LALT || ev.KeyScan == KEY_RALT ||
                // ev.KeyScan == KEY_RSHIFT || ev.KeyScan == KEY_LSHIFT ||
                // ev.KeyScan == KEY_TAB ||
                ev.KeyScan == KEY_PAD_B )
            {
                if( ev.Type == EV_KeyDown )
                {
                    cstime_pause();
                    return true;
                }
            }

            if( ev.KeyScan == KEY_SPACE )
            {
                if( ev.Type == EV_KeyUp )
                {
                    cstime_pause();
                    return true;
                }
            }
        }

        // unpause
        if( g_cstime_paused )
        {
            if( ev.KeyScan == KEY_SPACE || ev.KeyScan == KEY_ENTER || ev.KeyScan == KEY_PAD_A )
            {
                if( ev.Type == EV_KeyUp )
                {
                    cstime_continue();
                    return true;
                }
            }
        }

#if RT_INTRO_SKIPPABLE
        // hold to skip
        if( !g_cstime_paused )
        {
            if( ev.KeyScan == KEY_ENTER || ev.KeyScan == KEY_PAD_A )
            {
                if( ev.Type == EV_KeyDown )
                {
                    state.m_skipButtonPressed++;
                    return true;
                }
                if( ev.Type == EV_KeyUp )
                {
                    state.m_skipButtonPressed = std::max( 0, state.m_skipButtonPressed - 1 );
                    return true;
                }
            }
        }
#endif

        return false;
    }

    bool tick( state_t& state )
    {
#if RT_INTRO_SKIPPABLE
        float deltatime;
        {
            double curtime   = RT_GetCurrentTime();
            deltatime        = float( curtime - state.m_prevtime );
            state.m_prevtime = curtime;
        }

        float dt             = state.m_skipButtonPressed ? deltatime : -deltatime;
        state.m_skipProgress = std::max( state.m_skipProgress + dt, 0.0f );

        if( state.m_skipProgress > SkipDuration )
        {
    // DEVTOOLS: RESTART CUTSCENE
    #if 1
            g_cstime_start  = RT_GetCurrentTime();
            g_cstime_paused = g_cstime_paused ? g_cstime_start : std::nullopt;
            return false;
    #else
            g_rt_cutscenename = nullptr;
            return true;
    #endif
        }
#endif

        return false;
    }

    int blink_timer( double base, double intervalOn = 0.6, double intervalOff = 0.2 )
    {
        double curtime = RT_GetCurrentTime() - base;

        double i = std::fmod( curtime, intervalOn + intervalOff );
        return i <= intervalOn ? 1 : 0;
    }

    void draw( state_t& state )
    {
        auto* font        = SmallFont;
        int text_height = font->GetHeight();
        int safe        = 4;
        int safe_upper  = 6;

#if RT_INTRO_SKIPPABLE
        // text = "Hold ENTER to skip";
        if( state.m_skipProgress > 0 )
        {
            float w = 0.5f * ( 1 - state.m_skipProgress );
            float h = float( safe + text_height + safe_upper ) / float( 200 );
            ClearRect( twod,
                       twod->GetWidth() * w,
                       twod->GetHeight() * ( 1 - h ),
                       twod->GetWidth() * ( 1 - w ),
                       twod->GetHeight(),
                       0,
                       MAKEARGB( 255, 255, 255, 255 ) );
        }
#endif

        const char* text = g_cstime_paused && blink_timer( *g_cstime_paused, 0.9, 0.4 )
                               ? "Press SPACE to continue"
                               : nullptr;

        if( text )
        {
            int text_width = font->StringWidth( text );
            DrawText( twod,
                      font,
                      CR_WHITE,
                      320 / 2 - text_width / 2,
                      200 - text_height - safe,
                      text,
                      DTA_320x200,
                      true,
                      DTA_KeepRatio,
                      false,
                      TAG_DONE );
        }
    }
} // intro
} // anonymous

float RT_CutsceneTime()
{
    return float( intro::cstime_now() );
}

void RT_OnHwndActivate( bool active )
{
    if( !active )
    {
        if( g_rt_cutscenename && !g_cstime_paused )
        {
            intro::cstime_pause();
        }
    }
}

// -------------- //


static double g_rt_mouse_x = 0;
static double g_rt_mouse_y = 0;

void RT_FirstStartSetMouse( float x, float y )
{
    if( RT_ForceCaptureMouse() )
    {
        g_rt_mouse_x += x;
        g_rt_mouse_y += y;
    }
}

namespace
{
namespace firststart
{
    enum item_t
    {
        ITEM_PAGE0_PRESSANYKEY,

        ITEM_MODE,
        ITEM_PRESET,
        ITEM_FRAMEGEN,
        ITEM_VSYNC,
        ITEM_PAGE1_ACCEPT,

        ITEM_HDR,
        ITEM_PAGE2_ACCEPT,
    };
    // clang-format off
    template< int Page > constexpr auto PageBeginEnd = std::pair< item_t, item_t >{};
    template<>           constexpr auto PageBeginEnd< 0 > = std::pair{ ITEM_PAGE0_PRESSANYKEY, ITEM_PAGE0_PRESSANYKEY };
    template<>           constexpr auto PageBeginEnd< 1 > = std::pair{ ITEM_MODE, ITEM_PAGE1_ACCEPT };
    template<>           constexpr auto PageBeginEnd< 2 > = std::pair{ ITEM_HDR, ITEM_PAGE2_ACCEPT };
    // clang-format on

    // from gltf
    constexpr auto DefaultCameraPosition = FVector3{ 18.9429f, -0.193118f, 0.608346f };
    constexpr auto DefaultCameraRotation = DRotator{ nullAngle, DAngle::fromDeg( 180 ), nullAngle };

    struct state_t
    {
        int    page{ 0 };
        item_t current{ ITEM_MODE };
        bool   finished{ false };

        std::optional< item_t > showDescription{};

        bool     cameraActive{ false };
        bool     cameraActiveZoom{ false };
        FVector3 cameraPosition{ DefaultCameraPosition };
        DRotator cameraRotation{ DefaultCameraRotation };
        float    cameraZoom{ 0.0f }; // -1: outside, 0: default, 1: full zoom
        bool     cameraOut{ false };

        bool   fadeinWas{ false };
        double fadeinStartTime{ 0 };

        std::optional< double > page1StartTime{};

#if RT_HOOK_INTRO
        std::optional< intro::state_t > introstate{};
#endif
    };

    void start( state_t& state )
    {
        g_rt_mouse_x = 0;
        g_rt_mouse_y = 0;

        if( !cvar::rt_firststart )
        {
            state.finished = true;
            return;
        }

        g_rt_showfirststartscene = true;
    }

    void destroy( state_t& state )
    {
        g_rt_showfirststartscene = false;
    }

    bool vsync_forced()
    {
        return cvar::rt_framegen && cvar::rt_upscale_dlss > 0 && cvar::rt_available_dlss3fg;
    }

    bool input( state_t& state, const FInputEvent& ev )
    {
#if RT_HOOK_INTRO
        if( state.introstate )
        {
            return intro::input( *state.introstate, ev );
        }
#endif

        if( state.page == 0 )
        {
            if( ev.Type == EV_KeyDown || ev.Type == EV_KeyUp )
            {
                state.page = 1;
                state.page1StartTime = RT_GetCurrentTime();
                return true;
            }
            return false;
        }

        // if esc, then free / capture the mouse
        if( ev.KeyScan == KEY_ESCAPE )
        {
            if( ev.Type == EV_KeyUp ) // ignore other esc event types
            {
                g_rt_showfirststartscene_untiemouse = !g_rt_showfirststartscene_untiemouse;
            }
        }
        else
        {
            // but on any other key event, capture the mouse
            g_rt_showfirststartscene_untiemouse = false;
        }

        if( ev.KeyScan == KEY_MOUSE1 )
        {
            if( ev.Type == EV_KeyDown )
            {
                state.cameraActive = true;
                return true;
            }
            if( ev.Type == EV_KeyUp )
            {
                state.cameraActive = false;
                return true;
            }
        }

        if( ev.KeyScan == KEY_MOUSE2 )
        {
            if( ev.Type == EV_KeyDown )
            {
                state.cameraActive     = true;
                state.cameraActiveZoom = true;
                return true;
            }
            if( ev.Type == EV_KeyUp )
            {
                state.cameraActive     = false;
                state.cameraActiveZoom = false;
                return true;
            }
        }

        if( ev.Type == EV_KeyDown )
        {
            int  downup    = 0;
            int  leftright = 0;
            bool enter     = false;
            bool esc       = false;
            bool showdesc  = false;

            switch( ev.KeyScan )
            {
                case KEY_UPARROW:
                case KEY_PAD_LTHUMB_UP:
                case KEY_PAD_RTHUMB_UP:
                case KEY_PAD_DPAD_UP: {
                    downup = -1;
                    break;
                }
                case KEY_DOWNARROW:
                case KEY_PAD_LTHUMB_DOWN:
                case KEY_PAD_RTHUMB_DOWN:
                case KEY_PAD_DPAD_DOWN: {
                    downup = +1;
                    break;
                }
                case KEY_LEFTARROW:
                case KEY_PAD_LTHUMB_LEFT:
                case KEY_PAD_RTHUMB_LEFT:
                case KEY_PAD_DPAD_LEFT: {
                    leftright = -1;
                    break;
                }
                case KEY_RIGHTARROW:
                case KEY_PAD_LTHUMB_RIGHT:
                case KEY_PAD_RTHUMB_RIGHT:
                case KEY_PAD_DPAD_RIGHT: {
                    leftright = +1;
                    break;
                }
                case KEY_ENTER:
                case KEY_SPACE:
                case KEY_PAD_A: {
                    enter = true;
                    break;
                }
                case KEY_ESCAPE:
                case KEY_BACKSPACE: {
                    esc = true;
                    break;
                };
                default: break;
            }

            if( ev.KeyChar == 'f' || ev.KeyChar == 'F' )
            {
                showdesc = true;
            }

            if( !state.showDescription )
            {
                if( showdesc )
                {
                    switch( state.current )
                    {
                        case ITEM_MODE:
                        case ITEM_PRESET:
                        case ITEM_FRAMEGEN:
                        case ITEM_VSYNC:
                        case ITEM_HDR: {
                            state.showDescription = state.current;
                            break;
                        }
                        case ITEM_PAGE1_ACCEPT:
                        case ITEM_PAGE2_ACCEPT:
                        default: break;
                    }
                }
            }
            else
            {
                if( enter || esc || showdesc )
                {
                    state.showDescription = std::nullopt;
                    return true;
                }
                // ignore other inputs, if showing description
                return false;
            }

            if( enter )
            {
                if( state.current == ITEM_PAGE1_ACCEPT )
                {
                    if( !cvar::rt_hdr_available )
                    {
                        // no HDR => skip the HDR page
                        state.finished = true;
                        return true;
                    }

                    state.page    = 2;
                    state.current = ITEM_PAGE2_ACCEPT;
                    return true;
                }
                if( state.current == ITEM_PAGE2_ACCEPT )
                {
                    state.finished = true;
                    return true;
                }
            }

            if( downup != 0 )
            {
                switch( state.page )
                {
                    case 1:
                        state.current = item_t( std::clamp< int >( state.current + downup,
                                                                   PageBeginEnd< 1 >.first,
                                                                   PageBeginEnd< 1 >.second ) );
                        return true;
                    case 2:
                        state.current = item_t( std::clamp< int >( state.current + downup,
                                                                   PageBeginEnd< 2 >.first,
                                                                   PageBeginEnd< 2 >.second ) );
                        return true;
                    default: assert( 0 ); break;
                }
            }

            if( leftright != 0 )
            {
                bool left = leftright < 0;
                switch( state.current )
                {
                    case ITEM_MODE: {
                        cvar::rt_framegen = false;
                        if( cvar::rt_upscale_dlss > 0 && cvar::rt_available_dlss2 )
                        {
                            const int preset      = *cvar::rt_upscale_dlss;
                            cvar::rt_upscale_dlss = 0;
                            cvar::rt_upscale_fsr2 = preset;
                        }
                        else if( cvar::rt_upscale_fsr2 > 0 && cvar::rt_available_fsr2 )
                        {
                            const int preset      = *cvar::rt_upscale_fsr2;
                            cvar::rt_upscale_dlss = preset;
                            cvar::rt_upscale_fsr2 = 0;
                        }
                        else
                        {
                            cvar::rt_upscale_dlss = cvar::rt_available_dlss2 ? 2 : 0;
                            cvar::rt_upscale_fsr2 = cvar::rt_available_dlss2 ? 0 : 2;
                        }
                        return true;
                    }
                    case ITEM_PRESET: {
                        cvar::rt_framegen = false;
                        if( cvar::rt_available_dlss2 )
                        {
                            switch( *cvar::rt_upscale_dlss )
                            {
                                case 0: break;
                                case 4: cvar::rt_upscale_dlss = left ? 4 : 3; break;
                                case 3: cvar::rt_upscale_dlss = left ? 4 : 2; break;
                                case 2: cvar::rt_upscale_dlss = left ? 3 : 1; break;
                                case 1: cvar::rt_upscale_dlss = left ? 2 : 6; break;
                                case 6: cvar::rt_upscale_dlss = left ? 1 : 6; break;
                                default: cvar::rt_upscale_dlss = 2; break;
                            }
                        }
                        if( cvar::rt_available_fsr2 )
                        {
                            switch( *cvar::rt_upscale_fsr2 )
                            {
                                case 0: break;
                                case 4: cvar::rt_upscale_fsr2 = left ? 4 : 3; break;
                                case 3: cvar::rt_upscale_fsr2 = left ? 4 : 2; break;
                                case 2: cvar::rt_upscale_fsr2 = left ? 3 : 1; break;
                                case 1: cvar::rt_upscale_fsr2 = left ? 2 : 6; break;
                                case 6: cvar::rt_upscale_fsr2 = left ? 1 : 6; break;
                                default: cvar::rt_upscale_fsr2 = 2; break;
                            }
                        }
                        return true;
                    }
                    default: break;
                }
            }

            if( leftright != 0 || enter )
            {
                switch( state.current )
                {
                    case ITEM_VSYNC: {
                        if( !vsync_forced() )
                        {
                            cvar::rt_vsync = !bool( cvar::rt_vsync );
                            // hack
                            cvar::rt_framegen = false;
                        }
                        return true;
                    }
                    case ITEM_HDR: {
                        cvar::rt_hdr = !bool( cvar::rt_hdr );
                        return true;
                    }
                    case ITEM_FRAMEGEN: {
                        if( ( cvar::rt_upscale_dlss > 0 && cvar::rt_available_dlss3fg ) ||
                            ( cvar::rt_upscale_fsr2 > 0 && cvar::rt_available_fsr3fg ) )
                        {
                            cvar::rt_framegen = !bool( cvar::rt_framegen );
                        }
                        return true;
                    }
                    default: break;
                }
            }
        }
        return false;
    }

    bool tick( state_t& state )
    {
        if( state.finished )
        {
            g_noinput_onstart = false;

#if RT_HOOK_INTRO
            // after settings, get to the intro
            if( cvar::rt_firststart )
            {
                if( !state.introstate )
                {
                    state.introstate = intro::state_t{};
                    intro::start( *state.introstate );
                }

                if( !intro::tick( *state.introstate ) )
                {
                    return false;
                }
                else
                {
                    intro::destroy( *state.introstate );
                }
            }
            g_rt_showfirststartscene = false;
#endif

            g_rt_cutscenename = nullptr;

            // dont show this screen on the next starts
            RT_FirstStartDone();
            // end cutscene
            return true;
        }
        return false;
    }

    int blink_timer( double intervalOn = 0.6, double intervalOff = 0.2 )
    {
        double curtime = RT_GetCurrentTime();

        double i = std::fmod( curtime, intervalOn + intervalOff );
        return i <= intervalOn ? 1 : 0;
    }

    auto new_camera_rotation( const state_t& state, double dt, double mousex, double mousey ) -> DRotator
    {
        DAngle deltas[ 2 ] = {
            DAngle::fromDeg( 0 ),
            DAngle::fromDeg( 0 ),
        };

        if( state.cameraActive )
        {
            deltas[ 0 ] = DAngle::fromDeg( -mousex * dt );
            deltas[ 1 ] = DAngle::fromDeg( -mousey * dt );
        }
        else
        {
            deltas[ 0 ] =
                deltaangle( state.cameraRotation.Yaw, DefaultCameraRotation.Yaw ) * dt;
            deltas[ 1 ] =
                deltaangle( state.cameraRotation.Pitch, DefaultCameraRotation.Pitch ) * dt;
        }

        DRotator newr;
        {
            newr.Yaw = ( state.cameraRotation.Yaw + deltas[ 0 ] ).Normalized180();
            newr.Pitch = clamp( ( state.cameraRotation.Pitch + deltas[ 1 ] ).Normalized180(),
                                DAngle::fromDeg( -89 ),
                                DAngle::fromDeg( 89 ) );
            newr.Roll = nullAngle;
        }
        return newr;
    }

    bool is_camera_out( const state_t& state )
    {
        if( state.cameraActiveZoom || !state.cameraActive )
        {
            return false;
        }

        // if looked outside before, and still holding mouse
        if( state.cameraOut )
        {
            return true;
        }

        // if looking outside
        return DVector3{ state.cameraRotation }.dot( DVector3{ DefaultCameraRotation } ) < 0.8;
    }

    auto new_camera_zoom( const state_t& state, double dt ) -> float
    {
        dt *= 2;

        double target = state.cameraActiveZoom //
                            ? 1.0
                            : state.cameraOut //
                                  ? -1.0
                                  : 0.0;

        double z = std::lerp( double( state.cameraZoom ), target, dt );
        return std::clamp( float( z ), -1.f, 1.f );
    }

    void update_camera( state_t& state )
    {
        double dt;
        {
            static double prevtime = RT_GetCurrentTime();

            double curtime = RT_GetCurrentTime();
            dt             = curtime - prevtime;
            prevtime       = curtime;
        }

        double mousex, mousey;
        {
            mousex       = g_rt_mouse_x;
            mousey       = g_rt_mouse_y;
            g_rt_mouse_x = 0;
            g_rt_mouse_y = 0;
        }

        state.cameraOut      = is_camera_out( state );
        state.cameraZoom     = new_camera_zoom( state, dt );
        state.cameraRotation = new_camera_rotation( state, dt, mousex, mousey );

        float fovy;
        if( state.cameraZoom >= 0 )
        {
            fovy = std::lerp( 30.f, 10.f, state.cameraZoom );
        }
        else
        {
            fovy = std::lerp( 30.f, 70.f, -state.cameraZoom );
        }

        RT_ForceCamera( state.cameraPosition, state.cameraRotation, fovy );
    }

    void draw( state_t& state )
    {
#if RT_HOOK_INTRO
        if( state.introstate )
        {
            intro::draw( *state.introstate );
            return;
        }
#endif

        update_camera( state );

        constexpr int canvas_height = 480;
        constexpr int canvas_width  = int( canvas_height * 4.0 / 3.0 );

        auto* font = NewConsoleFont;

        constexpr int xsafe = 4;
        constexpr int ysafe = 4;

        auto l_width = [ font ]( const char* s ) {
            return font->StringWidth( s );
        };
        const int text_height = font->GetHeight();


        enum l_drawtext_enum
        {
            L_DRAWTEXT_DEFAULT,
            L_DRAWTEXT_SELECTED,
            L_DRAWTEXT_ERROR,
        };

        auto l_drawtext = [ font ]( l_drawtext_enum e,
                                    double          x,
                                    double          y,
                                    const char*     string,
                                    bool            gray = false ) {
            auto rgb = std::array< uint8_t, 3 >{};
            if( gray )
            {
                switch( e )
                {
                    case L_DRAWTEXT_ERROR: rgb = { 80, 30, 30 }; break;
                    default: rgb = { 80, 80, 80 }; break;
                }
            }
            else
            {
                switch( e )
                {
                    case L_DRAWTEXT_SELECTED: rgb = { 17, 122, 0 }; break;
                    case L_DRAWTEXT_ERROR: rgb = { 240, 0, 0 }; break;
                    case L_DRAWTEXT_DEFAULT:
                    default: rgb = { 0, 0, 220 }; break;
                }
            }

            {
                double xr = x - xsafe * 0.5;
                double yr = y - ysafe * 0.5;
                double w  = font->StringWidth( string ) + xsafe;
                double h  = font->GetHeight() + ysafe;

                VirtualToRealCoords( twod, xr, yr, w, h, canvas_width, canvas_height );

                ClearRect( twod, //
                           xr,
                           yr,
                           xr + w,
                           yr + h,
                           -1,
                           ( int( rgb[ 0 ] ) << 16 | int( rgb[ 1 ] ) << 8 | int( rgb[ 2 ] ) ) );
            }

            DrawText( twod,
                      font,
                      CR_WHITE,
                      x,
                      y,
                      string,
                      DTA_VirtualWidth,
                      canvas_width,
                      DTA_VirtualHeight,
                      canvas_height,
                      true,
                      DTA_KeepRatio,
                      false,
                      TAG_DONE );
        };

        auto l_drawimage = []( FTextureID texture, double x, double y ) {
            DrawTexture( twod, //
                         texture,
                         false,
                         x,
                         y,
                         DTA_VirtualWidth,
                         canvas_width,
                         DTA_VirtualHeight,
                         canvas_height,
                         true,
                         DTA_KeepRatio,
                         false,
                         TAG_DONE );
        };

        auto l_getmode = []() -> const char* {
            if( cvar::rt_upscale_dlss > 0 && cvar::rt_available_dlss2 )
            {
                return cvar::rt_framegen && cvar::rt_available_dlss3fg //
                           ? "DLSS 3"
                           : "DLSS 2";
            }
            if( cvar::rt_upscale_fsr2 > 0 && cvar::rt_available_fsr2 )
            {
                return cvar::rt_framegen && cvar::rt_available_fsr3fg //
                           ? "FSR 3"
                           : "FSR 2";
            }
            return "Custom";
        };

        auto l_getpreset = []() -> const char* {
            if( cvar::rt_upscale_dlss > 0 && cvar::rt_available_dlss2 )
            {
                switch( *cvar::rt_upscale_dlss )
                {
                    case 6: return "DLAA";
                    case 1: return "QUALITY";
                    case 2: return "BALANCED";
                    case 3: return "PERFORMANCE";
                    case 4: return "ULTRA PERFORMANCE";
                    default: return "CUSTOM";
                }
            }
            if( cvar::rt_upscale_fsr2 > 0 && cvar::rt_available_fsr2 )
            {
                switch( *cvar::rt_upscale_fsr2 )
                {
                    case 1: return "QUALITY";
                    case 2: return "BALANCED";
                    case 3: return "PERFORMANCE";
                    case 4: return "ULTRA PERFORMANCE";
                    case 6: return "NATIVE";
                    default: return "CUSTOM";
                }
            }
            return "UNAVAILABLE";
        };

        auto l_getvsync = []() -> const char* {
            if( vsync_forced() )
            {
                return "FORCED BY DLSS 3";
            }
            return cvar::rt_vsync ? "ON" : "OFF";
        };

        auto l_gethdr = []() -> const char* {
            return cvar::rt_hdr && cvar::rt_hdr_available ? "ON" : "OFF";
        };

        auto l_available_framegen = []() -> bool {
            if( cvar::rt_upscale_dlss > 0 )
            {
                return cvar::rt_available_dlss3fg;
            }
            if( cvar::rt_upscale_fsr2 > 0 )
            {
                return cvar::rt_available_fsr3fg;
            }
            return false;
        };

        auto l_getframegen = []() -> const char* {
            if( cvar::rt_framegen )
            {
                if( cvar::rt_upscale_dlss > 0 )
                {
                    return cvar::rt_available_dlss3fg ? "ON" : "UNAVAILABLE";
                }
                if( cvar::rt_upscale_fsr2 > 0 )
                {
                    return cvar::rt_available_fsr3fg ? "ON" : "UNAVAILABLE";
                }
            }
            return "OFF";
        };

        auto pagetable = std::vector< std::tuple< item_t, const char*, const char* > >{};
        switch( state.page )
        {
            case 0:
                break;
            case 1:
                pagetable.emplace_back( ITEM_MODE, "Mode", l_getmode() );
                pagetable.emplace_back( ITEM_PRESET, "Preset", l_getpreset() );
                pagetable.emplace_back( ITEM_FRAMEGEN, "Frame Generation", l_getframegen() );
                pagetable.emplace_back( ITEM_VSYNC, "VSync", l_getvsync() );
                pagetable.emplace_back( ITEM_PAGE1_ACCEPT, "Apply", nullptr );
                break;
            case 2:
                pagetable.emplace_back( ITEM_HDR, "HDR", l_gethdr() );
                pagetable.emplace_back( ITEM_PAGE2_ACCEPT, "Apply", nullptr );
                break;
            default: assert( 0 ); break;
        }

        const char* selector       = "*";
        const auto  selector_width = l_width( selector );

        constexpr int xcenter = canvas_width / 2;
        constexpr int yoffset = 25;

        int y = canvas_height - ( yoffset + int( pagetable.size() ) * ( text_height + ysafe ) );

        for( const auto& [ item, key, value ] : pagetable )
        {
            const bool isselected = ( state.current == item );

            const bool available = ( item == ITEM_FRAMEGEN ? l_available_framegen()
                                     : item == ITEM_VSYNC  ? !vsync_forced()
                                                           : true );

            if( value )
            {
                l_drawtext( isselected ? L_DRAWTEXT_SELECTED : L_DRAWTEXT_DEFAULT,
                            xcenter - selector_width / 2.0 - xsafe - l_width( key ),
                            y,
                            key,
                            !available );
                if( isselected )
                {
                    l_drawtext( L_DRAWTEXT_SELECTED,
                                xcenter - selector_width / 2.0,
                                y,
                                selector,
                                !available );
                }
                l_drawtext( isselected ? L_DRAWTEXT_SELECTED : L_DRAWTEXT_DEFAULT,
                            xcenter + selector_width / 2.0 + xsafe,
                            y,
                            value,
                            !available );
            }
            else
            {
                // offset for "Apply"
                y += ysafe * 2;
                l_drawtext( isselected ? L_DRAWTEXT_SELECTED : L_DRAWTEXT_DEFAULT, //
                            xcenter - l_width( key ) / 2.0,
                            y,
                            key );
            }

            y += text_height + ysafe;
        }

        const auto curTime = RT_GetCurrentTime();

        auto say = [ & ]( const char* text, l_drawtext_enum color = L_DRAWTEXT_DEFAULT ) {
            if( text && text[ 0 ] != '\0' )
            {
                l_drawtext( color, xcenter - l_width( text ) / 2.0, y, text );
            }
            return text_height + ysafe;
        };

        if( state.page != 0 )
        {
            y = yoffset;

            // vram
            {
                bool ok;
                char vram_buf[ 64 ];
                snprintf( vram_buf, std::size( vram_buf ), "%s VRAM", RT_GetVramUsage( &ok ) );
                vram_buf[ std::size( vram_buf ) - 1 ] = '\0';

                l_drawtext_enum color = L_DRAWTEXT_DEFAULT;
                if( !ok )
                {
                    if( static_cast< size_t >( curTime * 2.0 ) % 2 == 0 )
                    {
                        color = L_DRAWTEXT_ERROR;
                    }
                }

                y += say( vram_buf, color );
            }

            // delay a bit messages to not overflow the user
            const bool ok1  = state.page1StartTime && curTime > state.page1StartTime.value() + 2.0;
            const bool ok1c = state.page1StartTime && curTime < state.page1StartTime.value() + 9.0;

            const bool ok2  = state.page1StartTime && curTime > state.page1StartTime.value() + 14.0;
            const bool ok2c =
                false; // state.page1StartTime && curTime < state.page1StartTime.value() + 8.0;

            // latency (only on the page with perf.settings)
            if( state.page == 1 && ok1 )
            {
                auto color =
                    ok1c && blink_timer( 0.5, 0.5 ) ? L_DRAWTEXT_SELECTED : L_DRAWTEXT_DEFAULT;

                y += text_height;
                y += say( "Use ARROWS to adjust settings. Camera movement should feel smooth.", color );
                if( g_rt_showfirststartscene_untiemouse )
                {
                    y += say( blink_timer() ? "Press any KEYBOARD KEY to ACTIVATE MOUSE." : "" );
                }
                else
                {
                    y += say( ok1c || blink_timer( 3.0, 2.0 )
                                  ? "Hold \'Left Mouse Button\' to control camera."
                                  : "Hold \'Right Mouse Button\' to zoom.",
                              color );
                }
            }

            // description
            if( ok2 )
            {
                auto color =
                    ok2c && blink_timer( 0.5, 0.5 ) ? L_DRAWTEXT_SELECTED : L_DRAWTEXT_DEFAULT;

                y += text_height;
                y += say( "Press \'F\' for the setting explanation.", color );
            }

            if( state.showDescription )
            {
                Dim( twod, 0x2F2F2F, 0.9f, 0, 0, twod->GetWidth(), twod->GetHeight() );

                auto rtkey = std::string_view{};
                switch( *state.showDescription )
                {
                    case ITEM_MODE: rtkey = "RTMNU_MODE"; break;
                    case ITEM_PRESET: rtkey = "RTMNU_PRESET"; break;
                    case ITEM_FRAMEGEN: rtkey = "RTMNU_FRAMEGEN"; break;
                    case ITEM_VSYNC: rtkey = "RTMNU_VSYNC"; break;
                    case ITEM_HDR: rtkey = "RTMNU_HDR"; break;
                    case ITEM_PAGE0_PRESSANYKEY:
                    case ITEM_PAGE1_ACCEPT:
                    case ITEM_PAGE2_ACCEPT:
                    default: break;
                }

                RT_DrawSettingDescription( rtkey, true );
            }
        }
        else
        {
            y = int( yoffset * 3.9f );

            // draw text after fading completed
            if( state.fadeinWas && curTime > state.fadeinStartTime + 3.0 )
            {
                y += say( blink_timer( 2.75, 0.25 ) ? "Press ANY key to continue" : "" );
            }
        }

        {
            constexpr auto fadeinDelay    = 0.5;
            constexpr auto fadeinDuration = 2.5;

            if( !state.fadeinWas )
            {
                state.fadeinWas       = true;
                state.fadeinStartTime = curTime + fadeinDelay;
            }

            const auto progress =
                std::clamp( curTime - state.fadeinStartTime, 0.0, fadeinDuration ) / fadeinDuration;

            if( progress < 0.999 )
            {
                float t = 1.0f - float( progress );
                t       = std::sqrt( t ); // slow down a bit

                twod->AddColorOnlyQuad( 0, //
                                        0,
                                        twod->GetWidth(),
                                        twod->GetHeight(),
                                        MAKEARGB( std::clamp< int >( 255 * t, 0, 255 ), 0, 0, 0 ) );
            }

            if( progress > 0.2 )
            {
                g_noinput_onstart = false;
            }
        }
    }
}
}

MAKE_VM_WRAPPER( DCutsceneFirstStart_Controller_RT, firststart::state_t )


// -------------- //
