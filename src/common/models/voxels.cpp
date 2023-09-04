/*
** voxels.cpp
**
**---------------------------------------------------------------------------
** Copyright 2010-2011 Randy Heit
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

#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <ctype.h>

#include "m_swap.h"
#include "m_argv.h"
#include "filesystem.h"
#include "v_video.h"
#include "sc_man.h"
#include "voxels.h"
#include "printf.h"

void VOX_AddVoxel(int sprnum, int frame, FVoxelDef *def);

TDeletingArray<FVoxel *> Voxels;	// used only to auto-delete voxels on exit.
TDeletingArray<FVoxelDef *> VoxelDefs;

//==========================================================================
//
// GetVoxelRemap
//
// Calculates a remap table for the voxel's palette. Results are cached so
// passing the same palette repeatedly will not require repeated
// recalculations.
//
//==========================================================================

static uint8_t *GetVoxelRemap(const uint8_t *pal)
{
	static uint8_t remap[256];
	static uint8_t oldpal[768];
	static bool firsttime = true;

	if (firsttime || memcmp(oldpal, pal, 768) != 0)
	{ // Not the same palette as last time, so recalculate.
		firsttime = false;
		memcpy(oldpal, pal, 768);
		for (int i = 0; i < 256; ++i)
		{
			// The voxel palette uses VGA colors, so we have to expand it
			// from 6 to 8 bits per component.
			remap[i] = ColorMatcher.Pick(
				(oldpal[i*3 + 0] << 2) | (oldpal[i*3 + 0] >> 4),
				(oldpal[i*3 + 1] << 2) | (oldpal[i*3 + 1] >> 4),
				(oldpal[i*3 + 2] << 2) | (oldpal[i*3 + 2] >> 4));
		}
	}
	return remap;
}

//==========================================================================
//
// CopyVoxelSlabs
//
// Copy all the slabs in a block of slabs.
//
//==========================================================================

static bool CopyVoxelSlabs(kvxslab_t *dest, const kvxslab_t *src, int size)
{
	while (size >= 3)
	{
		int slabzleng = src->zleng;

		if (3 + slabzleng > size)
		{ // slab is too tall
			return false;
		}

		dest->ztop = src->ztop;
		dest->zleng = src->zleng;
		dest->backfacecull = src->backfacecull;

		for (int j = 0; j < slabzleng; ++j)
		{
			dest->col[j] = src->col[j];
		}
		slabzleng += 3;
		src = (kvxslab_t *)((uint8_t *)src + slabzleng);
		dest = (kvxslab_t *)((uint8_t *)dest + slabzleng);
		size -= slabzleng;
	}
	return true;
}

//==========================================================================
//
// RemapVoxelSlabs
//
// Remaps all the slabs in a block of slabs.
//
//==========================================================================

static void RemapVoxelSlabs(kvxslab_t *dest, int size, const uint8_t *remap)
{
	while (size >= 3)
	{
		int slabzleng = dest->zleng;

		for (int j = 0; j < slabzleng; ++j)
		{
			dest->col[j] = remap[dest->col[j]];
		}
		slabzleng += 3;
		dest = (kvxslab_t *)((uint8_t *)dest + slabzleng);
		size -= slabzleng;
	}
}

//==========================================================================
//
// R_LoadKVX
//
//==========================================================================

#if defined __GNUC__ && !defined __clang__
#pragma GCC push_options
#pragma GCC optimize ("-fno-tree-loop-vectorize")
#endif // __GNUC__ && !__clang__

#if !HAVE_RT // .vox support
FVoxel *R_LoadKVX(int lumpnum)
#else
FVoxel *R_LoadKVX_internal(const uint8_t *rawvoxel, int voxelsize, int lumpnum)
#endif
{
	const kvxslab_t *slabs[MAXVOXMIPS];
	FVoxel *voxel = new FVoxel;
	const uint8_t *rawmip;
	int mip, maxmipsize;
	int i, j, n;

#if !HAVE_RT // .vox support
	auto lump =  fileSystem.ReadFile(lumpnum);	// FileData adds an extra 0 byte to the end.
	auto rawvoxel = lump.bytes();
	int voxelsize = (int)(lump.size());
#endif

	// Oh, KVX, why couldn't you have a proper header? We'll just go through
	// and collect each MIP level, doing lots of range checking, and if the
	// last one doesn't end exactly 768 bytes before the end of the file,
	// we'll reject it.

	for (mip = 0, rawmip = rawvoxel, maxmipsize = voxelsize - 768 - 4;
		 mip < MAXVOXMIPS;
		 mip++)
	{
		int numbytes = GetInt(rawmip);
		if (numbytes > maxmipsize || numbytes < 24)
		{
			break;
		}
		rawmip += 4;

		FVoxelMipLevel *mipl = &voxel->Mips[mip];

		// Load header data.
		mipl->SizeX = GetInt(rawmip + 0);
		mipl->SizeY = GetInt(rawmip + 4);
		mipl->SizeZ = GetInt(rawmip + 8);
		mipl->Pivot.X = GetInt(rawmip + 12) / 256.;
		mipl->Pivot.Y = GetInt(rawmip + 16) / 256.;
		mipl->Pivot.Z = GetInt(rawmip + 20) / 256.;

		// How much space do we have for voxdata?
		int offsetsize = (mipl->SizeX + 1) * 4 + mipl->SizeX * (mipl->SizeY + 1) * 2;
		int voxdatasize = numbytes - 24 - offsetsize;
		if (voxdatasize < 0)
		{ // Clearly, not enough.
			break;
		}
		if (voxdatasize != 0)
		{	// This mip level is not empty.
			// Allocate slab data space.
			mipl->OffsetX = new int[(numbytes - 24 + 3) / 4];
			mipl->OffsetXY = (short *)(mipl->OffsetX + mipl->SizeX + 1);
			mipl->SlabData = (uint8_t *)(mipl->OffsetXY + mipl->SizeX * (mipl->SizeY + 1));

			// Load x offsets.
			for (i = 0, n = mipl->SizeX; i <= n; ++i)
			{
				// The X offsets stored in the KVX file are relative to the start of the
				// X offsets array. Make them relative to voxdata instead.
				mipl->OffsetX[i] = GetInt(rawmip + 24 + i * 4) - offsetsize;
			}

			// The first X offset must be 0 (since we subtracted offsetsize), according to the spec:
			//		NOTE: xoffset[0] = (xsiz+1)*4 + xsiz*(ysiz+1)*2 (ALWAYS)
			if (mipl->OffsetX[0] != 0)
			{
				break;
			}
			// And the final X offset must point just past the end of the voxdata.
			if (mipl->OffsetX[mipl->SizeX] != voxdatasize)
			{
				break;
			}

			// Load xy offsets.
			i = 24 + i * 4;
			for (j = 0, n *= mipl->SizeY + 1; j < n; ++j)
			{
				mipl->OffsetXY[j] = GetShort(rawmip + i + j * 2);
			}

			// Ensure all offsets are within bounds.
			for (i = 0; i < mipl->SizeX; ++i)
			{
				int xoff = mipl->OffsetX[i];
				for (j = 0; j < mipl->SizeY; ++j)
				{
					int yoff = mipl->OffsetXY[(mipl->SizeY + 1) * i + j];
					if (unsigned(xoff + yoff) > unsigned(voxdatasize))
					{
						delete voxel;
						return NULL;
					}
				}
			}

			// Record slab location for the end.
			slabs[mip] = (kvxslab_t *)(rawmip + 24 + offsetsize);
		}

		// Time for the next mip Level.
		rawmip += numbytes;
		maxmipsize -= numbytes + 4;
	}
	// Did we get any mip levels, and if so, does the last one leave just
	// enough room for the palette after it?
	if (mip == 0 || rawmip != rawvoxel + voxelsize - 768)
	{
		delete voxel;
		return NULL;
	}

	// Do not count empty mips at the end.
	for (; mip > 0; --mip)
	{
		if (voxel->Mips[mip - 1].SlabData != NULL)
			break;
	}
	voxel->NumMips = mip;

	// Fix pivot data for submips, since some tools seem to like to just center these.
	for (i = 1; i < mip; ++i)
	{
		voxel->Mips[i].Pivot = voxel->Mips[i - 1].Pivot / 2;
	}

	for (i = 0; i < mip; ++i)
	{
		if (!CopyVoxelSlabs((kvxslab_t *)voxel->Mips[i].SlabData, slabs[i], voxel->Mips[i].OffsetX[voxel->Mips[i].SizeX]))
		{ // Invalid slabs encountered. Reject this voxel.
			delete voxel;
			return NULL;
		}
	}

	voxel->LumpNum = lumpnum;
	voxel->Palette.Resize(768);
	memcpy(voxel->Palette.Data(), rawvoxel + voxelsize - 768, 768);

	return voxel;
}

#if defined __GNUC__ && !defined __clang__
#pragma GCC pop_options
#endif // __GNUC__ && !__clang__

#if HAVE_RT // .vox support

auto SLADE_voxToKvx(const uint8_t *in) -> std::vector<uint8_t>
{
#define AT(x, y, z) (((x) * length + (y)) * height + (z))

#pragma pack(push, r1, 1)
	struct VoxStartHeader
	{
		char name[4];
		int version;
	};

	struct VoxChunk
	{
		char name[4];
		int numBytes;
		int numBytesChildren;
	};

	struct Xyzi
	{
		uint8_t x;
		uint8_t y;
		uint8_t z;
		uint8_t colorIndex;
	};

	struct VoxRgba
	{
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t a;
	};

	struct KvxRgb
	{
		uint8_t r;
		uint8_t g;
		uint8_t b;
	};

	struct KvxHeader
	{
		uint32_t total_bytes;
		uint32_t width;
		uint32_t length;
		uint32_t height;
		uint32_t pivot_x;
		uint32_t pivot_y;
		uint32_t pivot_z;
	};

	struct KvxColumnPostHeader
	{
		uint8_t topdelta;
		uint8_t size;
		uint8_t culling;
	};
#pragma pack(pop, r1)

	const uint8_t LEFT = 1;
	const uint8_t RIGHT = 2;
	const uint8_t FRONT = 4;
	const uint8_t BACK = 8;
	const uint8_t TOP = 16;
	const uint8_t BOTTOM = 32;

	auto readin = [&in](void *dst, size_t sz) -> const void *
		{
			const void *src = in;
			if (dst)
			{
				memcpy(dst, src, sz);
			}
			in += sz;
			return src;
		};
	auto checkname = [](const char name[4], const char *check)
		{
			return name[0] == check[0] &&
				name[1] == check[1] &&
				name[2] == check[2] &&
				name[3] == check[3];
		};

	{
		VoxStartHeader voxHeader{};
		readin(&voxHeader, sizeof(voxHeader));
		assert(checkname(voxHeader.name, "VOX "));
	}
	{
		VoxChunk mainChunk{};
		readin(&mainChunk, sizeof(mainChunk));
		assert(checkname(mainChunk.name, "MAIN"));

		assert(mainChunk.numBytes == 0);
		readin(nullptr, mainChunk.numBytes);
	}
	{
		VoxChunk packChunk{};
		readin(&packChunk, sizeof(packChunk));
		if (checkname(packChunk.name, "PACK"))
		{
			assert(packChunk.numBytes == 4);

			int numModels = 0;
			readin(&numModels, sizeof(numModels));
		}
		else
		{
			// reset back
			in -= sizeof(packChunk);
		}
	}

	uint32_t width = 0;
	uint32_t length = 0;
	uint32_t height = 0;
	{
		VoxChunk szChunk{};
		readin(&szChunk, sizeof(szChunk));
		assert(checkname(szChunk.name, "SIZE"));

		assert(szChunk.numBytes == 12);

		readin(&width, sizeof(width));
		readin(&length, sizeof(length));
		readin(&height, sizeof(height));
	}

#define EMPTYVOXEL 0

	// XYZ -> color or EMPTYVOXEL
	auto voxels = std::vector<uint8_t>{};
	voxels.resize((size_t)width * (size_t)length * (size_t)height);
	{
		VoxChunk xyziChunk{};
		readin(&xyziChunk, sizeof(xyziChunk));
		assert(checkname(xyziChunk.name, "XYZI"));

		assert(xyziChunk.numBytes >= 4);

		int numVoxels = 0;
		readin(&numVoxels, sizeof(numVoxels));

		static_assert(sizeof(Xyzi) == 4);
		auto allXyzi = static_cast<const Xyzi *>(readin(nullptr, sizeof(Xyzi) * numVoxels));

		memset(voxels.data(), EMPTYVOXEL, voxels.size());
		for (int i = 0; i < numVoxels; i++)
		{
			const Xyzi &v = allXyzi[i];

			assert(v.colorIndex != EMPTYVOXEL);
			voxels[AT(v.x, v.y, v.z)] = v.colorIndex;
		}
	}

	//
	// mykola-ambar begin
	//
	// Based on https://github.com/mykola-ambar 's VOX to KVX converter
	// https://github.com/sirjuddington/SLADE/pull/1063
	//

	auto visibilities = std::vector<uint8_t>{};
	visibilities.resize((size_t)width * length * height);
	for (uint32_t x = 0; x < width; x++)
	{
		for (uint32_t y = 0; y < length; y++)
		{
			for (uint32_t z = 0; z < height; z++)
			{
				if (voxels[AT(x, y, z)] == EMPTYVOXEL) continue;

				if (x == 0 || voxels[AT(x - 1, y, z)] == EMPTYVOXEL)
					visibilities[AT(x, y, z)] |= LEFT;
				if (x == width - 1 || voxels[AT(x + 1, y, z)] == EMPTYVOXEL)
					visibilities[AT(x, y, z)] |= RIGHT;
				if (y == 0 || voxels[AT(x, y - 1, z)] == EMPTYVOXEL)
					visibilities[AT(x, y, z)] |= FRONT;
				if (y == length - 1 || voxels[AT(x, y + 1, z)] == EMPTYVOXEL)
					visibilities[AT(x, y, z)] |= BACK;
				if (z == 0 || voxels[AT(x, y, z - 1)] == EMPTYVOXEL)
					visibilities[AT(x, y, z)] |= TOP;
				if (z == height - 1 || voxels[AT(x, y, z + 1)] == EMPTYVOXEL)
					visibilities[AT(x, y, z)] |= BOTTOM;
			}
		}
	}
	for (uint32_t x = 0; x < width; x++)
	{
		for (uint32_t y = 0; y < length; y++)
		{
			for (uint32_t z = 0; z < height; z++)
			{
				if (visibilities[AT(x, y, z)] == 0)
				{
					voxels[AT(x, y, z)] = EMPTYVOXEL;
				}
			}
		}
	}

	//
	// mykola-ambar end
	//


	// skip until palette
	while (true)
	{
		VoxChunk c{};
		readin(&c, sizeof(c));
		if (checkname(c.name, "RGBA"))
		{
			// reset back
			in -= sizeof(c);
			break;
		}
		readin(nullptr, c.numBytes);
	}

	auto palette = std::vector<KvxRgb>{};
	palette.resize(256);
	{
		VoxChunk rgbaChunk{};
		readin(&rgbaChunk, sizeof(rgbaChunk));
		assert(checkname(rgbaChunk.name, "RGBA"));

		static_assert(sizeof(VoxRgba) == 4);
		auto allRgba = (const VoxRgba *)readin(nullptr, 256 * sizeof(VoxRgba));
		for (int i = 0; i <= 254; i++)
		{
			palette[i + 1] = KvxRgb{ allRgba[i].r, allRgba[i].g, allRgba[i].b };
		}
	}


	//
	// mykola-ambar begin
	//
	//  Based on https://github.com/mykola-ambar 's VOX to KVX converter
	// https://github.com/sirjuddington/SLADE/pull/1063
	//

	uint32_t *xoffsets = new uint32_t[width + 1];
	uint16_t *xyoffsets = new uint16_t[width * (length + 1)];

	auto out_data = std::vector<uint8_t>{};
	size_t out_ptr = 0;

	// skip, write at the end
	out_ptr += sizeof(KvxHeader);
	out_ptr += (width + 1) * sizeof(uint32_t);
	out_ptr += width * (length + 1) * sizeof(uint16_t);

	auto out_currentPos = [&out_ptr]()
		{
			return out_ptr;
		};
	auto out_write = [&out_data, &out_ptr](const void *data, const size_t sz)
		{
			if (out_data.size() < out_ptr + sz)
			{
				out_data.resize(out_ptr + sz);
			}

			memcpy(out_data.data() + out_ptr, data, sz);
			out_ptr += sz;
			assert(out_ptr <= out_data.size());
		};

	xoffsets[0] = out_currentPos() - sizeof(KvxHeader);

	std::vector<uint8_t> post_colors;
	post_colors.reserve(256);

	for (uint32_t x = 0; x < width; x++)
	{
		xyoffsets[x * (length + 1)] = out_currentPos() - xoffsets[x] - sizeof(KvxHeader);
		for (uint32_t y = 0; y < length; y++)
		{
			for (uint32_t z = 0; z < height; z++)
			{
				uint8_t color = voxels[AT(x, y, z)];
				uint8_t visibility = visibilities[AT(x, y, z)];
				bool at_end = (z == height - 1);

				if (color != EMPTYVOXEL)
				{
					post_colors.push_back(color);
				}

				bool must_write_post = (!post_colors.empty()
					&& (at_end
						|| voxels[AT(x, y, z + 1)] == EMPTYVOXEL
						|| visibilities[AT(x, y, z + 1)] != visibility
						|| post_colors.size() == 255
						));

				if (must_write_post)
				{
					auto postHeader = KvxColumnPostHeader{
						.topdelta = uint8_t(z - (post_colors.size() - 1)),
						.size = uint8_t(post_colors.size()),
						.culling = visibility,
					};

					out_write(&postHeader, sizeof(KvxColumnPostHeader));
					out_write(post_colors.data(), post_colors.size());
					post_colors.clear();
				}
			}
			xyoffsets[x * (length + 1) + y + 1] = out_currentPos() - xoffsets[x] - sizeof(KvxHeader);
		}
		xoffsets[x + 1] = out_currentPos() - sizeof(KvxHeader);
	}

	uint32_t total_bytes = out_currentPos() - sizeof(uint32_t);

	assert(palette.size() * sizeof(decltype(palette)::value_type) == 768);
	out_write(palette.data(), 768);

	auto kvxHeader = KvxHeader{
		.total_bytes = total_bytes,
		.width = width,
		.length = length,
		.height = height,
		.pivot_x = width << 7,
		.pivot_y = length << 7,
		.pivot_z = height << 8
	};

	// out.seek(0, SEEK_SET);
	out_ptr = 0;
	out_write(&kvxHeader, sizeof(KvxHeader));
	out_write(xoffsets, (width + 1) * sizeof(uint32_t));
	out_write(xyoffsets, width * (length + 1) * sizeof(uint16_t));

	delete[] xoffsets;
	delete[] xyoffsets;

	// 
	// mykola-ambar end
	// 

	return out_data;

#undef AT
}

FVoxel *R_LoadKVX(int lumpnum)
{
	auto lump = fileSystem.ReadFile(lumpnum);	// FileData adds an extra 0 byte to the end.
	auto rawvoxel = lump.bytes();
	int voxelsize = (int)(lump.size());

	const char *name = fileSystem.GetFileFullName(lumpnum, false);
	if (name && strstr(name, ".vox") && voxelsize > 0)
	{
		auto kvx = SLADE_voxToKvx(rawvoxel);
		return R_LoadKVX_internal(kvx.data(), kvx.size(), lumpnum);
	}

	return R_LoadKVX_internal(rawvoxel, voxelsize, lumpnum);
}

#endif

//==========================================================================
//
//
//
//==========================================================================

FVoxelDef *R_LoadVoxelDef(int lumpnum, int spin)
{
	FVoxel *vox = R_LoadKVX(lumpnum);
	if (vox == NULL)
	{
		Printf("%s is not a valid voxel file\n", fileSystem.GetFileFullName(lumpnum));
		return NULL;
	}
	else
	{
		FVoxelDef *voxdef = new FVoxelDef;
		*voxdef = {};
		voxdef->Voxel = vox;
		voxdef->Scale = 1.;
		voxdef->DroppedSpin = voxdef->PlacedSpin = spin;
		voxdef->AngleOffset = DAngle::fromDeg(90.);

		Voxels.Push(vox);
		VoxelDefs.Push(voxdef);
		return voxdef;
	}
}

//==========================================================================
//
// FVoxelMipLevel Constructor
//
//==========================================================================

FVoxelMipLevel::FVoxelMipLevel()
{
	SizeZ = SizeY = SizeX = 0;
	Pivot.Zero();
	OffsetX = NULL;
	OffsetXY = NULL;
	SlabData = NULL;
}

//==========================================================================
//
// FVoxelMipLevel Destructor
//
//==========================================================================

FVoxelMipLevel::~FVoxelMipLevel()
{
	if (OffsetX != NULL)
	{
		delete[] OffsetX;
	}
}

//==========================================================================
//
// FVoxelMipLevel :: GetSlabData
//
//==========================================================================

uint8_t *FVoxelMipLevel::GetSlabData(bool wantremapped) const
{
	if (wantremapped && SlabDataRemapped.Size() > 0) return &SlabDataRemapped[0];
	return SlabData;
}

//==========================================================================
//
// Create true color version of the slab data
//
//==========================================================================

void FVoxel::CreateBgraSlabData()
{
	if (Bgramade) return;
	Bgramade = true;
	for (int i = 0; i < NumMips; ++i)
	{
		int size = Mips[i].OffsetX[Mips[i].SizeX];
		if (size <= 0) continue;

		Mips[i].SlabDataBgra.Resize(size);

		kvxslab_t *src = (kvxslab_t*)Mips[i].SlabData;
		kvxslab_bgra_t *dest = (kvxslab_bgra_t*)&Mips[i].SlabDataBgra[0];

		while (size >= 3)
		{
			dest->backfacecull = src->backfacecull;
			dest->ztop = src->ztop;
			dest->zleng = src->zleng;

			int slabzleng = src->zleng;
			for (int j = 0; j < slabzleng; ++j)
			{
				int colorIndex = src->col[j];

				uint32_t red, green, blue;
				if (Palette.Size())
				{
					red = (Palette[colorIndex * 3 + 0] << 2) | (Palette[colorIndex * 3 + 0] >> 4);
					green = (Palette[colorIndex * 3 + 1] << 2) | (Palette[colorIndex * 3 + 1] >> 4);
					blue = (Palette[colorIndex * 3 + 2] << 2) | (Palette[colorIndex * 3 + 2] >> 4);
				}
				else
				{
					red = GPalette.BaseColors[colorIndex].r;
					green = GPalette.BaseColors[colorIndex].g;
					blue = GPalette.BaseColors[colorIndex].b;
				}

				dest->col[j] = 0xff000000 | (red << 16) | (green << 8) | blue;
			}
			slabzleng += 3;

			dest = (kvxslab_bgra_t *)((uint32_t *)dest + slabzleng);
			src = (kvxslab_t *)((uint8_t *)src + slabzleng);
			size -= slabzleng;
		}
	}
}

//==========================================================================
//
// Remap the voxel to the game palette
//
//==========================================================================

void FVoxel::Remap()
{
	if (Remapped) return;
	Remapped = true;
	if (Palette.Size())
	{
		uint8_t *remap = GetVoxelRemap(Palette.Data());
		for (int i = 0; i < NumMips; ++i)
		{
			int size = Mips[i].OffsetX[Mips[i].SizeX];
			if (size <= 0) continue;

			Mips[i].SlabDataRemapped.Resize(size);
			memcpy(&Mips[i].SlabDataRemapped [0], Mips[i].SlabData, size);
			RemapVoxelSlabs((kvxslab_t *)&Mips[i].SlabDataRemapped[0], Mips[i].OffsetX[Mips[i].SizeX], remap);
		}
	}
}

//==========================================================================
//
// Delete the voxel's built-in palette
//
//==========================================================================

void FVoxel::RemovePalette()
{
	Palette.Reset();
}


//==========================================================================
//
// VOX_GetVoxel
//
// Returns a voxel object for the given lump or NULL if it is not a valid
// voxel. If the voxel has already been loaded, it will be reused.
//
//==========================================================================

FVoxel* VOX_GetVoxel(int lumpnum)
{
	// Is this voxel already loaded? If so, return it.
	for (unsigned i = 0; i < Voxels.Size(); ++i)
	{
		if (Voxels[i]->LumpNum == lumpnum)
		{
			return Voxels[i];
		}
	}
	FVoxel* vox = R_LoadKVX(lumpnum);
	if (vox != NULL)
	{
		Voxels.Push(vox);
	}
	return vox;
}


