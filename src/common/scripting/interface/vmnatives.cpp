/*
** vmnatives.cpp
**
** VM exports for engine backend classes
**
**---------------------------------------------------------------------------
** Copyright 2005-2020 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
**
*/ 


#include "texturemanager.h"
#include "filesystem.h"
#include "c_console.h"
#include "c_cvars.h"
#include "c_bind.h"
#include "c_dispatch.h"

#include "menu.h"
#include "vm.h"
#include "gstrings.h"
#include "printf.h"
#include "s_music.h"
#include "i_interface.h"
#include "base_sbar.h"
#include "image.h"
#include "s_soundinternal.h"
#include "i_time.h"

#include "maps.h"
#include "types.h"

static ZSMap<FName, DObject*> AllServices;

static void MarkServices()
{
	ZSMap<FName, DObject*>::Iterator it(AllServices);
	ZSMap<FName, DObject*>::Pair* pair;
	while (it.NextPair(pair))
	{
		GC::Mark<DObject>(pair->Value);
	}
}

void InitServices()
{
	PClass* cls = PClass::FindClass("Service");
	for (PClass* clss : PClass::AllClasses)
	{
		if (clss != cls && cls->IsAncestorOf(clss))
		{
			DObject* obj = clss->CreateNew();
			obj->ObjectFlags |= OF_Transient;
			AllServices.Insert(clss->TypeName, obj);
		}
	}
	GC::AddMarkerFunc(&MarkServices);
}

void ClearServices()
{
	AllServices.Clear();
}



//==========================================================================
//
// status bar exports
//
//==========================================================================

static double StatusbarToRealCoords(DStatusBarCore* self, double x, double y, double w, double h, double* py, double* pw, double* ph)
{
	self->StatusbarToRealCoords(x, y, w, h);
	*py = y;
	*pw = w;
	*ph = h;
	return x;
}

DEFINE_ACTION_FUNCTION_NATIVE(DStatusBarCore, StatusbarToRealCoords, StatusbarToRealCoords)
{
	PARAM_SELF_PROLOGUE(DStatusBarCore);
	PARAM_FLOAT(x);
	PARAM_FLOAT(y);
	PARAM_FLOAT(w);
	PARAM_FLOAT(h);
	self->StatusbarToRealCoords(x, y, w, h);
	if (numret > 0) ret[0].SetFloat(x);
	if (numret > 1) ret[1].SetFloat(y);
	if (numret > 2) ret[2].SetFloat(w);
	if (numret > 3) ret[3].SetFloat(h);
	return min(4, numret);
}

void SBar_DrawTexture(DStatusBarCore* self, int texid, double x, double y, int flags, double alpha, double w, double h, double scaleX, double scaleY, int style, int color, int translation, double clipwidth)
{
	if (!twod->HasBegun2D()) ThrowAbortException(X_OTHER, "Attempt to draw to screen outside a draw function");
	self->DrawGraphic(FSetTextureID(texid), x, y, flags, alpha, w, h, scaleX, scaleY, ERenderStyle(style), color, translation, clipwidth);
}

DEFINE_ACTION_FUNCTION_NATIVE(DStatusBarCore, DrawTexture, SBar_DrawTexture)
{
	PARAM_SELF_PROLOGUE(DStatusBarCore);
	PARAM_INT(texid);
	PARAM_FLOAT(x);
	PARAM_FLOAT(y);
	PARAM_INT(flags);
	PARAM_FLOAT(alpha);
	PARAM_FLOAT(w);
	PARAM_FLOAT(h);
	PARAM_FLOAT(scaleX);
	PARAM_FLOAT(scaleY);
	PARAM_INT(style);
	PARAM_INT(col);
	PARAM_INT(trans);
	PARAM_FLOAT(clipwidth);
	SBar_DrawTexture(self, texid, x, y, flags, alpha, w, h, scaleX, scaleY, style, col, trans, clipwidth);
	return 0;
}

void SBar_DrawImage(DStatusBarCore* self, const FString& texid, double x, double y, int flags, double alpha, double w, double h, double scaleX, double scaleY, int style, int color, int translation, double clipwidth)
{
	if (!twod->HasBegun2D()) ThrowAbortException(X_OTHER, "Attempt to draw to screen outside a draw function");
	self->DrawGraphic(TexMan.CheckForTexture(texid.GetChars(), ETextureType::Any), x, y, flags, alpha, w, h, scaleX, scaleY, ERenderStyle(style), color, translation, clipwidth);
}

DEFINE_ACTION_FUNCTION_NATIVE(DStatusBarCore, DrawImage, SBar_DrawImage)
{
	PARAM_SELF_PROLOGUE(DStatusBarCore);
	PARAM_STRING(texid);
	PARAM_FLOAT(x);
	PARAM_FLOAT(y);
	PARAM_INT(flags);
	PARAM_FLOAT(alpha);
	PARAM_FLOAT(w);
	PARAM_FLOAT(h);
	PARAM_FLOAT(scaleX);
	PARAM_FLOAT(scaleY);
	PARAM_INT(style);
	PARAM_INT(col);
	PARAM_INT(trans);
	PARAM_FLOAT(clipwidth);
	SBar_DrawImage(self, texid, x, y, flags, alpha, w, h, scaleX, scaleY, style, col, trans, clipwidth);
	return 0;
}

void SBar_DrawImageRotated(DStatusBarCore* self, const FString& texid, double x, double y, int flags, double angle, double alpha, double scaleX, double scaleY, int style, int color, int translation)
{
	if (!twod->HasBegun2D()) ThrowAbortException(X_OTHER, "Attempt to draw to screen outside a draw function");
	self->DrawRotated(TexMan.CheckForTexture(texid.GetChars(), ETextureType::Any), x, y, flags, angle, alpha, scaleX, scaleY, color, translation, (ERenderStyle)style);
}

DEFINE_ACTION_FUNCTION_NATIVE(DStatusBarCore, DrawImageRotated, SBar_DrawImageRotated)
{
	PARAM_SELF_PROLOGUE(DStatusBarCore);
	PARAM_STRING(texid);
	PARAM_FLOAT(x);
	PARAM_FLOAT(y);
	PARAM_INT(flags);
	PARAM_FLOAT(angle);
	PARAM_FLOAT(alpha);
	PARAM_FLOAT(scaleX);
	PARAM_FLOAT(scaleY);
	PARAM_INT(style);
	PARAM_INT(col);
	PARAM_INT(trans);
	SBar_DrawImageRotated(self, texid, x, y, flags, angle, alpha, scaleX, scaleY, style, col, trans);
	return 0;
}

void SBar_DrawTextureRotated(DStatusBarCore* self, int texid, double x, double y, int flags, double angle, double alpha, double scaleX, double scaleY, int style, int color, int translation)
{
	if (!twod->HasBegun2D()) ThrowAbortException(X_OTHER, "Attempt to draw to screen outside a draw function");
	self->DrawRotated(FSetTextureID(texid), x, y, flags, angle, alpha, scaleX, scaleY, color, translation, (ERenderStyle)style);
}

DEFINE_ACTION_FUNCTION_NATIVE(DStatusBarCore, DrawTextureRotated, SBar_DrawTextureRotated)
{
	PARAM_SELF_PROLOGUE(DStatusBarCore);
	PARAM_INT(texid);
	PARAM_FLOAT(x);
	PARAM_FLOAT(y);
	PARAM_INT(flags);
	PARAM_FLOAT(angle);
	PARAM_FLOAT(alpha);
	PARAM_FLOAT(scaleX);
	PARAM_FLOAT(scaleY);
	PARAM_INT(style);
	PARAM_INT(col);
	PARAM_INT(trans);
	SBar_DrawTextureRotated(self, texid, x, y, flags, angle, alpha, scaleX, scaleY, style, col, trans);
	return 0;
}


void SBar_DrawString(DStatusBarCore* self, DHUDFont* font, const FString& string, double x, double y, int flags, int trans, double alpha, int wrapwidth, int linespacing, double scaleX, double scaleY, int translation, int style);

