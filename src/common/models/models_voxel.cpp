// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2010-2016 Christoph Oelckers
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//
/*
** gl_voxels.cpp
**
** Voxel management
**
**/

#include "filesystem.h"
#include "colormatcher.h"
#include "bitmap.h"
#include "model_kvx.h"
#include "image.h"
#include "texturemanager.h"
#include "modelrenderer.h"
#include "voxels.h"
#include "texturemanager.h"
#include "palettecontainer.h"
#include "textures.h"
#include "imagehelpers.h"

#ifdef _MSC_VER
#pragma warning(disable:4244) // warning C4244: conversion from 'double' to 'float', possible loss of data
#endif

#if HAVE_RT
    #define VOXEL_TO_GLTF 1
#endif

#ifdef VOXEL_TO_GLTF
#include "m_argv.h"
#include "printf.h"

#include <filesystem>
#include <fstream>
#include <span>

#include <cgltf/cgltf.h>

#define CGLTF_WRITE_IMPLEMENTATION
#include <cgltf/cgltf_write.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

auto rt_make_gltf_path( const char* folder, const char* name ) -> std::filesystem::path
{
    return std::filesystem::path{ folder } / ( std::string{ name } + ".gltf" );
}

auto rt_make_material_name( const char* name ) -> std::string
{
    return "vx_" + std::string{ name };
}

