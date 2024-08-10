#define STB_IMAGE_IMPLEMENTATION
#include "C:\Dev\gzdoom\src\common\thirdparty\stb\stb_image.h"

#define WIN32_LEAN_AND_MEAN
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>

#include <filesystem>
#include <string>

static void SetWindowRounding( HWND hwnd, bool rounded )
{
    static HMODULE dwm = LoadLibrary( L"dwmapi.dll" );
    if( !dwm )
    {
        return;
    }

    static auto dwmSetWindowAttribute = reinterpret_cast< decltype( &DwmSetWindowAttribute ) >(
        GetProcAddress( dwm, "DwmSetWindowAttribute" ) );
    if( !dwmSetWindowAttribute )
    {
        return;
    }

    constexpr uint32_t dwmwa_window_corner_preference = 33;

    const uint32_t dwmwcp_round = rounded ? 2  // DWM_WINDOW_CORNER_PREFERENCE::DWMWCP_ROUND
                                          : 1; // DWM_WINDOW_CORNER_PREFERENCE::DWMWCP_DONOTROUND

    dwmSetWindowAttribute(
        hwnd, dwmwa_window_corner_preference, &dwmwcp_round, sizeof( dwmwcp_round ) );
}

void ShowWarning( const char* msg )
{
    MessageBoxA( nullptr, msg, "Warning - Remix Launcher", MB_ICONEXCLAMATION | MB_OK );
}

struct img_t
{
    uint8_t* data;
    int      w;
    int      h;
};

static auto loadimg( const char* path ) -> img_t
{
    int  x, y, channels;
    auto img = stbi_load( path, &x, &y, &channels, 4 );
    if( !img || x <= 0 || y <= 0 )
    {
        return {};
    }
    // rgb -> bgr for winapi
    for( int i = 0; i < x; i++ )
    {
        for( int j = 0; j < y; j++ )
        {
            int pix = i * y + j;
            std::swap( img[ 4 * pix + 0 ], img[ 4 * pix + 2 ] );
        }
    }
    return {
        .data = img,
        .w    = x,
        .h    = y,
    };
}

auto drawbackground( HDC hdc, const img_t& img )
{
    if( !img.data )
    {
        return;
    }

    BITMAPINFO bmi              = {};
    bmi.bmiHeader.biSize        = sizeof( BITMAPINFOHEADER );
    bmi.bmiHeader.biWidth       = img.w;
    bmi.bmiHeader.biHeight      = -img.h; // Negative height to indicate top-down image
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32; // 32 bits per pixel (RGBA)
    bmi.bmiHeader.biCompression = BI_RGB;

    SetDIBitsToDevice( hdc, // Destination device context
                       0,
                       0, // Destination position (x, y)
                       img.w,
                       img.h, // Width and height of the destination rectangle
                       0,
                       0,             // Source position (x, y)
                       0,             // Start scan line
                       img.h,         // Number of scan lines
                       img.data,      // Pointer to image data (RGBA uint8)
                       &bmi,          // Pointer to the BITMAPINFO structure
                       DIB_RGB_COLORS // Color usage
    );
};

int wnd_size_x = 0;
int wnd_size_y = 0;

img_t lnch_none{};
img_t lnch_rr{};
img_t lnch_nrd{};
img_t lnch_exit{};

HWND hwnd_button_rr  = nullptr;
HWND hwnd_button_nrd = nullptr;

enum launcherresult_e
{
    LAUNCHERRESULT_NONE,
    LAUNCHERRESULT_EXIT,
    LAUNCHERRESULT_RR,
    LAUNCHERRESULT_NRD,
};
launcherresult_e g_result{ LAUNCHERRESULT_NONE };

#define BUTTON_IDB_RR  0
#define BUTTON_IDB_NRD 1

launcherresult_e cursor_over( POINT p, POINT p_local )
{
    RECT rect_exit_local = {
        .left   = int( wnd_size_x * 0.9f ),
        .top    = 0,
        .right  = wnd_size_x,
        .bottom = int( wnd_size_x * 0.1f ),
    };

    RECT rect_1{}, rect_2{};
    GetWindowRect( hwnd_button_rr, &rect_1 );
    GetWindowRect( hwnd_button_nrd, &rect_2 );

    if( PtInRect( &rect_1, p ) )
    {
        return LAUNCHERRESULT_RR;
    }
    if( PtInRect( &rect_2, p ) )
    {
        return LAUNCHERRESULT_NRD;
    }
    if( PtInRect( &rect_exit_local, p_local ) )
    {
        return LAUNCHERRESULT_EXIT;
    }
    return LAUNCHERRESULT_NONE;
}