DEFINE_ACTION_FUNCTION_NATIVE(DStatusBarCore, DrawString, SBar_DrawString)
{
	PARAM_SELF_PROLOGUE(DStatusBarCore);
	PARAM_POINTER_NOT_NULL(font, DHUDFont);
	PARAM_STRING(string);
	PARAM_FLOAT(x);
	PARAM_FLOAT(y);
	PARAM_INT(flags);
	PARAM_INT(trans);
	PARAM_FLOAT(alpha);
	PARAM_INT(wrapwidth);
	PARAM_INT(linespacing);
	PARAM_FLOAT(scaleX);
	PARAM_FLOAT(scaleY);
	PARAM_INT(pt);
	PARAM_INT(style);
	SBar_DrawString(self, font, string, x, y, flags, trans, alpha, wrapwidth, linespacing, scaleX, scaleY, pt, style);
	return 0;
}

static double SBar_TransformRect(DStatusBarCore* self, double x, double y, double w, double h, int flags, double* py, double* pw, double* ph)
{
	self->TransformRect(x, y, w, h, flags);
	*py = y;
	*pw = w;
	*ph = h;
	return x;
}

DEFINE_ACTION_FUNCTION_NATIVE(DStatusBarCore, TransformRect, SBar_TransformRect)
{
	PARAM_SELF_PROLOGUE(DStatusBarCore);
	PARAM_FLOAT(x);
	PARAM_FLOAT(y);
	PARAM_FLOAT(w);
	PARAM_FLOAT(h);
	PARAM_INT(flags);
	self->TransformRect(x, y, w, h, flags);
	if (numret > 0) ret[0].SetFloat(x);
	if (numret > 1) ret[1].SetFloat(y);
	if (numret > 2) ret[2].SetFloat(w);
	if (numret > 3) ret[3].SetFloat(h);
	return min(4, numret);
}

static void SBar_Fill(DStatusBarCore* self, int color, double x, double y, double w, double h, int flags)
{
	if (!twod->HasBegun2D()) ThrowAbortException(X_OTHER, "Attempt to draw to screen outside a draw function");
	self->Fill(color, x, y, w, h, flags);
}

DEFINE_ACTION_FUNCTION_NATIVE(DStatusBarCore, Fill, SBar_Fill)
{
	PARAM_SELF_PROLOGUE(DStatusBarCore);
	PARAM_COLOR(color);
	PARAM_FLOAT(x);
	PARAM_FLOAT(y);
	PARAM_FLOAT(w);
	PARAM_FLOAT(h);
	PARAM_INT(flags);
	SBar_Fill(self, color, x, y, w, h, flags);
	return 0;
}

static void SBar_SetClipRect(DStatusBarCore* self, double x, double y, double w, double h, int flags)
{
	self->SetClipRect(x, y, w, h, flags);
}

DEFINE_ACTION_FUNCTION_NATIVE(DStatusBarCore, SetClipRect, SBar_SetClipRect)
{
	PARAM_SELF_PROLOGUE(DStatusBarCore);
	PARAM_FLOAT(x);
	PARAM_FLOAT(y);
	PARAM_FLOAT(w);
	PARAM_FLOAT(h);
	PARAM_INT(flags);
	self->SetClipRect(x, y, w, h, flags);
	return 0;
}

void FormatNumber(int number, int minsize, int maxsize, int flags, const FString& prefix, FString* result);

DEFINE_ACTION_FUNCTION_NATIVE(DStatusBarCore, FormatNumber, FormatNumber)
{
	PARAM_PROLOGUE;
	PARAM_INT(number);
	PARAM_INT(minsize);
	PARAM_INT(maxsize);
	PARAM_INT(flags);
	PARAM_STRING(prefix);
	FString fmt;
	FormatNumber(number, minsize, maxsize, flags, prefix, &fmt);
	ACTION_RETURN_STRING(fmt);
}

static void SBar_SetSize(DStatusBarCore* self, int rt, int vw, int vh, int hvw, int hvh)
{
	self->SetSize(rt, vw, vh, hvw, hvh);
}

DEFINE_ACTION_FUNCTION_NATIVE(DStatusBarCore, SetSize, SBar_SetSize)
{
	PARAM_SELF_PROLOGUE(DStatusBarCore);
	PARAM_INT(rt);
	PARAM_INT(vw);
	PARAM_INT(vh);
	PARAM_INT(hvw);
	PARAM_INT(hvh);
	self->SetSize(rt, vw, vh, hvw, hvh);
	return 0;
}

static void SBar_GetHUDScale(DStatusBarCore* self, DVector2* result)
{
	*result = self->GetHUDScale();
}

DEFINE_ACTION_FUNCTION_NATIVE(DStatusBarCore, GetHUDScale, SBar_GetHUDScale)
{
	PARAM_SELF_PROLOGUE(DStatusBarCore);
	ACTION_RETURN_VEC2(self->GetHUDScale());
}

static void BeginStatusBar(DStatusBarCore* self, bool fs, int w, int h, int r)
{
	self->BeginStatusBar(w, h, r, fs);
}

DEFINE_ACTION_FUNCTION_NATIVE(DStatusBarCore, BeginStatusBar, BeginStatusBar)
{
	PARAM_SELF_PROLOGUE(DStatusBarCore);
	PARAM_BOOL(fs);
	PARAM_INT(w);
	PARAM_INT(h);
	PARAM_INT(r);
	self->BeginStatusBar(w, h, r, fs);
	return 0;
}

static void BeginHUD(DStatusBarCore* self, double a, bool fs, int w, int h)
{
	self->BeginHUD(w, h, a, fs);
}

DEFINE_ACTION_FUNCTION_NATIVE(DStatusBarCore, BeginHUD, BeginHUD)
{
	PARAM_SELF_PROLOGUE(DStatusBarCore);
	PARAM_FLOAT(a);
	PARAM_BOOL(fs);
	PARAM_INT(w);
	PARAM_INT(h);
	self->BeginHUD(w, h, a, fs);
	return 0;
}


//=====================================================================================
//
// 
//
//=====================================================================================

DHUDFont* CreateHudFont(FFont* fnt, int spac, int mono, int sx, int sy)
{
	return Create<DHUDFont>(fnt, spac, EMonospacing(mono), sy, sy);
}

DEFINE_ACTION_FUNCTION_NATIVE(DHUDFont, Create, CreateHudFont)
{
	PARAM_PROLOGUE;
	PARAM_POINTER(fnt, FFont);
	PARAM_INT(spac);
	PARAM_INT(mono);
	PARAM_INT(sx);
	PARAM_INT(sy);
	ACTION_RETURN_POINTER(CreateHudFont(fnt, spac, mono, sy, sy));
}




//==========================================================================
//
// texture manager exports
//
//==========================================================================

DEFINE_ACTION_FUNCTION(_TexMan, GetName)
{
	PARAM_PROLOGUE;
	PARAM_INT(texid);
	auto tex = TexMan.GameByIndex(texid);
	FString retval;

	if (tex != nullptr)
	{
		if (tex->GetName().IsNotEmpty()) retval = tex->GetName();
		else
		{
			// Textures for full path names do not have their own name, they merely link to the source lump.
			auto lump = tex->GetSourceLump();
			if (TexMan.GetLinkedTexture(lump) == tex)
				retval = fileSystem.GetFileFullName(lump);
		}
	}
	ACTION_RETURN_STRING(retval);
}

static int CheckForTexture(const FString& name, int type, int flags)
{
	return TexMan.CheckForTexture(name.GetChars(), static_cast<ETextureType>(type), flags).GetIndex();
}

DEFINE_ACTION_FUNCTION_NATIVE(_TexMan, CheckForTexture, CheckForTexture)
{
	PARAM_PROLOGUE;
	PARAM_STRING(name);
	PARAM_INT(type);
	PARAM_INT(flags);
	ACTION_RETURN_INT(CheckForTexture(name, type, flags));
}

//==========================================================================
//
//
//
//==========================================================================

static int GetTextureSize(int texid, int* py)
{
	auto tex = TexMan.GameByIndex(texid);
	int x, y;
	if (tex != nullptr)
	{
		x = int(0.5 + tex->GetDisplayWidth());
		y = int(0.5 + tex->GetDisplayHeight());
	}
	else x = y = -1;
	if (py) *py = y;
	return x;
}

DEFINE_ACTION_FUNCTION_NATIVE(_TexMan, GetSize, GetTextureSize)
{
	PARAM_PROLOGUE;
	PARAM_INT(texid);
	int x, y;
	x = GetTextureSize(texid, &y);
	if (numret > 0) ret[0].SetInt(x);
	if (numret > 1) ret[1].SetInt(y);
	return min(numret, 2);
}