void rt_export_to_gltf( const char*                     folder,
                        const char*                     name,
                        const std::span< FModelVertex > verts,
                        const std::span< uint32_t >     indices,
                        const std::span< uint8_t >      imageData )
{
    auto materialname              = rt_make_material_name( name );
    auto texture_filename_relative = materialname + ".tga";

    constexpr int NEAREST_FILTER = 9728;
    constexpr int WRAP_REPEAT    = 10497;

    cgltf_sampler sampler = {
        .name       = nullptr,
        .mag_filter = NEAREST_FILTER,
        .min_filter = NEAREST_FILTER,
        .wrap_s     = WRAP_REPEAT,
        .wrap_t     = WRAP_REPEAT,
    };

    {
        auto pt = ( std::filesystem::path{ folder } / texture_filename_relative ).string();

        assert( imageData.size_bytes() == 16 * 16 * 3 );
        stbi_write_tga( pt.c_str(), 16, 16, 3, imageData.data() );
    }

    cgltf_image image = {
        .name = materialname.data(),
        .uri  = texture_filename_relative.data(),
    };

    cgltf_texture texture = {
        .image   = &image,
        .sampler = &sampler,
    };
    
    cgltf_material material = {
        .name                       = materialname.data(),
        .has_pbr_metallic_roughness = true,
        .pbr_metallic_roughness =
            cgltf_pbr_metallic_roughness{
                .base_color_texture = cgltf_texture_view{ .texture = &texture, .texcoord = 0, .scale = 1.0f, },
                .metallic_roughness_texture = {},
                .base_color_factor          = { 1.0f, 1.0f, 1.0f, 1.0f },
                .metallic_factor            = 0.0f,
                .roughness_factor           = 1.0f,
            },
    };

    cgltf_buffer      buf = {};
    cgltf_buffer_view bufviews[ 2 ]{};

    const auto binpath = rt_make_gltf_path( folder, name ).replace_extension( ".bin" );
    const auto binuri  = binpath.filename().string();
    {
        auto bin = std::ofstream{ binpath, std::ios::out | std::ios::trunc | std::ios::binary };

        size_t vert_offset  = 0;
        size_t vert_size    = 0;
        size_t index_offset = 0;
        size_t index_size   = 0;

        {
            bin.write( reinterpret_cast< const char* >( verts.data() ),
                       std::streamsize( verts.size_bytes() ) );
            vert_offset = 0;
            vert_size   = verts.size_bytes();
        }
        {
            bin.write( reinterpret_cast< const char* >( indices.data() ),
                       std::streamsize( indices.size_bytes() ) );
            index_offset = vert_size;
            index_size   = indices.size_bytes();
        }

        buf = cgltf_buffer{
            .name = nullptr,
            .size = vert_size + index_size,
            .uri  = const_cast< char* >( binuri.c_str() ),
        };

    #define BUFFER_VIEW_VERTS 0
        bufviews[ BUFFER_VIEW_VERTS ] = cgltf_buffer_view{
            .name   = nullptr,
            .buffer = &buf,
            .offset = vert_offset,
            .size   = vert_size,
            .stride = sizeof( FModelVertex ),
            .type   = cgltf_buffer_view_type_vertices,
        };
    #define BUFFER_VIEW_INDEX 1
        bufviews[ BUFFER_VIEW_INDEX ] = cgltf_buffer_view{
            .name   = nullptr,
            .buffer = &buf,
            .offset = index_offset,
            .size   = index_size,
            .stride = sizeof( uint32_t ),
            .type   = cgltf_buffer_view_type_indices,
        };
    }

    cgltf_accessor accessors[] = {
    #define ACCESSOR_POSITION 0
        {
            .component_type = cgltf_component_type_r_32f,
            .type           = cgltf_type_vec3,
            .offset         = offsetof( FModelVertex, x ),
            .count          = verts.size(),
            .stride         = sizeof( FModelVertex ),
            .buffer_view    = &bufviews[ BUFFER_VIEW_VERTS ],
        },
    #define ACCESSOR_TEXCOORD 1
        {
            .component_type = cgltf_component_type_r_32f,
            .type           = cgltf_type_vec2,
            .offset         = offsetof( FModelVertex, u ),
            .count          = verts.size(),
            .stride         = sizeof( FModelVertex ),
            .buffer_view    = &bufviews[ BUFFER_VIEW_VERTS ],
        },
    #define ACCESSOR_INDEX 2
        {
            .component_type = cgltf_component_type_r_32u,
            .type           = cgltf_type_scalar,
            .offset         = 0,
            .count          = indices.size(),
            .stride         = sizeof( uint32_t ),
            .buffer_view    = &bufviews[ BUFFER_VIEW_INDEX ],
        },
    };

    cgltf_attribute vert_attribs[] = {
        {
            .name  = const_cast< char* >( "POSITION" ),
            .type  = cgltf_attribute_type_position,
            .index = 0,
            .data  = &accessors[ ACCESSOR_POSITION ],
        },
        {
            .name  = const_cast< char* >( "TEXCOORD_0" ),
            .type  = cgltf_attribute_type_texcoord,
            .index = 0,
            .data  = &accessors[ ACCESSOR_TEXCOORD ],
        },
    };

    cgltf_primitive primitive = {
        .type             = cgltf_primitive_type_triangles,
        .indices          = &accessors[ ACCESSOR_INDEX ],
        .material         = &material,
        .attributes       = vert_attribs,
        .attributes_count = std::size( vert_attribs ),
    };

    auto name_alloc = std::string{ name };

    cgltf_mesh mesh = {
        .name             = name_alloc.data(),
        .primitives       = &primitive,
        .primitives_count = 1,
    };

    #define GLTF_MATRIX_IDENTITY 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1

    cgltf_node node = {
        .name            = const_cast< char* >( "main" ),
        .parent          = nullptr,
        .children        = nullptr,
        .children_count  = 0,
        .mesh            = &mesh,
        .has_translation = 1,
        .has_rotation    = 1,
        .has_scale       = 1,
        .has_matrix      = 0,
        .translation     = { 0, 0, 0 },
        .rotation        = { 0, 0, 1, 0 }, // rotate 180
        .scale           = { 1, 1, 1 },
    };

    cgltf_node* scene_nodes[] = { &node };

    cgltf_scene scene = {
        .name        = const_cast< char* >( "default" ),
        .nodes       = scene_nodes,
        .nodes_count = std::size( scene_nodes ),
    };

    cgltf_data data = {
        .asset =
            cgltf_asset{
                .copyright   = nullptr,
                .generator   = const_cast< char* >( "GZDOOM" ),
                .version     = const_cast< char* >( "2.0" ),
                .min_version = nullptr,
            },
        .meshes             = &mesh,
        .meshes_count       = 1,
        .materials          = &material,
        .materials_count    = 1,
        .accessors          = accessors,
        .accessors_count    = std::size( accessors ),
        .buffer_views       = bufviews,
        .buffer_views_count = std::size( bufviews ),
        .buffers            = &buf,
        .buffers_count      = 1,
        .images             = &image,
        .images_count       = 1,
        .textures           = &texture,
        .textures_count     = 1,
        .samplers           = &sampler,
        .samplers_count     = 1,
        .nodes              = &node,
        .nodes_count        = 1,
        .scenes             = &scene,
        .scenes_count       = 1,
        .scene              = &scene,
    };

    const auto filename = rt_make_gltf_path( folder, name ).string();

    constexpr cgltf_options defaultoptions = {};

    cgltf_result result = cgltf_write_file( &defaultoptions, filename.c_str(), &data );
    if( result != cgltf_result_success )
    {
        Printf( PRINT_HIGH,
                "Failed exporting VOXEL to GLTF (error code %i): %s\n",
                int( result ),
                name );
    }
}

