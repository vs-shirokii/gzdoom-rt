//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//

#include <stdlib.h>
#include "templates.h"
#include "doomdef.h"
#include "m_bbox.h"
#include "i_system.h"
#include "p_lnspec.h"
#include "p_setup.h"
#include "swrenderer/drawers/r_draw.h"
#include "swrenderer/plane/r_visibleplane.h"
#include "a_sharedglobal.h"
#include "g_level.h"
#include "p_effect.h"
#include "doomstat.h"
#include "r_state.h"
#include "v_palette.h"
#include "r_sky.h"
#include "po_man.h"
#include "r_data/colormaps.h"
#include "r_memory.h"

namespace swrenderer
{
	short *openings;

	namespace
	{
		size_t maxopenings;
		ptrdiff_t lastopening;
	}

	ptrdiff_t R_NewOpening(ptrdiff_t len)
	{
		ptrdiff_t res = lastopening;
		len = (len + 1) & ~1;	// only return DWORD aligned addresses because some code stores fixed_t's and floats in openings... 
		lastopening += len;
		if ((size_t)lastopening > maxopenings)
		{
			do
				maxopenings = maxopenings ? maxopenings * 2 : 16384;
			while ((size_t)lastopening > maxopenings);
			openings = (short *)M_Realloc(openings, maxopenings * sizeof(*openings));
			DPrintf(DMSG_NOTIFY, "MaxOpenings increased to %zu\n", maxopenings);
		}
		return res;
	}

	void R_FreeOpenings()
	{
		lastopening = 0;
	}

	void R_DeinitOpenings()
	{
		if (openings != nullptr)
		{
			M_Free(openings);
			openings = nullptr;
		}
	}

	/////////////////////////////////////////////////////////////////////////
	
	void *RenderMemory::AllocBytes(int size)
	{
		size = (size + 15) / 16 * 16; // 16-byte align
		
		if (UsedBlocks.empty() || UsedBlocks.back()->Position + size > BlockSize)
		{
			if (!FreeBlocks.empty())
			{
				auto block = std::move(FreeBlocks.back());
				block->Position = 0;
				FreeBlocks.pop_back();
				UsedBlocks.push_back(std::move(block));
			}
			else
			{
				UsedBlocks.push_back(std::make_unique<MemoryBlock>());
			}
		}
		
		auto &block = UsedBlocks.back();
		void *data = block->Data + block->Position;
		block->Position += size;

		return data;
	}
	
	void RenderMemory::Clear()
	{
		while (!UsedBlocks.empty())
		{
			auto block = std::move(UsedBlocks.back());
			UsedBlocks.pop_back();
			FreeBlocks.push_back(std::move(block));
		}
	}
	
	std::vector<std::unique_ptr<RenderMemory::MemoryBlock>> RenderMemory::UsedBlocks;
	std::vector<std::unique_ptr<RenderMemory::MemoryBlock>> RenderMemory::FreeBlocks;
}