//==========================================================================
//
//
//
//==========================================================================
static void GetScaledSize(int texid, DVector2* pvec)
{
	auto tex = TexMan.GameByIndex(texid);
	double x, y;
	if (tex != nullptr)
	{
		x = tex->GetDisplayWidth();
		y = tex->GetDisplayHeight();
	}
	else x = y = -1;
	if (pvec)
	{
		pvec->X = x;
		pvec->Y = y;
	}
}

DEFINE_ACTION_FUNCTION_NATIVE(_TexMan, GetScaledSize, GetScaledSize)
{
	PARAM_PROLOGUE;
	PARAM_INT(texid);
	DVector2 vec;
	GetScaledSize(texid, &vec);
	ACTION_RETURN_VEC2(vec);
}

//==========================================================================
//
//
//
//==========================================================================
static void GetScaledOffset(int texid, DVector2* pvec)
{
	auto tex = TexMan.GameByIndex(texid);
	double x, y;
	if (tex != nullptr)
	{
		x = tex->GetDisplayLeftOffset();
		y = tex->GetDisplayTopOffset();
	}
	else x = y = -1;
	if (pvec)
	{
		pvec->X = x;
		pvec->Y = y;
	}
}

DEFINE_ACTION_FUNCTION_NATIVE(_TexMan, GetScaledOffset, GetScaledOffset)
{
	PARAM_PROLOGUE;
	PARAM_INT(texid);
	DVector2 vec;
	GetScaledOffset(texid, &vec);
	ACTION_RETURN_VEC2(vec);
}

//==========================================================================
//
//
//
//==========================================================================

static int CheckRealHeight(int texid)
{
	auto tex = TexMan.GameByIndex(texid);
	if (tex != nullptr) return tex->CheckRealHeight();
	else return -1;
}

DEFINE_ACTION_FUNCTION_NATIVE(_TexMan, CheckRealHeight, CheckRealHeight)
{
	PARAM_PROLOGUE;
	PARAM_INT(texid);
	ACTION_RETURN_INT(CheckRealHeight(texid));
}

static int OkForLocalization_(int index, const FString& substitute)
{
	return sysCallbacks.OkForLocalization? sysCallbacks.OkForLocalization(FSetTextureID(index), substitute.GetChars()) : false;
}

DEFINE_ACTION_FUNCTION_NATIVE(_TexMan, OkForLocalization, OkForLocalization_)
{
	PARAM_PROLOGUE;
	PARAM_INT(name);
	PARAM_STRING(subst)
	ACTION_RETURN_INT(OkForLocalization_(name, subst));
}

static int UseGamePalette(int index)
{
	auto tex = TexMan.GameByIndex(index, false);
	if (!tex) return false;
	auto image = tex->GetTexture()->GetImage();
	return image ? image->UseGamePalette() : false;
}

DEFINE_ACTION_FUNCTION_NATIVE(_TexMan, UseGamePalette, UseGamePalette)
{
	PARAM_PROLOGUE;
	PARAM_INT(texid);
	ACTION_RETURN_INT(UseGamePalette(texid));
}

FCanvas* GetTextureCanvas(const FString& texturename);

DEFINE_ACTION_FUNCTION(_TexMan, GetCanvas)
{
	PARAM_PROLOGUE;
	PARAM_STRING(texturename);
	ACTION_RETURN_POINTER(GetTextureCanvas(texturename));
}

//=====================================================================================
//
// FFont exports
//
//=====================================================================================

static FFont *GetFont(int name)
{
	return V_GetFont(FName(ENamedName(name)).GetChars());
}

DEFINE_ACTION_FUNCTION_NATIVE(FFont, GetFont, GetFont)
{
	PARAM_PROLOGUE;
	PARAM_INT(name);
	ACTION_RETURN_POINTER(GetFont(name));
}

static FFont *FindFont(int name)
{
	return FFont::FindFont(FName(ENamedName(name)));
}

DEFINE_ACTION_FUNCTION_NATIVE(FFont, FindFont, FindFont)
{
	PARAM_PROLOGUE;
	PARAM_NAME(name);
	ACTION_RETURN_POINTER(FFont::FindFont(name));
}

static int GetCharWidth(FFont *font, int code)
{
	return font->GetCharWidth(code);
}

DEFINE_ACTION_FUNCTION_NATIVE(FFont, GetCharWidth, GetCharWidth)
{
	PARAM_SELF_STRUCT_PROLOGUE(FFont);
	PARAM_INT(code);
	ACTION_RETURN_INT(self->GetCharWidth(code));
}

static int GetHeight(FFont *font)
{
	return font->GetHeight();
}

DEFINE_ACTION_FUNCTION_NATIVE(FFont, GetHeight, GetHeight)
{
	PARAM_SELF_STRUCT_PROLOGUE(FFont);
	ACTION_RETURN_INT(self->GetHeight());
}

static int GetDisplacement(FFont* font)
{
	return font->GetDisplacement();
}

DEFINE_ACTION_FUNCTION_NATIVE(FFont, GetDisplacement, GetDisplacement)
{
	PARAM_SELF_STRUCT_PROLOGUE(FFont);
	ACTION_RETURN_INT(self->GetDisplacement());
}

double GetBottomAlignOffset(FFont *font, int c);
DEFINE_ACTION_FUNCTION_NATIVE(FFont, GetBottomAlignOffset, GetBottomAlignOffset)
{
	PARAM_SELF_STRUCT_PROLOGUE(FFont);
	PARAM_INT(code);
	ACTION_RETURN_FLOAT(GetBottomAlignOffset(self, code));
}

static int StringWidth(FFont *font, const FString &str, int localize)
{
	const char *txt = (localize && str[0] == '$') ? GStrings(&str[1]) : str.GetChars();
	return font->StringWidth(txt);
}

DEFINE_ACTION_FUNCTION_NATIVE(FFont, StringWidth, StringWidth)
{
	PARAM_SELF_STRUCT_PROLOGUE(FFont);
	PARAM_STRING(str);
	PARAM_BOOL(localize);
	ACTION_RETURN_INT(StringWidth(self, str, localize));
}

static int GetMaxAscender(FFont* font, const FString& str, int localize)
{
	const char* txt = (localize && str[0] == '$') ? GStrings(&str[1]) : str.GetChars();
	return font->GetMaxAscender(txt);
}

DEFINE_ACTION_FUNCTION_NATIVE(FFont, GetMaxAscender, GetMaxAscender)
{
	PARAM_SELF_STRUCT_PROLOGUE(FFont);
	PARAM_STRING(str);
	PARAM_BOOL(localize);
	ACTION_RETURN_INT(GetMaxAscender(self, str, localize));
}

static int CanPrint(FFont *font, const FString &str, int localize)
{
	const char *txt = (localize && str[0] == '$') ? GStrings(&str[1]) : str.GetChars();
	return font->CanPrint(txt);
}

DEFINE_ACTION_FUNCTION_NATIVE(FFont, CanPrint, CanPrint)
{
	PARAM_SELF_STRUCT_PROLOGUE(FFont);
	PARAM_STRING(str);
	PARAM_BOOL(localize);
	ACTION_RETURN_INT(CanPrint(self, str, localize));
}

static int FindFontColor(int name)
{
	return V_FindFontColor(ENamedName(name));
}

DEFINE_ACTION_FUNCTION_NATIVE(FFont, FindFontColor, FindFontColor)
{
	PARAM_PROLOGUE;
	PARAM_NAME(code);
	ACTION_RETURN_INT((int)V_FindFontColor(code));
}

static void GetCursor(FFont *font, FString *result)
{
	*result = font->GetCursor();
}

DEFINE_ACTION_FUNCTION_NATIVE(FFont, GetCursor, GetCursor)
{
	PARAM_SELF_STRUCT_PROLOGUE(FFont);
	ACTION_RETURN_STRING(FString(self->GetCursor()));
}

static int GetGlyphHeight(FFont* fnt, int code)
{
	auto glyph = fnt->GetChar(code, CR_UNTRANSLATED, nullptr);
	return glyph ? (int)glyph->GetDisplayHeight() : 0;
}

DEFINE_ACTION_FUNCTION_NATIVE(FFont, GetGlyphHeight, GetGlyphHeight)
{
	PARAM_SELF_STRUCT_PROLOGUE(FFont);
	PARAM_INT(code);
	ACTION_RETURN_INT(GetGlyphHeight(self, code));
}

static int GetDefaultKerning(FFont* font)
{
	return font->GetDefaultKerning();
}