#endif

//===========================================================================
//
// Creates a 16x16 texture from the palette so that we can
// use the existing palette manipulation code to render the voxel
// Otherwise all shaders had to be duplicated and the non-shader code
// would be a lot less efficient.
//
//===========================================================================

class FVoxelTexture : public FImageSource
{
public:
	FVoxelTexture(FVoxel *voxel);

	int CopyPixels(FBitmap *bmp, int conversion, int frame = 0) override;
	PalettedPixels CreatePalettedPixels(int conversion, int frame = 0) override;

protected:
	FVoxel *SourceVox;
};

//===========================================================================
//
// 
//
//===========================================================================

FVoxelTexture::FVoxelTexture(FVoxel *vox)
{
	SourceVox = vox;
	Width = 16;
	Height = 16;
	//bNoCompress = true;
}

//===========================================================================
//
// 
//
//===========================================================================

PalettedPixels FVoxelTexture::CreatePalettedPixels(int conversion, int frame)
{
	// GetPixels gets called when a translated palette is used so we still need to implement it here.
	PalettedPixels Pixels(256);
	uint8_t *pp = SourceVox->Palette.Data();

	if(pp != NULL)
	{
		for(int i=0;i<256;i++, pp+=3)
		{
			PalEntry pe;
#if !HAVE_RT // .vox support
			pe.r = (pp[0] << 2) | (pp[0] >> 4);
			pe.g = (pp[1] << 2) | (pp[1] >> 4);
			pe.b = (pp[2] << 2) | (pp[2] >> 4);
#else
			pe.r = pp[0];
			pe.g = pp[1];
			pe.b = pp[2];
#endif
			// Alphatexture handling is just for completeness, but rather unlikely to be used ever.
			Pixels[i] = conversion == luminance ? pe.r : ColorMatcher.Pick(pe);

		}
	}
	else 
	{
		for(int i=0;i<256;i++, pp+=3)
		{
			Pixels[i] = (uint8_t)i;
		}
	}  
	ImageHelpers::FlipSquareBlock(Pixels.Data(), Width);
	return Pixels;
}

//===========================================================================
//
// FVoxelTexture::CopyPixels
//
// This creates a dummy 16x16 paletted bitmap and converts that using the
// voxel palette
//
//===========================================================================

int FVoxelTexture::CopyPixels(FBitmap *bmp, int conversion, int frame)
{
	PalEntry pe[256];
	uint8_t bitmap[256];
	uint8_t *pp = SourceVox->Palette.Data();

	if(pp != nullptr)
	{
		for(int i=0;i<256;i++, pp+=3)
		{
			bitmap[i] = (uint8_t)i;
#if !HAVE_RT // .vox support
			pe[i].r = (pp[0] << 2) | (pp[0] >> 4);
			pe[i].g = (pp[1] << 2) | (pp[1] >> 4);
			pe[i].b = (pp[2] << 2) | (pp[2] >> 4);
#else
			pe[i].r = pp[0];
			pe[i].g = pp[1];
			pe[i].b = pp[2];
#endif
			pe[i].a = 255;
		}
	}
	else 
	{
		for(int i=0;i<256;i++, pp+=3)
		{
			bitmap[i] = (uint8_t)i;
			pe[i] = GPalette.BaseColors[i];
			pe[i].a = 255;
		}
	}    
	bmp->CopyPixelData(0, 0, bitmap, Width, Height, 1, 16, 0, pe);
	return 0;
}	

//===========================================================================
//
// 
//
//===========================================================================