LRESULT CALLBACK WndProc( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    float y0 = ( 3 * 0.13f + 0.01f );
    float y1 = ( 2 * 0.13f );
    float y2 = ( 1 * 0.13f - 0.01f );

    switch( message )
    {
        case WM_CREATE: {
            hwnd_button_rr =
                CreateWindow( L"BUTTON", // predefined class
                              L"rr",
                              WS_TABSTOP | WS_CHILD | BS_DEFPUSHBUTTON, // | WS_VISIBLE,
                              int( 0.1f * wnd_size_x ),
                              int( wnd_size_y - y0 * wnd_size_y ),
                              int( 0.8f * wnd_size_x ),
                              int( ( y0 - y1 ) * wnd_size_y ),
                              hwnd,
                              ( HMENU )BUTTON_IDB_RR,
                              GetModuleHandle( NULL ),
                              NULL );
            hwnd_button_nrd =
                CreateWindow( L"BUTTON", // predefined class
                              L"nrd",
                              WS_TABSTOP | WS_CHILD | BS_DEFPUSHBUTTON, // | WS_VISIBLE,
                              int( 0.1f * wnd_size_x ),
                              int( wnd_size_y - y1 * wnd_size_y ),
                              int( 0.8f * wnd_size_x ),
                              int( ( y1 - y2 ) * wnd_size_y ),
                              hwnd,
                              ( HMENU )BUTTON_IDB_NRD,
                              GetModuleHandle( NULL ),
                              NULL );
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC         hdcPaint = BeginPaint( hwnd, &ps );

            // double buffering to prevent flickering
            HDC     hdc     = CreateCompatibleDC( hdcPaint );
            HBITMAP hBitmap = CreateCompatibleBitmap( hdcPaint, wnd_size_x, wnd_size_y );
            SelectObject( hdc, hBitmap );

            auto imgat = [ & ]() -> const img_t& {
                POINT p;
                GetCursorPos( &p );
                POINT p_local = p;
                ScreenToClient( hwnd, &p_local );

                switch( cursor_over( p, p_local ) )
                {
                    case LAUNCHERRESULT_RR: return lnch_rr;
                    case LAUNCHERRESULT_NRD: return lnch_nrd;
                    case LAUNCHERRESULT_EXIT: return lnch_exit;
                    default: return lnch_none;
                }
            };

            drawbackground( hdc, imgat() );

            BitBlt( hdcPaint, 0, 0, wnd_size_x, wnd_size_y, hdc, 0, 0, SRCCOPY );
            DeleteObject( hBitmap );
            DeleteDC( hdc );

            EndPaint( hwnd, &ps );

            return 0;
        }

        case WM_MOUSEMOVE: {
            HCURSOR hCursor = LoadCursor( nullptr, IDC_ARROW );
            SetCursor( hCursor );
            break;
        }

        case WM_LBUTTONDOWN: {
            POINT p_local = { GET_X_LPARAM( lParam ), GET_Y_LPARAM( lParam ) };
            POINT p       = p_local;
            ClientToScreen( hwnd, &p );

            auto r = cursor_over( p, p_local );
            switch( r )
            {
                case LAUNCHERRESULT_RR:
                case LAUNCHERRESULT_NRD: g_result = r; break;
                case LAUNCHERRESULT_EXIT: PostMessage( hwnd, WM_CLOSE, 0, 0 ); break;
                default: break;
            }

            return 0;
        }

        case WM_KEYDOWN: {
            if( wParam == VK_ESCAPE )
            {
                PostMessage( hwnd, WM_CLOSE, 0, 0 );
                return 0;
            }
            break;
        }
        case WM_CLOSE:
        case WM_DESTROY: {
            if( g_result == LAUNCHERRESULT_NONE )
            {
                g_result = LAUNCHERRESULT_EXIT;
            }
            return 0;
        }

        default: break;
    }

    return DefWindowProc( hwnd, message, wParam, lParam );
}