DEFINE_ACTION_FUNCTION_NATIVE(FFont, GetDefaultKerning, GetDefaultKerning)
{
	PARAM_SELF_STRUCT_PROLOGUE(FFont);
	ACTION_RETURN_INT(self->GetDefaultKerning());
}

static double GetDisplayTopOffset(FFont* font, int c)
{
	auto texc = font->GetChar(c, CR_UNDEFINED, nullptr);
	return texc ? texc->GetDisplayTopOffset() : 0;
}

DEFINE_ACTION_FUNCTION_NATIVE(FFont, GetDisplayTopOffset, GetDisplayTopOffset)
{
	PARAM_SELF_STRUCT_PROLOGUE(FFont);
	PARAM_INT(code);
	ACTION_RETURN_FLOAT(GetDisplayTopOffset(self, code));
}

//==========================================================================
//
// file system
//
//==========================================================================

DEFINE_ACTION_FUNCTION(_Wads, GetNumLumps)
{
	PARAM_PROLOGUE;
	ACTION_RETURN_INT(fileSystem.GetNumEntries());
}

DEFINE_ACTION_FUNCTION(_Wads, CheckNumForName)
{
	PARAM_PROLOGUE;
	PARAM_STRING(name);
	PARAM_INT(ns);
	PARAM_INT(wadnum);
	PARAM_BOOL(exact);
	ACTION_RETURN_INT(fileSystem.CheckNumForName(name.GetChars(), ns, wadnum, exact));
}

DEFINE_ACTION_FUNCTION(_Wads, CheckNumForFullName)
{
	PARAM_PROLOGUE;
	PARAM_STRING(name);
	ACTION_RETURN_INT(fileSystem.CheckNumForFullName(name.GetChars()));
}

DEFINE_ACTION_FUNCTION(_Wads, FindLump)
{
	PARAM_PROLOGUE;
	PARAM_STRING(name);
	PARAM_INT(startlump);
	PARAM_INT(ns);
	const bool isLumpValid = startlump >= 0 && startlump < fileSystem.GetNumEntries();
	ACTION_RETURN_INT(isLumpValid ? fileSystem.FindLump(name.GetChars(), &startlump, 0 != ns) : -1);
}

DEFINE_ACTION_FUNCTION(_Wads, FindLumpFullName)
{
	PARAM_PROLOGUE;
	PARAM_STRING(name);
	PARAM_INT(startlump);
	PARAM_BOOL(noext);
	const bool isLumpValid = startlump >= 0 && startlump < fileSystem.GetNumEntries();
	ACTION_RETURN_INT(isLumpValid ? fileSystem.FindLumpFullName(name.GetChars(), &startlump, noext) : -1);
}

DEFINE_ACTION_FUNCTION(_Wads, GetLumpName)
{
	PARAM_PROLOGUE;
	PARAM_INT(lump);
	ACTION_RETURN_STRING(fileSystem.GetFileShortName(lump));
}

DEFINE_ACTION_FUNCTION(_Wads, GetLumpFullName)
{
	PARAM_PROLOGUE;
	PARAM_INT(lump);
	ACTION_RETURN_STRING(fileSystem.GetFileFullName(lump));
}

DEFINE_ACTION_FUNCTION(_Wads, GetLumpNamespace)
{
	PARAM_PROLOGUE;
	PARAM_INT(lump);
	ACTION_RETURN_INT(fileSystem.GetFileNamespace(lump));
}

DEFINE_ACTION_FUNCTION(_Wads, ReadLump)
{
	PARAM_PROLOGUE;
	PARAM_INT(lump);
	const bool isLumpValid = lump >= 0 && lump < fileSystem.GetNumEntries();
	ACTION_RETURN_STRING(isLumpValid ? GetStringFromLump(lump, false) : FString());
}

DEFINE_ACTION_FUNCTION(_Wads, GetLumpLength)
{
	PARAM_PROLOGUE;
	PARAM_INT(lump);
	ACTION_RETURN_INT(fileSystem.FileLength(lump));
}

//==========================================================================
//
// CVARs
//
//==========================================================================

DEFINE_ACTION_FUNCTION(_CVar, GetInt)
{
	PARAM_SELF_STRUCT_PROLOGUE(FBaseCVar);
	auto v = self->GetGenericRep(CVAR_Int);
	ACTION_RETURN_INT(v.Int);
}

DEFINE_ACTION_FUNCTION(_CVar, GetFloat)
{
	PARAM_SELF_STRUCT_PROLOGUE(FBaseCVar);
	auto v = self->GetGenericRep(CVAR_Float);
	ACTION_RETURN_FLOAT(v.Float);
}

DEFINE_ACTION_FUNCTION(_CVar, GetString)
{
	PARAM_SELF_STRUCT_PROLOGUE(FBaseCVar);
	auto v = self->GetGenericRep(CVAR_String);
	ACTION_RETURN_STRING(v.String);
}

DEFINE_ACTION_FUNCTION(_CVar, SetInt)
{
	// Only menus are allowed to change CVARs.
	PARAM_SELF_STRUCT_PROLOGUE(FBaseCVar);
	if (!(self->GetFlags() & CVAR_MOD))
	{
		// Only menus are allowed to change non-mod CVARs.
		if (DMenu::InMenu == 0)
		{
			ThrowAbortException(X_OTHER, "Attempt to change CVAR '%s' outside of menu code", self->GetName());
		}
	}
	PARAM_INT(val);
	UCVarValue v;
	v.Int = val;

	if(self->GetFlags() & CVAR_ZS_CUSTOM_CLONE)
	{
		auto realCVar = (FBaseCVar*)(self->GetExtraDataPointer());
		assert(realCVar->GetFlags() & CVAR_ZS_CUSTOM);
		
		v = realCVar->GenericZSCVarCallback(v, CVAR_Int);
		self->SetGenericRep(v, realCVar->GetRealType());

		if(realCVar->GetRealType() == CVAR_String) delete[] v.String;
	}
	else
	{
		self->SetGenericRep(v, CVAR_Int);
	}

	return 0;
}

DEFINE_ACTION_FUNCTION(_CVar, SetFloat)
{
	PARAM_SELF_STRUCT_PROLOGUE(FBaseCVar);
	if (!(self->GetFlags() & CVAR_MOD))
	{
		// Only menus are allowed to change non-mod CVARs.
		if (DMenu::InMenu == 0)
		{
			ThrowAbortException(X_OTHER, "Attempt to change CVAR '%s' outside of menu code", self->GetName());
		}
	}
	PARAM_FLOAT(val);
	UCVarValue v;
	v.Float = (float)val;

	if(self->GetFlags() & CVAR_ZS_CUSTOM_CLONE)
	{
		auto realCVar = (FBaseCVar*)(self->GetExtraDataPointer());
		assert(realCVar->GetFlags() & CVAR_ZS_CUSTOM);

		v = realCVar->GenericZSCVarCallback(v, CVAR_Float);
		self->SetGenericRep(v, realCVar->GetRealType());

		if(realCVar->GetRealType() == CVAR_String) delete[] v.String;
	}
	else
	{
		self->SetGenericRep(v, CVAR_Float);
	}

	return 0;
}

DEFINE_ACTION_FUNCTION(_CVar, SetString)
{
	// Only menus are allowed to change CVARs.
	PARAM_SELF_STRUCT_PROLOGUE(FBaseCVar);
	if (!(self->GetFlags() & CVAR_MOD))
	{
		// Only menus are allowed to change non-mod CVARs.
		if (DMenu::InMenu == 0)
		{
			ThrowAbortException(X_OTHER, "Attempt to change CVAR '%s' outside of menu code", self->GetName());
		}
	}
	PARAM_STRING(val);
	UCVarValue v;
	v.String = val.GetChars();

	if(self->GetFlags() & CVAR_ZS_CUSTOM_CLONE)
	{
		auto realCVar = (FBaseCVar*)(self->GetExtraDataPointer());
		assert(realCVar->GetFlags() & CVAR_ZS_CUSTOM);

		v = realCVar->GenericZSCVarCallback(v, CVAR_String);
		self->SetGenericRep(v, realCVar->GetRealType());

		if(realCVar->GetRealType() == CVAR_String) delete[] v.String;
	}
	else
	{
		self->SetGenericRep(v, CVAR_String);
	}

	return 0;
}

