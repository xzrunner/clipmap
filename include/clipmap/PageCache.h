#pragma once

#include <unirender/typedef.h>
#include <textile/PageCache.h>

namespace ur { class Device; }

namespace clipmap
{

class TextureStack;

class PageCache : public textile::PageCache
{
public:
	PageCache(textile::PageLoader& loader, const textile::PageIndexer& indexer,
        TextureStack& tex_stack);
    virtual ~PageCache();

	virtual void LoadComplete(const ur::Device& dev, const textile::Page& page, const uint8_t* data) override;

    ur::TexturePtr QueryPageTex(const textile::Page& page) const;

private:
    ur::TexturePtr CreatePageTex(const ur::Device& dev, const uint8_t* data) const;

private:
    const textile::PageIndexer& m_indexer;
    TextureStack& m_tex_stack;

    std::unordered_map<int, ur::TexturePtr> m_map_page2tex;

    uint8_t* m_page_buf = nullptr;

}; // PageCache


}