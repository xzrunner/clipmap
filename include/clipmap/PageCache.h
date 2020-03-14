#pragma once

#include <unirender/Texture.h>
#include <textile/PageCache.h>

namespace clipmap
{

class TextureStack;

class PageCache : public textile::PageCache
{
public:
	PageCache(textile::PageLoader& loader, const textile::PageIndexer& indexer,
        TextureStack& tex_stack);

	virtual void LoadComplete(const textile::Page& page, const uint8_t* data) override;

private:
    TextureStack& m_tex_stack;

}; // PageCache


}