DEFINE_ACTION_FUNCTION(_CVar, GetRealType)
{
	PARAM_SELF_STRUCT_PROLOGUE(FBaseCVar);
	ACTION_RETURN_INT(self->GetRealType());
}

DEFINE_ACTION_FUNCTION(_CVar, ResetToDefault)
{
	PARAM_SELF_STRUCT_PROLOGUE(FBaseCVar);
	if (!(self->GetFlags() & CVAR_MOD))
	{
		// Only menus are allowed to change non-mod CVARs.
		if (DMenu::InMenu == 0)
		{
			ThrowAbortException(X_OTHER, "Attempt to change CVAR '%s' outside of menu code", self->GetName());
		}
	}

	self->ResetToDefault();
	return 0;
}

DEFINE_ACTION_FUNCTION(_CVar, FindCVar)
{
	PARAM_PROLOGUE;
	PARAM_NAME(name);
	ACTION_RETURN_POINTER(FindCVar(name.GetChars(), nullptr));
}

//=============================================================================
//
//
//
//=============================================================================

DEFINE_ACTION_FUNCTION(FKeyBindings, SetBind)
{
	PARAM_SELF_STRUCT_PROLOGUE(FKeyBindings);
	PARAM_INT(k);
	PARAM_STRING(cmd);

	// Only menus are allowed to change bindings.
	if (DMenu::InMenu == 0)
	{
		I_FatalError("Attempt to change key bindings outside of menu code to '%s'", cmd.GetChars());
	}


	self->SetBind(k, cmd.GetChars());
	return 0;
}

DEFINE_ACTION_FUNCTION(FKeyBindings, NameKeys)
{
	PARAM_PROLOGUE;
	PARAM_INT(k1);
	PARAM_INT(k2);
	char buffer[120];
	C_NameKeys(buffer, k1, k2);
	ACTION_RETURN_STRING(buffer);
}

DEFINE_ACTION_FUNCTION(FKeyBindings, NameAllKeys)
{
	PARAM_PROLOGUE;
	PARAM_POINTER(array, TArray<int>);
	PARAM_BOOL(color);
	auto buffer = C_NameKeys(array->Data(), array->Size(), color);
	ACTION_RETURN_STRING(buffer);
}

DEFINE_ACTION_FUNCTION(FKeyBindings, GetKeysForCommand)
{
	PARAM_SELF_STRUCT_PROLOGUE(FKeyBindings);
	PARAM_STRING(cmd);
	int k1, k2;
	self->GetKeysForCommand(cmd.GetChars(), &k1, &k2);
	if (numret > 0) ret[0].SetInt(k1);
	if (numret > 1) ret[1].SetInt(k2);
	return min(numret, 2);
}

DEFINE_ACTION_FUNCTION(FKeyBindings, GetAllKeysForCommand)
{
	PARAM_SELF_STRUCT_PROLOGUE(FKeyBindings);
	PARAM_POINTER(array, TArray<int>);
	PARAM_STRING(cmd);
	*array = self->GetKeysForCommand(cmd.GetChars());
	return 0;
}

DEFINE_ACTION_FUNCTION(FKeyBindings, GetBinding)
{
	PARAM_SELF_STRUCT_PROLOGUE(FKeyBindings);
	PARAM_INT(key);
	ACTION_RETURN_STRING(self->GetBinding(key));
}

DEFINE_ACTION_FUNCTION(FKeyBindings, UnbindACommand)
{
	PARAM_SELF_STRUCT_PROLOGUE(FKeyBindings);
	PARAM_STRING(cmd);

	// Only menus are allowed to change bindings.
	if (DMenu::InMenu == 0)
	{
		I_FatalError("Attempt to unbind key bindings for '%s' outside of menu code", cmd.GetChars());
	}

	self->UnbindACommand(cmd.GetChars());
	return 0;
}

// This is only accessible to the special menu item to run CCMDs.
DEFINE_ACTION_FUNCTION(DOptionMenuItemCommand, DoCommand)
{
	PARAM_PROLOGUE;
	PARAM_STRING(cmd);
	PARAM_BOOL(unsafe);

	// Only menus are allowed to execute CCMDs.
	if (DMenu::InMenu == 0)
	{
		I_FatalError("Attempt to execute CCMD '%s' outside of menu code", cmd.GetChars());
	}

	UnsafeExecutionScope scope(unsafe);
	AddCommandString(cmd.GetChars());
	return 0;
}

DEFINE_ACTION_FUNCTION(_Console, HideConsole)
{
	C_HideConsole();
	return 0;
}

DEFINE_ACTION_FUNCTION(_Console, Printf)
{
	PARAM_PROLOGUE;
	PARAM_VA_POINTER(va_reginfo)	// Get the hidden type information array

	FString s = FStringFormat(VM_ARGS_NAMES);
	Printf("%s\n", s.GetChars());
	return 0;
}

DEFINE_ACTION_FUNCTION(_Console, PrintfEx)
{
	PARAM_PROLOGUE;
	PARAM_INT(printlevel);
	PARAM_VA_POINTER(va_reginfo)	// Get the hidden type information array

	FString s = FStringFormat(VM_ARGS_NAMES,1);

	Printf(printlevel,"%s\n", s.GetChars());
	return 0;
}

static void StopAllSounds()
{
	soundEngine->StopAllChannels();
}

DEFINE_ACTION_FUNCTION_NATIVE(_System, StopAllSounds, StopAllSounds)
{
	StopAllSounds();
	return 0;
}

static int PlayMusic(const FString& musname, int order, int looped)
{
	return S_ChangeMusic(musname.GetChars(), order, !!looped, true);
}

DEFINE_ACTION_FUNCTION_NATIVE(_System, PlayMusic, PlayMusic)
{
	PARAM_PROLOGUE;
	PARAM_STRING(name);
	PARAM_INT(order);
	PARAM_BOOL(looped);
	ACTION_RETURN_BOOL(PlayMusic(name, order, looped));
}

static void Mus_Stop()
{
	S_StopMusic(true);
}

DEFINE_ACTION_FUNCTION_NATIVE(_System, StopMusic, Mus_Stop)
{
	Mus_Stop();
	return 0;
}

DEFINE_ACTION_FUNCTION_NATIVE(_System, SoundEnabled, SoundEnabled)
{
	ACTION_RETURN_INT(SoundEnabled());
}

DEFINE_ACTION_FUNCTION_NATIVE(_System, MusicEnabled, MusicEnabled)
{
	ACTION_RETURN_INT(MusicEnabled());
}

static double Jit_GetTimeFrac() // cannot use I_GetTimwfrac directly due to default arguments.
{
	return I_GetTimeFrac();
}

DEFINE_ACTION_FUNCTION_NATIVE(_System, GetTimeFrac, Jit_GetTimeFrac)
{
	ACTION_RETURN_FLOAT(I_GetTimeFrac());
}


DEFINE_GLOBAL_NAMED(mus_playing, musplaying);
DEFINE_FIELD_X(MusPlayingInfo, MusPlayingInfo, name);
DEFINE_FIELD_X(MusPlayingInfo, MusPlayingInfo, baseorder);
DEFINE_FIELD_X(MusPlayingInfo, MusPlayingInfo, loop);
DEFINE_FIELD_X(MusPlayingInfo, MusPlayingInfo, handle);

DEFINE_GLOBAL_NAMED(PClass::AllClasses, AllClasses)

DEFINE_GLOBAL(AllServices)

DEFINE_GLOBAL(Bindings)
DEFINE_GLOBAL(AutomapBindings)
DEFINE_GLOBAL(generic_ui)

DEFINE_FIELD(DStatusBarCore, RelTop);
DEFINE_FIELD(DStatusBarCore, HorizontalResolution);
DEFINE_FIELD(DStatusBarCore, VerticalResolution);
DEFINE_FIELD(DStatusBarCore, CompleteBorder);
DEFINE_FIELD(DStatusBarCore, Alpha);
DEFINE_FIELD(DStatusBarCore, drawOffset);
DEFINE_FIELD(DStatusBarCore, drawClip);
DEFINE_FIELD(DStatusBarCore, fullscreenOffsets);
DEFINE_FIELD(DStatusBarCore, defaultScale);
DEFINE_FIELD(DHUDFont, mFont);

//
// Quaternion
void QuatFromAngles(double yaw, double pitch, double roll, DQuaternion* pquat)
{
	*pquat = DQuaternion::FromAngles(DAngle::fromDeg(yaw), DAngle::fromDeg(pitch), DAngle::fromDeg(roll));
}

