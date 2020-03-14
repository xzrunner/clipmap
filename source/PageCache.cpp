#include "clipmap/PageCache.h"
#include "clipmap/TextureStack.h"

namespace clipmap
{

PageCache::PageCache(textile::PageLoader& loader, const textile::PageIndexer& indexer,
                     TextureStack& tex_stack)
    : textile::PageCache(loader, indexer)
    , m_tex_stack(tex_stack)
{
}

void PageCache::LoadComplete(const textile::Page& page, const uint8_t* data)
{
    m_lru.AddFront(page, 0, 0);
    m_tex_stack.Update(page, data);
}

}