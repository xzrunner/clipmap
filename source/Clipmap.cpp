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

void Clipmap::Init(const ur2::Device& dev)
{
    m_stack.Init(dev);
}

void Clipmap::Update(const ur2::Device& dev, ur2::Context& ctx,
                     float scale, const sm::vec2& offset)
{
    m_stack.Update(dev, ctx, m_cache, m_viewport, scale, offset);
}

void Clipmap::GetRegion(float& scale, sm::vec2& offset) const
{
    m_stack.GetRegion(scale, offset);
}

void Clipmap::Draw(const ur2::Device& dev, ur2::Context& ctx,
                   float screen_width, float screen_height) const
{
    m_stack.Draw(dev, ctx, screen_width, screen_height);
}

void Clipmap::DebugDraw(const ur2::Device& dev, ur2::Context& ctx) const
{
    m_stack.DebugDraw(dev, ctx);
}

}