DEFINE_ACTION_FUNCTION_NATIVE(_QuatStruct, FromAngles, QuatFromAngles)
{
	PARAM_PROLOGUE;
	PARAM_FLOAT(yaw);
	PARAM_FLOAT(pitch);
	PARAM_FLOAT(roll);

	DQuaternion quat;
	QuatFromAngles(yaw, pitch, roll, &quat);
	ACTION_RETURN_QUAT(quat);
}

void QuatAxisAngle(double x, double y, double z, double angleDeg, DQuaternion* pquat)
{
	auto axis = DVector3(x, y, z);
	auto angle = DAngle::fromDeg(angleDeg);
	*pquat = DQuaternion::AxisAngle(axis, angle);
}

DEFINE_ACTION_FUNCTION_NATIVE(_QuatStruct, AxisAngle, QuatAxisAngle)
{
	PARAM_PROLOGUE;
	PARAM_FLOAT(x);
	PARAM_FLOAT(y);
	PARAM_FLOAT(z);
	PARAM_FLOAT(angle);

	DQuaternion quat;
	QuatAxisAngle(x, y, z, angle, &quat);
	ACTION_RETURN_QUAT(quat);
}

void QuatNLerp(
	double ax, double ay, double az, double aw,
	double bx, double by, double bz, double bw,
	double t,
	DQuaternion* pquat
)
{
	auto from = DQuaternion { ax, ay, az, aw };
	auto to   = DQuaternion { bx, by, bz, bw };
	*pquat = DQuaternion::NLerp(from, to, t);
}

DEFINE_ACTION_FUNCTION_NATIVE(_QuatStruct, NLerp, QuatNLerp)
{
	PARAM_PROLOGUE;
	PARAM_FLOAT(ax);
	PARAM_FLOAT(ay);
	PARAM_FLOAT(az);
	PARAM_FLOAT(aw);
	PARAM_FLOAT(bx);
	PARAM_FLOAT(by);
	PARAM_FLOAT(bz);
	PARAM_FLOAT(bw);
	PARAM_FLOAT(t);

	DQuaternion quat;
	QuatNLerp(ax, ay, az, aw, bx, by, bz, bw, t, &quat);
	ACTION_RETURN_QUAT(quat);
}

void QuatSLerp(
	double ax, double ay, double az, double aw,
	double bx, double by, double bz, double bw,
	double t,
	DQuaternion* pquat
)
{
	auto from = DQuaternion { ax, ay, az, aw };
	auto to   = DQuaternion { bx, by, bz, bw };
	*pquat = DQuaternion::SLerp(from, to, t);
}

DEFINE_ACTION_FUNCTION_NATIVE(_QuatStruct, SLerp, QuatSLerp)
{
	PARAM_PROLOGUE;
	PARAM_FLOAT(ax);
	PARAM_FLOAT(ay);
	PARAM_FLOAT(az);
	PARAM_FLOAT(aw);
	PARAM_FLOAT(bx);
	PARAM_FLOAT(by);
	PARAM_FLOAT(bz);
	PARAM_FLOAT(bw);
	PARAM_FLOAT(t);

	DQuaternion quat;
	QuatSLerp(ax, ay, az, aw, bx, by, bz, bw, t, &quat);
	ACTION_RETURN_QUAT(quat);
}

void QuatConjugate(
	double x, double y, double z, double w,
	DQuaternion* pquat
)
{
	*pquat = DQuaternion(x, y, z, w).Conjugate();
}

DEFINE_ACTION_FUNCTION_NATIVE(_QuatStruct, Conjugate, QuatConjugate)
{
	PARAM_SELF_STRUCT_PROLOGUE(DQuaternion);

	DQuaternion quat;
	QuatConjugate(self->X, self->Y, self->Z, self->W, &quat);
	ACTION_RETURN_QUAT(quat);
}

void QuatInverse(
	double x, double y, double z, double w,
	DQuaternion* pquat
)
{
	*pquat = DQuaternion(x, y, z, w).Inverse();
}

DEFINE_ACTION_FUNCTION_NATIVE(_QuatStruct, Inverse, QuatInverse)
{
	PARAM_SELF_STRUCT_PROLOGUE(DQuaternion);

	DQuaternion quat;
	QuatInverse(self->X, self->Y, self->Z, self->W, &quat);
	ACTION_RETURN_QUAT(quat);
}

PFunction * FindFunctionPointer(PClass * cls, int fn_name)
{
	auto fn = dyn_cast<PFunction>(cls->FindSymbol(ENamedName(fn_name), true));
	return (fn && (fn->Variants[0].Flags & (VARF_Action | VARF_Virtual)) == 0 ) ? fn : nullptr;
}

DEFINE_ACTION_FUNCTION_NATIVE(DObject, FindFunction, FindFunctionPointer)
{
	PARAM_PROLOGUE;
	PARAM_CLASS(cls, DObject);
	PARAM_NAME(fn);

	ACTION_RETURN_POINTER(FindFunctionPointer(cls, fn.GetIndex()));
}

FTranslationID R_FindCustomTranslation(FName name);

static int ZFindTranslation(int intname)
{
	return R_FindCustomTranslation(ENamedName(intname)).index();
}

static int MakeTransID(int g, int s)
{
	return TRANSLATION(g, s).index();
}

DEFINE_ACTION_FUNCTION_NATIVE(_Translation, GetID, ZFindTranslation)
{
	PARAM_PROLOGUE;
	PARAM_INT(t);
	ACTION_RETURN_INT(ZFindTranslation(t));
}

// same as above for the compiler which needs a class to look this up.
DEFINE_ACTION_FUNCTION_NATIVE(DObject, BuiltinFindTranslation, ZFindTranslation)
{
	PARAM_PROLOGUE;
	PARAM_INT(t);
	ACTION_RETURN_INT(ZFindTranslation(t));
}

DEFINE_ACTION_FUNCTION_NATIVE(_Translation, MakeID, MakeTransID)
{
	PARAM_PROLOGUE;
	PARAM_INT(g);
	PARAM_INT(t);
	ACTION_RETURN_INT(MakeTransID(g, t));
}

#if HAVE_RT

EXTERN_CVAR( Int, menu_resolution_custom_width )
EXTERN_CVAR( Int, menu_resolution_custom_height )

static std::pair< int, int > GetFullscreen();

// To set window size / fullscreen
DEFINE_ACTION_FUNCTION(DListMenuItemTextItem_RT, ApplyResolution)
{
    PARAM_PROLOGUE;
    PARAM_INT(width);
    PARAM_INT(height);
    PARAM_BOOL(fullscreen);

	const auto [width_fs, height_fs] = GetFullscreen();
	fullscreen = fullscreen || (width == width_fs && height == height_fs);

    if (fullscreen)
    {
		width = width_fs;
		height = height_fs;
    }

    menu_resolution_custom_width = std::max(200, width);
    menu_resolution_custom_height = std::max(200, height);

    auto cmd = std::string{ "menu_resolution_commit_changes " };
    cmd += fullscreen ? "1" : "0";

    AddCommandString(cmd.c_str());
    return 0;
}

DEFINE_ACTION_FUNCTION(DListMenuItemTextItem_RT, ValidateResolutions)
{
    PARAM_PROLOGUE;
    PARAM_POINTER(arrWidthHeight, TArray<int>);

    assert(arrWidthHeight->Size() % 2 == 0);

    const auto fullscreen = GetFullscreen();

    TArray<int> validated;
    bool foundFullscreen = false;
    for (uint32_t i = 0; i < arrWidthHeight->Size() / 2; i++)
    {
        int w = (*arrWidthHeight)[i * 2 + 0];
        int h = (*arrWidthHeight)[i * 2 + 1];

        if (w <= fullscreen.first && h <= fullscreen.second)
        {
            validated.Push(w);
            validated.Push(h);

            if (w == fullscreen.first && h == fullscreen.second)
            {
                foundFullscreen = true;
            }
        }
    }

    if (!foundFullscreen)
    {
        validated.Push(fullscreen.first);
        validated.Push(fullscreen.second);
    }

    assert(validated.Size() % 2 == 0);

    *arrWidthHeight = std::move(validated);
    return 0;
}

#include "v_video.h"

