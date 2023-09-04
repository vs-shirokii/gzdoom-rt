#pragma once

#if HAVE_RT

struct sector_t;
struct seg_t;

void RT_BakeExportables( const std::vector< bool >& animatedTexnums );
bool RT_IsSectorExportable( const sector_t* sector, bool ceiling );
bool RT_IsSectorExportable2( int sectornum, bool ceiling );
bool RT_IsWallExportable( const seg_t* seg );

void RT_RequestMelt();
bool RT_IsMeltActive();
bool RT_IgnoreUserInput();

auto RT_GetCurrentTime() -> double;
auto RT_GetVramUsage( bool* ok = nullptr ) -> const char*;

#endif