FVoxelModel::FVoxelModel(FVoxel *voxel, bool owned)
{
	mVoxel = voxel;
	mOwningVoxel = owned;
#if !HAVE_RT
	mPalette = TexMan.AddGameTexture(MakeGameTexture(new FImageTexture(new FVoxelTexture(voxel)), nullptr, ETextureType::Override));
#else
	auto append = []<size_t N>(char(&str)[N], const char *toappend)
	{
		strncat_s(str, std::size(str), toappend, 9);
		str[std::size(str) - 1] = '\0';
	};
	auto removelastchar = []<size_t N>(char(&str)[N])
	{
		const size_t lastcharid = strnlen(str, std::size(str));
		str[lastcharid > 0 ? lastcharid - 1 : 0] = '\0';
	};

	// need a name for the texture to export
	char texname[16] = "vx_";
	append(texname, fileSystem.GetFileShortName(voxel->LumpNum));
	// last char is a frame index; remove it, assuming that a model has identical palette across its frames
	removelastchar(texname);
	mPalette = TexMan.AddGameTexture(MakeGameTexture(new FImageTexture(new FVoxelTexture(voxel)), texname, ETextureType::Override));
#endif

#ifdef VOXEL_TO_GLTF
    if( Args->CheckParm( "-vox2gltf" ) > 0 )
    {
        const char* folder    = "vox2gltf";
        const char* shortname = voxel ? fileSystem.GetFileShortName( voxel->LumpNum ) : nullptr;
        shortname             = shortname && shortname[ 0 ] != '\0' ? shortname : nullptr;

        if( shortname && !std::filesystem::exists( rt_make_gltf_path( folder, shortname ) ) )
        {
            std::error_code ec;
            std::filesystem::create_directory( folder, ec );

            Initialize();

            if( mVertices.Size() > 0 )
            {
                rt_export_to_gltf( folder,
                                   shortname,
                                   std::span{ mVertices.data(), mVertices.size() },
                                   std::span{ mIndices.data(), mIndices.size() },
                                   std::span{ voxel->Palette.data(), voxel->Palette.size() } );
            }

            // delete our temporary buffers
            mNumIndices = 0;
            mVertices.Clear();
            mIndices.Clear();
            mVertices.ShrinkToFit();
            mIndices.ShrinkToFit();
        }
    }
#endif
}

//===========================================================================
//
// 
//
//===========================================================================

FVoxelModel::~FVoxelModel()
{
	if (mOwningVoxel) delete mVoxel;
}


//===========================================================================
//
// 
//
//===========================================================================

unsigned int FVoxelModel::AddVertex(FModelVertex &vert, FVoxelMap &check)
{
	unsigned int index = check[vert];
	if (index == 0xffffffff)
	{
		index = check[vert] =mVertices.Push(vert);
	}
	return index;
}

//===========================================================================
//
// 
//
//===========================================================================

void FVoxelModel::AddFace(int x1, int y1, int z1, int x2, int y2, int z2, int x3, int y3, int z3, int x4, int y4, int z4, uint8_t col, FVoxelMap &check)
{
	float PivotX = mVoxel->Mips[0].Pivot.X;
	float PivotY = mVoxel->Mips[0].Pivot.Y;
	float PivotZ = mVoxel->Mips[0].Pivot.Z;
	FModelVertex vert;
	unsigned int indx[4];

	vert.packedNormal = 0;	// currently this is not being used for voxels.
	vert.u = (((col & 15) + 0.5f) / 16.f);
	vert.v = (((col / 16) + 0.5f) / 16.f);

	vert.x =  x1 - PivotX;
	vert.z = -y1 + PivotY;
	vert.y = -z1 + PivotZ;
	indx[0] = AddVertex(vert, check);

	vert.x =  x2 - PivotX;
	vert.z = -y2 + PivotY;
	vert.y = -z2 + PivotZ;
	indx[1] = AddVertex(vert, check);

	vert.x =  x4 - PivotX;
	vert.z = -y4 + PivotY;
	vert.y = -z4 + PivotZ;
	indx[2] = AddVertex(vert, check);

	vert.x =  x3 - PivotX;
	vert.z = -y3 + PivotY;
	vert.y = -z3 + PivotZ;
	indx[3] = AddVertex(vert, check);


	mIndices.Push(indx[0]);
	mIndices.Push(indx[1]);
	mIndices.Push(indx[3]);
	mIndices.Push(indx[1]);
	mIndices.Push(indx[2]);
	mIndices.Push(indx[3]);
}

//===========================================================================
//
// 
//
//===========================================================================

