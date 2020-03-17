#include "clipmap/Clipmap.h"

namespace clipmap
{

Clipmap::Clipmap(const std::string& filepath, const textile::VTexInfo& info)
    : m_info(info)
    , m_indexer(m_info)
    , m_loader(filepath, m_indexer)
    , m_cache(m_loader, m_indexer, m_stack)
    , m_stack(m_info)
{
}

void Clipmap::Update(float scale, const sm::vec2& offset)
{
    m_stack.Update(m_cache, m_viewport, scale, offset);
}

void Clipmap::Draw(float scale, const sm::vec2& offset,
                   float screen_width, float screen_height)
{
    m_stack.Draw(scale, offset, screen_width, screen_height);
}

}