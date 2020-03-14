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

    void Draw(float scale, const sm::vec2& offset,
        float screen_width, float screen_height);

private:
    void Update();

private:
    textile::VTexInfo m_info;

    textile::PageIndexer m_indexer;
    textile::PageLoader  m_loader;

    PageCache m_cache;

    TextureStack m_stack;

}; // Clipmap

}