void FVoxelModel::MakeSlabPolys(int x, int y, kvxslab_t *voxptr, FVoxelMap &check)
{
	const uint8_t *col = voxptr->col;
	int zleng = voxptr->zleng;
	int ztop = voxptr->ztop;
	int cull = voxptr->backfacecull;

	if (cull & 16)
	{
		AddFace(x, y, ztop, x+1, y, ztop, x, y+1, ztop, x+1, y+1, ztop, *col, check);
	}
	int z = ztop;
	while (z < ztop+zleng)
	{
		int c = 0;
		while (z+c < ztop+zleng && col[c] == col[0]) c++;

		if (cull & 1)
		{
			AddFace(x, y, z, x, y+1, z, x, y, z+c, x, y+1, z+c, *col, check);
		}
		if (cull & 2)
		{
			AddFace(x+1, y+1, z, x+1, y, z, x+1, y+1, z+c, x+1, y, z+c, *col, check);
		}
		if (cull & 4)
		{
			AddFace(x+1, y, z, x, y, z, x+1, y, z+c, x, y, z+c, *col, check);
		}
		if (cull & 8)
		{
			AddFace(x, y+1, z, x+1, y+1, z, x, y+1, z+c, x+1, y+1, z+c, *col, check);
		}	
		z+=c;
		col+=c;
	}
	if (cull & 32)
	{
		int zz = ztop+zleng-1;
		AddFace(x+1, y, zz+1, x, y, zz+1, x+1, y+1, zz+1, x, y+1, zz+1, voxptr->col[zleng-1], check);
	}
}

//===========================================================================
//
// 
//
//===========================================================================

void FVoxelModel::Initialize()
{
	FVoxelMap check;
	FVoxelMipLevel *mip = &mVoxel->Mips[0];
	for (int x = 0; x < mip->SizeX; x++)
	{
		uint8_t *slabxoffs = &mip->GetSlabData(false)[mip->OffsetX[x]];
		short *xyoffs = &mip->OffsetXY[x * (mip->SizeY + 1)];
		for (int y = 0; y < mip->SizeY; y++)
		{
			kvxslab_t *voxptr = (kvxslab_t *)(slabxoffs + xyoffs[y]);
			kvxslab_t *voxend = (kvxslab_t *)(slabxoffs + xyoffs[y+1]);
			for (; voxptr < voxend; voxptr = (kvxslab_t *)((uint8_t *)voxptr + voxptr->zleng + 3))
			{
				MakeSlabPolys(x, y, voxptr, check);
			}
		}
	}
}

//===========================================================================
//
// 
//
//===========================================================================

void FVoxelModel::BuildVertexBuffer(FModelRenderer *renderer)
{
	if (!GetVertexBuffer(renderer->GetType()))
	{
		Initialize();

		auto vbuf = renderer->CreateVertexBuffer(true, true);
		SetVertexBuffer(renderer->GetType(), vbuf);

		FModelVertex *vertptr = vbuf->LockVertexBuffer(mVertices.Size());
		unsigned int *indxptr = vbuf->LockIndexBuffer(mIndices.Size());

		memcpy(vertptr, &mVertices[0], sizeof(FModelVertex)* mVertices.Size());
		memcpy(indxptr, &mIndices[0], sizeof(unsigned int)* mIndices.Size());

		vbuf->UnlockVertexBuffer();
		vbuf->UnlockIndexBuffer();
		mNumIndices = mIndices.Size();

		// delete our temporary buffers
		mVertices.Clear();
		mIndices.Clear();
		mVertices.ShrinkToFit();
		mIndices.ShrinkToFit();
	}
}


//===========================================================================
//
// for skin precaching
//
//===========================================================================

void FVoxelModel::AddSkins(uint8_t *hitlist, const FTextureID*)
{
	hitlist[mPalette.GetIndex()] |= FTextureManager::HIT_Flat;
}

//===========================================================================
//
// 
//
//===========================================================================

bool FVoxelModel::Load(const char * fn, int lumpnum, const char * buffer, int length)
{
	return false;	// not needed
}

//===========================================================================
//
// Voxels don't have frames so always return 0
//
//===========================================================================

int FVoxelModel::FindFrame(const char* name, bool nodefault)
{
	return nodefault ? FErr_Voxel : 0; // -2, not -1 because voxels are special.
}

//===========================================================================
//
// Voxels need aspect ratio correction according to the current map's setting
//
//===========================================================================

float FVoxelModel::getAspectFactor(float stretch)
{
	return stretch;
}

//===========================================================================
//
// Voxels never interpolate between frames, they only have one.
//
//===========================================================================

void FVoxelModel::RenderFrame(FModelRenderer *renderer, FGameTexture * skin, int frame, int frame2, double inter, FTranslationID translation, const FTextureID*, const TArray<VSMatrix>& boneData, int boneStartPosition)
{
	renderer->SetMaterial(skin, true, translation);
	renderer->SetupFrame(this, 0, 0, 0, {}, -1);
	renderer->DrawElements(mNumIndices, 0);
}
