#include "clipmap/PageCache.h"
#include "clipmap/TextureStack.h"

#include <textile/PageIndexer.h>
#include <textile/PageLoader.h>
#include <unirender/Blackboard.h>
#include <unirender/RenderContext.h>

namespace
{

const size_t CAPACITY = 256;

}

namespace clipmap
{

PageCache::PageCache(textile::PageLoader& loader, const textile::PageIndexer& indexer,
                     TextureStack& tex_stack)
    : textile::PageCache(loader, indexer)
    , m_indexer(indexer)
    , m_tex_stack(tex_stack)
{
    auto& info = loader.GetVTexInfo();
    m_page_buf = new uint8_t[info.tile_size * info.tile_size * 4];
}

PageCache::~PageCache()
{
    delete[] m_page_buf;
}

void PageCache::LoadComplete(const textile::Page& page, const uint8_t* data)
{
    if (m_lru.Size() == CAPACITY)
    {
        auto end = m_lru.GetListEnd();
        assert(end);
        m_map_page2tex.erase(m_indexer.CalcPageIdx(end->page));
        m_lru.RemoveBack();
    }

    m_lru.AddFront(page, 0, 0);

    m_map_page2tex.insert({ 
        m_indexer.CalcPageIdx(page), 
        CreatePageTex(data) 
    });
}

ur::TexturePtr PageCache::QueryPageTex(const textile::Page& page) const
{
    auto itr = m_map_page2tex.find(m_indexer.CalcPageIdx(page));
    return itr == m_map_page2tex.end() ? nullptr : itr->second;
}

ur::TexturePtr PageCache::CreatePageTex(const uint8_t* data) const
{
    auto& info = m_loader.GetVTexInfo();
    auto dst = m_page_buf;
    memset(dst, 0xff, info.tile_size * info.tile_size * 4);
    const uint8_t* src = data;
    for (size_t y = 0; y < info.tile_size; ++y) {
        for (size_t x = 0; x < info.tile_size; ++x) {
            memset(&dst[(y * info.tile_size + x) * 4], 0xff, 4);
            for (size_t c = 0; c < info.channels; ++c) {
                auto s_idx = ((y * info.tile_size + x) * info.channels + c) * info.bytes;
                auto d_idx = (y * info.tile_size + x) * 4 + c;
                dst[d_idx] = src[s_idx];
            }
        }
    }

    auto tex = std::make_shared<ur::Texture>();
    auto& rc = ur::Blackboard::Instance()->GetRenderContext();
    tex->Upload(&rc, info.tile_size, info.tile_size, ur::TEXTURE_RGBA8, m_page_buf);
    return tex;
}

}