DEFINE_ACTION_FUNCTION(DListMenuItemTextItem_RT, GetCurrentWindowHeight)
{
    ACTION_RETURN_INT(screen->GetClientHeight());
}
DEFINE_ACTION_FUNCTION(DListMenuItemTextItem_RT, GetCurrentWindowWidth)
{
    ACTION_RETURN_INT(screen->GetClientWidth());
}

#include "rt/rt_cvars.h"

DEFINE_ACTION_FUNCTION(DListMenuItemTextItem_RT, IsAvailable_DLSS2)
{
    ACTION_RETURN_BOOL(cvar::rt_available_dlss2);
}
DEFINE_ACTION_FUNCTION(DListMenuItemTextItem_RT, IsAvailable_DLSS3FG)
{
	ACTION_RETURN_BOOL(cvar::rt_available_dlss3fg);
}
DEFINE_ACTION_FUNCTION(DListMenuItemTextItem_RT, IsAvailable_FSR2)
{
    ACTION_RETURN_BOOL(cvar::rt_available_fsr2);
}
DEFINE_ACTION_FUNCTION(DListMenuItemTextItem_RT, IsAvailable_FSR3FG)
{
	ACTION_RETURN_BOOL(cvar::rt_available_fsr3fg);
}
DEFINE_ACTION_FUNCTION(DListMenuItemTextItem_RT, IsAvailable_DXGI)
{
    ACTION_RETURN_BOOL(cvar::rt_available_dxgi);
}
DEFINE_ACTION_FUNCTION(DListMenuItemTextItem_RT, IsHDRAvailable)
{
    ACTION_RETURN_BOOL(cvar::rt_hdr_available);
}

#include <span>

static auto RT_GetDescription( std::string_view rtkey, bool forFirstStartMenu = false ) -> std::span< const char* >
{
    if( rtkey == "RTMNU_WINDOW_RESOL" )
    {
        static const char* lines[] = {
            "Set the window size to present into.",
            "If matches the display size, then assuming a fullscreen mode.",
        };
        return lines;
    }
    if( rtkey == "RTMNU_VSYNC" )
    {
        static const char* lines[] = {
            "Set VSync to OFF (recommended), to keep the latency low.",
            "(Latency is a time between an in-game action and",
            "its corresponding result being visible on display,",
            "e.g. mouse feels as if there's a delay).",
            "",
            "Set VSync to ON, only if you observe a screen tearing",
            "(i.e. image being torn vertically).",
            "VSync ON will enforce the game frame rate to be locked",
            "to a display refresh rate, which increases the latency.",
        };
        return lines;
    }
    if( rtkey == "RTMNU_DXGI" )
    {
        static const char* lines[] = {
            "Presentation engine to use to display the rendered image.",
            "",
            "On Windows, DXGI is recommended for a smoother experience",
            "(e.g. Alt-Tab, going to Fullscreen mode, etc). ",
            "But DXGI might lead to a slightly lower frame rate, because ",
            "the frame rendering is done via Vulkan API, but ",
            "that rendered frame is presented to a display via DirectX 12.",
        };
        return lines;
    }
    if( rtkey == "RTMNU_HDR" )
    {
        static const char* lines[] = {
            "If HDR is ON, the rendered frame will",
            "not be limited to standard 8-bit RGB color.",
            "With HDR, certain parts of the image (such as lava)",
            "might appear as bright as stepping outside",
            "into a scorching summer noon from inside a house,",
            "all windows of which were closed.",
            "",
            "Requires a display with HDR support.",
            "Windows Auto-HDR is not recommended to be OFF.",
        };
        return lines;
    }
    if( rtkey == "RTMNU_MODE" )
    {
        static const char* lines[] = {
            "Technique to apply to upscale from",
            "a render resolution to a window / display resolution.",
            "",
            "NVIDIA DLSS 2 -- Super Resolution.",
            "Uses AI for upscaling. Exclusive to NVIDIA RTX graphics cards.",
            "",
            "AMD FSR 2 -- Super Resolution.",
            "Uses heuristics for upscaling. Available on most graphics cards.",
            "",
			"Vintage techniques render at 90s displays' resolution.",
        };
        if( forFirstStartMenu )
        {
			// do not mention 'Vintage'
            return std::span{ lines, std::size( lines ) - 2 };
        }
        return lines;
    }
    if( rtkey == "RTMNU_PRESET" )
    {
        static const char* lines[] = {
            "Main setting to control GPU PERFORMANCE.",
            "",
            "      Ultra Perf. -- highest performance                  ",
            "      Performance -- better performance than Balanced     ",
            "         Balanced -- optimal image quality and performance",
            "          Quality -- higher image quality than Balanced   ",
            "    Native / DLAA -- only anti-aliasing, no upscaling     ",
        };
        return lines;
    }
    if( rtkey == "RTMNU_FRAMEGEN" )
    {
        static const char* lines[] = {
            "Insert an interpolated frame in-between actual frames.",
            "Because it's a virtual frame, expect a higher input latency.",
            "Requires a stable \'actual\' frame rate, more than 60 FPS.",
            "",
            "NVIDIA DLSS 3 -- Frame Generation.",
            "Uses AI to interpolate frames.",
            "Exclusive to NVIDIA RTX 40-series and higher.",
            "",
            "AMD FSR 3 -- Frame Generation.",
            "Uses heuristics to interpolate frames.",
            "Available on most graphics cards.",
        };
        return lines;
    }
    if( rtkey == "RTMNU_CLASSIC" )
    {
        static const char* lines[] = {
            "Render world with the original renderer",
            "on top of ray traced one.",
            "",
            "Use \'C\' key to switch",
            "between Classic and Path Traced renderers IN-GAME.",
        };
        return lines;
    }
    if( rtkey == "RTMNU_BLOOM" )
    {
        static const char* lines[] = {
            "Post-effect to simulate glow around bright spots.",
        };
        return lines;
    }
    if( rtkey == "RTMNU_HUD_STYLE" )
    {
        static const char* lines[] = {
            "In the graphics settings menu of Doom: Ray Traced, players are presented with a",
            "comprehensive array of HUD (Heads-Up Display) styles to suit their",
            "preferences. Among these options lie three distinct choices: None, Simple,",
            "and Classic. Opting for the \'None\' style removes all HUD elements from the",
            "screen, allowing for an unobstructed view of the game environment.",
            "Alternatively, selecting the \'Simple\' style offers users a streamlined",
            "display, providing essential information without unnecessary visual",
            "distractions. For those seeking a touch of nostalgia,",
            "the \'Classic\' style recreates the original status bar of DOOM. With",
            "these meticulously crafted options, players can tailor their gaming",
            "experience to align with their individual preferences, ensuring immersion and",
            "enjoyment throughout their gameplay journey :)",
        };
        return lines;
    }
    if( rtkey == "RTMNU_HUD_SIZE" )
    {
        static const char* lines[] = {
            "Yeah... tweaks the size of the HUD!",
        };
        return lines;
    }
    if( rtkey == "RTMNU_FLUID" )
    {
        static const char* lines[] = {
            "Fluid blood simulation and rendering.",
            "Impacts GPU performance considerably.",
        };
        return lines;
    }
    return {};
}

static auto RT_GetErrorsFor( std::string_view rtkey ) -> std::vector< const char* >
{
    if( rtkey == "RTMNU_WINDOW_RESOL" || //
        rtkey == "RTMNU_VSYNC" ||        //
        rtkey == "RTMNU_DXGI" ||         //
        rtkey == "RTMNU_HDR" )
    {
        if( cvar::rt_failreason_dxgi )
        {
            return { cvar::rt_failreason_dxgi };
        }
    }
    else if( rtkey == "RTMNU_MODE" ||   //
             rtkey == "RTMNU_PRESET" || //
             rtkey == "RTMNU_FRAMEGEN" )
    {
        auto errors = std::vector< const char* >{};
        if( cvar::rt_failreason_dlss2 || cvar::rt_failreason_dlss3fg )
        {
            errors.push_back( cvar::rt_failreason_dlss2 && cvar::rt_failreason_dlss3fg
                                  ? "NVIDIA DLSS 2 and NVIDIA DLSS 3 failed:"
                              : cvar::rt_failreason_dlss2 && !cvar::rt_failreason_dlss3fg
                                  ? "NVIDIA DLSS 2 failed:"
                                  : "NVIDIA DLSS 3 failed:" );
            errors.push_back( cvar::rt_failreason_dlss2 ? cvar::rt_failreason_dlss2
                                                        : cvar::rt_failreason_dlss3fg );
        }
        if( cvar::rt_failreason_fsr2 || cvar::rt_failreason_fsr3fg )
        {
            errors.push_back(
                cvar::rt_failreason_fsr2 && cvar::rt_failreason_fsr3fg    ? "AMD FSR 2/3 failed:"
                : cvar::rt_failreason_fsr2 && !cvar::rt_failreason_fsr3fg ? "AMD FSR 2 failed:"
                                                                          : "AMD FSR 3 failed:" );
            errors.push_back( cvar::rt_failreason_fsr2 ? cvar::rt_failreason_fsr2
                                                       : cvar::rt_failreason_fsr3fg );
        }
        return errors;
    }
    return {};
}

