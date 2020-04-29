#pragma once

#include <SM_Vector.h>
#include <SM_Rect.h>
#include <unirender/typedef.h>

#include <vector>
#include <functional>

namespace ur {
    class Device;
    class Context;
    struct RenderState;
    class ShaderProgram;
    class Framebuffer;
}
namespace textile { struct Page; struct VTexInfo; }

namespace clipmap
{

class PageCache;

class TextureStack
{
public:
    struct Layer
    {
        Layer() {
            region.MakeEmpty();
        }

        ur::TexturePtr tex = nullptr;
        sm::rect region;
    };

public:
    TextureStack(const textile::VTexInfo& vtex_info);

    void Init(const ur::Device& dev);

    void Update(const ur::Device& dev, ur::Context& ctx,
        PageCache& cache, const sm::rect& viewport,
        float scale, const sm::vec2& offset);
    void Draw(const ur::Device& dev, ur::Context& ctx,
        float screen_width, float screen_height) const;
    void DebugDraw(const ur::Device& dev, ur::Context& ctx) const;

    auto& GetAllLayers() const { return m_layers; }

    size_t GetTextureSize() const;

    void GetRegion(float& scale, sm::vec2& offset) const {
        scale = m_scale;
        offset = m_offset;
    }

    static sm::rect CalcUVRegion(int level, const Layer& layer);
    static size_t CalcMipmapLevel(int level_num, float scale);

private:
    void AddPage(const ur::Device& dev, ur::Context& ctx, const textile::Page& page,
        const ur::TexturePtr& tex, const sm::rect& region);

    void DrawTexture(const ur::Device& dev, ur::Context& ctx, const ur::RenderState& rs,
        float screen_width, float screen_height) const;
    void DrawDebug(const ur::Device& dev, ur::Context& ctx, const ur::RenderState& rs) const;

    void TraverseDiffPages(const std::vector<sm::rect>& regions, size_t start_layer,
        std::function<void(const textile::Page& page, const sm::rect& region)> cb);
    void TraversePages(const sm::rect& region, size_t start_layer,
        std::function<void(const textile::Page& page, const sm::rect& region)> cb);

private:
    const textile::VTexInfo& m_vtex_info;

    std::vector<Layer> m_layers;

    std::shared_ptr<ur::Framebuffer> m_fbo = nullptr;
    std::shared_ptr<ur::ShaderProgram> m_update_shader = nullptr;
    mutable std::shared_ptr<ur::ShaderProgram> m_final_shader = nullptr;

    float    m_scale = 0;
    sm::vec2 m_offset;

}; // TextureStack

}