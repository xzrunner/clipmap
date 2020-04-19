#pragma once

#include "clipmap/PageCache.h"
#include "clipmap/TextureStack.h"

#include <textile/PageIndexer.h>
#include <textile/PageLoader.h>
#include <textile/VTexInfo.h>

#include <boost/noncopyable.hpp>

#include <string>

namespace clipmap
{

class Clipmap : private boost::noncopyable
{
public:
    Clipmap(const std::string& filepath, const textile::VTexInfo& info);

    void Init(const ur2::Device& dev);

    void Update(const ur2::Device& dev, ur2::Context& ctx,
        float scale, const sm::vec2& offset);
    void GetRegion(float& scale, sm::vec2& offset) const;

    void Draw(const ur2::Device& dev, ur2::Context& ctx,
        float screen_width, float screen_height) const;
    void DebugDraw(const ur2::Device& dev, ur2::Context& ctx) const;

    auto& GetAllLayers() const { return m_stack.GetAllLayers(); }
    size_t GetStackTexSize() const { return m_stack.GetTextureSize(); }

private:
    textile::VTexInfo m_info;

    textile::PageIndexer m_indexer;
    textile::PageLoader  m_loader;

    PageCache m_cache;

    TextureStack m_stack;

    sm::rect m_viewport = sm::rect(0, 0, 512, 512);

}; // Clipmap

}