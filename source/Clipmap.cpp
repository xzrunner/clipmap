#include "clipmap/Clipmap.h"

#include <SM_Rect.h>
#include <unirender/Texture.h>
#include <unirender/Blackboard.h>
#include <unirender/RenderContext.h>

#include <algorithm>

namespace
{

sm::rect REGION(0, 0, 512, 512);

}

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

void Clipmap::Draw(float scale, const sm::vec2& offset,
                   float screen_width, float screen_height)
{
    auto& rc = ur::Blackboard::Instance()->GetRenderContext();
    rc.SetZTest(ur::DEPTH_DISABLE);
    rc.SetCullMode(ur::CULL_DISABLE);

    m_stack.Draw(scale, offset, screen_width, screen_height);

    rc.SetZTest(ur::DEPTH_LESS_EQUAL);
    rc.SetCullMode(ur::CULL_BACK);

    Update();
}

void Clipmap::Update()
{
    //m_cache.Request(textile::Page(0, 0, 4));
    //m_cache.Request(textile::Page(1, 0, 4));
    //m_cache.Request(textile::Page(2, 0, 4));
    //m_cache.Request(textile::Page(0, 1, 4));
    //m_cache.Request(textile::Page(1, 1, 4));
    //m_cache.Request(textile::Page(2, 1, 4));

    //m_cache.Request(textile::Page(0, 0, 3));
    //m_cache.Request(textile::Page(1, 0, 3));
    //m_cache.Request(textile::Page(2, 0, 3));
    //m_cache.Request(textile::Page(3, 0, 3));
    //m_cache.Request(textile::Page(4, 0, 3));
    //m_cache.Request(textile::Page(5, 0, 3));
    //m_cache.Request(textile::Page(0, 1, 3));
    //m_cache.Request(textile::Page(1, 1, 3));
    //m_cache.Request(textile::Page(2, 1, 3));
    //m_cache.Request(textile::Page(3, 1, 3));
    //m_cache.Request(textile::Page(4, 1, 3));
    //m_cache.Request(textile::Page(5, 1, 3));
    //m_cache.Request(textile::Page(0, 2, 3));
    //m_cache.Request(textile::Page(1, 2, 3));
    //m_cache.Request(textile::Page(2, 2, 3));
    //m_cache.Request(textile::Page(3, 2, 3));
    //m_cache.Request(textile::Page(4, 2, 3));
    //m_cache.Request(textile::Page(5, 2, 3));


}

}