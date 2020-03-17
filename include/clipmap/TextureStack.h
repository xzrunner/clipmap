#pragma once

#include <SM_Vector.h>
#include <SM_Rect.h>
#include <unirender/Texture.h>

#include <vector>
#include <functional>

namespace ur { class Shader; }
namespace textile { struct Page; struct VTexInfo; }

namespace clipmap
{

class PageCache;

class TextureStack
{
public:
    TextureStack(const textile::VTexInfo& vtex_info);
    ~TextureStack();

    void Update(PageCache& cache, const sm::rect& viewport,
        float scale, const sm::vec2& offset);
    void Draw(float scale, const sm::vec2& offset,
        float screen_width, float screen_height) const;

private:
    void AddPage(const textile::Page& page, const ur::TexturePtr& tex,
        const sm::rect& region);

    void DrawTexture(float scale, const sm::vec2& offset,
        float screen_width, float screen_height) const;
    void DrawDebug(float scale, const sm::vec2& offset) const;

    void TraverseDiffPages(const sm::rect& old_r, const sm::rect& new_r,
        std::function<void(const textile::Page& page, const sm::rect& region)> cb);
    void TraversePages(const sm::rect& region,
        std::function<void(const textile::Page& page, const sm::rect& region)> cb);

private:
    struct Layer
    {
        ur::TexturePtr tex = nullptr;
    };

private:
    const textile::VTexInfo& m_vtex_info;

    std::vector<Layer> m_layers;

    int m_fbo = 0;
    std::shared_ptr<ur::Shader> m_update_shader = nullptr;
    mutable std::shared_ptr<ur::Shader> m_final_shader = nullptr;

    sm::rect m_region;

}; // TextureStack

}