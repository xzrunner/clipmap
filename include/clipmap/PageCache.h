#pragma once

#include <unirender2/typedef.h>
#include <textile/PageCache.h>

namespace ur2 { class Device; }

namespace clipmap
{

class TextureStack;

class PageCache : public textile::PageCache
{
public:
	PageCache(textile::PageLoader& loader, const textile::PageIndexer& indexer,
        TextureStack& tex_stack);
    virtual ~PageCache();

	virtual void LoadComplete(const ur2::Device& dev, const textile::Page& page, const uint8_t* data) override;

    ur2::TexturePtr QueryPageTex(const textile::Page& page) const;

private:
    ur2::TexturePtr CreatePageTex(const ur2::Device& dev, const uint8_t* data) const;

private:
    const textile::PageIndexer& m_indexer;
    TextureStack& m_tex_stack;

    std::unordered_map<int, ur2::TexturePtr> m_map_page2tex;

    uint8_t* m_page_buf = nullptr;

}; // PageCache


}