int WINAPI WinMain( HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR args, int nCmdShow )
{
    if( !std::filesystem::exists( "gzdoom.exe" ) )
    {
        ShowWarning( "Can't find gzdoom.exe" );
        return -1;
    }

    const wchar_t* winname = L"Choose Doom";

    WNDCLASS wc = {
        .lpfnWndProc   = WndProc,
        .hInstance     = GetModuleHandle( nullptr ),
        .lpszClassName = winname,
    };

    if( !RegisterClass( &wc ) )
    {
        ShowWarning( "RegisterClass fail" );
        return -1;
    }

    lnch_none = loadimg( "rt\\launcher\\lnch_none.png" );
    lnch_rr   = loadimg( "rt\\launcher\\lnch_rr.png" );
    lnch_nrd  = loadimg( "rt\\launcher\\lnch_nrd.png" );
    lnch_exit = loadimg( "rt\\launcher\\lnch_exit.png" );

    if( !lnch_none.data || !lnch_rr.data || !lnch_nrd.data || !lnch_exit.data )
    {
        ShowWarning( "Image load fail" );
        return -1;
    }

    wnd_size_x = lnch_none.w;
    wnd_size_y = lnch_none.h;

    int screen_size_y = GetSystemMetrics( SM_CYSCREEN );
    int screen_size_x = GetSystemMetrics( SM_CXSCREEN );

    HWND hwnd = CreateWindowEx( WS_EX_APPWINDOW,
                                winname,
                                winname,
                                WS_OVERLAPPED,
                                ( screen_size_x - wnd_size_x ) / 2,
                                ( screen_size_y - wnd_size_y ) / 2,
                                wnd_size_x,
                                wnd_size_y,
                                NULL,
                                NULL,
                                GetModuleHandle( nullptr ),
                                NULL );
    if( !hwnd )
    {
        ShowWarning( "CreateWindowEx fail" );
        return -1;
    }

    // borderless
    {
        LONG lStyle = GetWindowLong( hwnd, GWL_STYLE );
        lStyle &= ~( WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU );
        SetWindowLong( hwnd, GWL_STYLE, lStyle );

        LONG lExStyle = GetWindowLong( hwnd, GWL_EXSTYLE );
        lExStyle &= ~( WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE );
        SetWindowLong( hwnd, GWL_EXSTYLE, lExStyle );

        SetWindowPos(
            hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED );
        SetWindowRounding( hwnd, true );
    }

    ShowWindow( hwnd, SW_SHOW );

    {
        MSG msg = {};
        while( GetMessage( &msg, NULL, 0, 0 ) )
        {
            TranslateMessage( &msg );
            DispatchMessage( &msg );

            if( g_result != LAUNCHERRESULT_NONE )
            {
                break;
            }

            RedrawWindow( hwnd, NULL, NULL, RDW_INVALIDATE );
        }
        DestroyWindow( hwnd );
        UnregisterClass( winname, GetModuleHandle( NULL ) );
    }

    {
        if( lnch_none.data )
        {
            stbi_image_free( lnch_none.data );
        }
        if( lnch_rr.data )
        {
            stbi_image_free( lnch_rr.data );
        }
        if( lnch_nrd.data )
        {
            stbi_image_free( lnch_nrd.data );
        }
        if( lnch_exit.data )
        {
            stbi_image_free( lnch_exit.data );
        }
    }

    if( g_result == LAUNCHERRESULT_EXIT )
    {
        return 0;
    }

    if( g_result != LAUNCHERRESULT_NRD && g_result != LAUNCHERRESULT_RR )
    {
        return -2;
    }

    bool forcedoom2 = true;
    {
        if( args && strstr( args, "-notdoom2" ) )
        {
            forcedoom2 = false;
        }
    }

    std::wstring cmd;
    {
        const wchar_t* gzdoom   = L"gzdoom.exe";
        const wchar_t* remix    = L"-rtxremix";
        const wchar_t* denoiser = ( g_result == LAUNCHERRESULT_RR )    ? L"+rt_remix_rayreconstr 1"
                                  : ( g_result == LAUNCHERRESULT_NRD ) ? L"+rt_remix_rayreconstr 0"
                                                                       : L"";

        cmd = std::wstring{ gzdoom } + L" " + remix + L" " + denoiser;

        if( forcedoom2 )
        {
            cmd += L" -rtdoom2";
        }
    }

    {
        STARTUPINFO         si = { .cb = sizeof( si ) };
        PROCESS_INFORMATION pi = {};

        if( !CreateProcess( NULL, // No module name (use command line)
                            const_cast< wchar_t* >( cmd.c_str() ), // Command line
                            NULL,                                  // Process handle not inheritable
                            NULL,                                  // Thread handle not inheritable
                            FALSE,                    // Set handle inheritance to FALSE
                            CREATE_NEW_PROCESS_GROUP, // -- do not wait for process
                            NULL,                     // Use parent's environment block
                            NULL,                     // Use parent's starting directory
                            &si,                      // Pointer to STARTUPINFO structure
                            &pi )                     // Pointer to PROCESS_INFORMATION structure
        )
        {
            ShowWarning( "CreateProcess fail" );
            return -1;
        }

        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
    }

    return 0;
}
