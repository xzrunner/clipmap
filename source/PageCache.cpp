#include "clipmap/PageCache.h"
#include "clipmap/TextureStack.h"

#include <unirender2/Device.h>
#include <unirender2/TextureDescription.h>
#include <textile/PageIndexer.h>
#include <textile/PageLoader.h>

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
    m_page_buf = new uint8_t[info.tile_size * info.tile_size * info.channels];
}

PageCache::~PageCache()
{
    delete[] m_page_buf;
}

void PageCache::LoadComplete(const ur2::Device& dev, const textile::Page& page, const uint8_t* data)
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
        CreatePageTex(dev, data)
    });
}

ur2::TexturePtr PageCache::QueryPageTex(const textile::Page& page) const
{
    auto itr = m_map_page2tex.find(m_indexer.CalcPageIdx(page));
    return itr == m_map_page2tex.end() ? nullptr : itr->second;
}

ur2::TexturePtr PageCache::CreatePageTex(const ur2::Device& dev, const uint8_t* data) const
{
    auto& info = m_loader.GetVTexInfo();
    assert(info.bytes == 1);
    auto dst = m_page_buf;
    memset(dst, 0xff, info.tile_size * info.tile_size * info.channels);
    const uint8_t* src = data;
    for (size_t y = 0; y < info.tile_size; ++y) {
        for (size_t x = 0; x < info.tile_size; ++x) {
            memset(&dst[(y * info.tile_size + x) * info.channels], 0xff, info.channels);
            for (size_t c = 0; c < info.channels; ++c) {
                auto s_idx = (y * info.tile_size + x) * info.channels + c;
                auto d_idx = (y * info.tile_size + x) * info.channels + c;
                dst[d_idx] = src[s_idx];
            }
        }
    }

    //auto tex = std::make_shared<ur::Texture>();

    ur2::TextureDescription desc;
    desc.target = ur2::TextureTarget::Texture2D;
    desc.width = info.tile_size;
    desc.height = info.tile_size;
    switch (info.channels)
    {
    case 1:
        desc.format = ur2::TextureFormat::RED;
        break;
    case 3:
        desc.format = ur2::TextureFormat::RGB;
        break;
    case 4:
        desc.format = ur2::TextureFormat::RGBA8;
        break;
    default:
        assert(0);
    }

    return dev.CreateTexture(desc, m_page_buf);
}

}