// lack of global vars in zs...
static bool g_bool0 = false;
DEFINE_ACTION_FUNCTION( DListMenuItemTextItem_RT, RT_SetBool0 )
{
    PARAM_PROLOGUE;
    PARAM_BOOL( v );
    g_bool0 = v;
    return 0;
}

DEFINE_ACTION_FUNCTION( DListMenuItemTextItem_RT, RT_GetBool0 )
{
    ACTION_RETURN_BOOL( g_bool0 );
}

DEFINE_ACTION_FUNCTION( DListMenuItemTextItem_RT, RT_DrawDescriptionHotkey )
{
    PARAM_PROLOGUE;
    PARAM_INT( a_key );

    // not printable
    if( a_key <= ' ' || a_key > '~' )
    {
        return 0;
    }

    char c = char( toupper( a_key ) );

    bool red = false;
    {
        using namespace std::chrono;

        static auto g_lasttime   = high_resolution_clock::time_point{};
        static auto g_blinkstart = high_resolution_clock::time_point{};
        const auto  now          = high_resolution_clock::now();
        if( now - g_lasttime > seconds{ 120 } )
        {
            g_blinkstart = now;
        }
        g_lasttime = now;

        const float secondsSinceStart =
            float( duration_cast< milliseconds >( now - g_blinkstart ).count() ) / 1000.0f;
        if( secondsSinceStart > 0.5f && secondsSinceStart < 5.5f )
        {
            int i = ( int( secondsSinceStart * 3.5f ) % 5 );
            red   = ( i == 0 || i == 2 );
        }
    }

    const auto strdesc = std::string{ c } + " - show description";

    int safe          = 4;
    int canvas_height = 280;
    int canvas_width  = canvas_height * float( twod->GetWidth() ) / float( twod->GetHeight() );

	double x = canvas_width - safe - SmallFont->StringWidth( strdesc.c_str() );
    double y = canvas_height - safe - SmallFont->GetHeight();

	if( red )
    {
        const uint8_t rgb[] = { 0, 0, 255 };

        double xr = x - safe;
        double yr = y - safe;
        double w  = SmallFont->StringWidth( strdesc.c_str() ) + safe * 2;
        double h  = SmallFont->GetHeight() + safe * 2;

        VirtualToRealCoords( twod, xr, yr, w, h, canvas_width, canvas_height, false, false );

        ClearRect( twod, //
                   xr,
                   yr,
                   xr + w,
                   yr + h,
                   -1,
                   ( int( rgb[ 0 ] ) << 16 | int( rgb[ 1 ] ) << 8 | int( rgb[ 2 ] ) ) );
    }

    DrawText( twod,
              SmallFont,
              CR_WHITE,
              x,
              y,
              strdesc.c_str(),
              DTA_VirtualWidth,
              canvas_width,
              DTA_VirtualHeight,
              canvas_height,
              DTA_KeepRatio,
              true,
              TAG_DONE );

    return 0;
}

DEFINE_ACTION_FUNCTION( DListMenuItemTextItem_RT, RT_HasDescription )
{
    PARAM_PROLOGUE;
    PARAM_STRING( rtkey );

	const auto lines = RT_GetDescription( std::string_view{ rtkey.GetChars(), rtkey.Len() } );
    const bool result = !lines.empty();

    ACTION_RETURN_BOOL( result );
}

void RT_DrawSettingDescription( std::string_view rtkey, bool forFirstStartMenu = false )
{
    const auto lines = RT_GetDescription( rtkey, forFirstStartMenu );
    if( lines.empty() )
    {
        return;
    }
    constexpr static const char* prompt[] = {
        "",
        "Press \'Esc\' to close",
    };
	auto errors = RT_GetErrorsFor( rtkey );
    if( !errors.empty() )
    {
        errors.insert( errors.begin(), "" );
    }


    // tweakables
    int              canvas_height = 540;
    FFont*           font          = NewConsoleFont ? NewConsoleFont : SmallFont;
    constexpr int    yspacing      = 4;
    constexpr int    safe          = 16;
    constexpr int    border        = 2;


    auto l_align = []( int x, int alignment ) {
        return ( ( x + alignment - 1 ) / alignment ) * alignment;
    };

    canvas_height    = l_align( canvas_height, font->GetHeight() );
    int canvas_width = l_align( canvas_height * 4.f / 3.f, font->GetHeight() );


    const int line_height = ( font->GetHeight() + yspacing );

    int alllines_height =
        int( std::size( prompt ) + std::size( lines ) + std::size( errors ) ) * line_height -
        yspacing;

    int alllines_width  = 0;
    for( const char* l : lines )
    {
        alllines_width = std::max( alllines_width, font->StringWidth( l ) );
    }
    for( const char* l : errors )
    {
        alllines_width = std::max( alllines_width, font->StringWidth( l ) );
    }
    for( const char* l : prompt )
    {
        alllines_width = std::max( alllines_width, font->StringWidth( l ) );
    }

    alllines_width  = l_align( alllines_width, border * 2 );
    alllines_height = l_align( alllines_height, border * 2 );


	auto l_drawcenterquad = [ & ]( int width, int height, uint8_t r, uint8_t g, uint8_t b ) {
        double w = width;
        double h = height;
        double x = canvas_width * 0.5 - w * 0.5;
        double y = canvas_height * 0.5 - h * 0.5;

        VirtualToRealCoords( twod, x, y, w, h, canvas_width, canvas_height );

        ClearRect( twod, //
                   x,
                   y,
                   x + w,
                   y + h,
                   -1,
                   ( int( r ) << 16 | int( g ) << 8 | int( b ) ) );
    };

    auto l_drawline = [ & ]( double y, const char* str, bool red = false ) {
        double x = canvas_width * 0.5 - font->StringWidth( str ) * 0.5;

        DrawText( twod,
                  font,
                  red ? CR_RED : CR_WHITE,
                  x,
                  y,
                  str,
                  DTA_VirtualWidth,
                  canvas_width,
                  DTA_VirtualHeight,
                  canvas_height,
                  DTA_KeepRatio,
                  false,
                  TAG_DONE );
        static_assert( std::is_same_v< decltype( canvas_width ), int >, "DTA_VirtualWidth or DTA_VirtualWidthF" );
        static_assert( std::is_same_v< decltype( canvas_height ), int >,
                       "DTA_VirtualWidth or DTA_VirtualWidthF" );
    };

	l_drawcenterquad( border + safe + alllines_width + safe + border,
                      border + safe + alllines_height + safe + border,
                      150,
                      150,
                      150 );
    l_drawcenterquad( safe + alllines_width + safe, //
                      safe + alllines_height + safe,
                      0,
                      0,
                      220 );

    double y = canvas_height * 0.5 - alllines_height * 0.5;
    for( const char* l : lines )
    {
        l_drawline( y, l );
        y += line_height;
    }
    for( const char* l : errors )
    {
        l_drawline( y, l, true );
        y += line_height;
    }
    for( const char* l : prompt )
    {
        l_drawline( y, l );
        y += line_height;
    }
}

DEFINE_ACTION_FUNCTION( DBlockingDescription_RT, RT_DrawDescriptions )
{
    PARAM_PROLOGUE;
    PARAM_STRING( a_rtkey );

    RT_DrawSettingDescription( std::string_view{ a_rtkey.GetChars(), a_rtkey.Len() } );

    return 0;
}

#define NOMINMAX
    #include "windows.h"

static std::pair< int, int > GetFullscreen()
{
    // TODO: make a call to Screen
    // should be a monitor resolution
    return {
        GetSystemMetrics( SM_CXSCREEN ),
        GetSystemMetrics( SM_CYSCREEN ),
    };
}

#endif // HAVE_RT
