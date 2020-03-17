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
    virtual ~PageCache();

	virtual void LoadComplete(const textile::Page& page, const uint8_t* data) override;

    ur::TexturePtr QueryPageTex(const textile::Page& page) const;

private:
    ur::TexturePtr CreatePageTex(const uint8_t* data) const;

private:
    const textile::PageIndexer& m_indexer;
    TextureStack& m_tex_stack;

    std::unordered_map<int, ur::TexturePtr> m_map_page2tex;

    uint8_t* m_page_buf = nullptr;

}; // PageCache


}