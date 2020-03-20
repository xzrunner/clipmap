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
private:
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
    ~TextureStack();

    void Update(PageCache& cache, const sm::rect& viewport,
        float scale, const sm::vec2& offset);
    void Draw(float screen_width, float screen_height) const;
    void DebugDraw() const;

    auto& GetAllLayers() const { return m_layers; }

    size_t GetTextureSize() const;

private:
    void AddPage(const textile::Page& page, const ur::TexturePtr& tex,
        const sm::rect& region);

    void DrawTexture(float screen_width, float screen_height) const;
    void DrawDebug() const;

    void TraverseDiffPages(const sm::rect& region, size_t start_layer,
        std::function<void(const textile::Page& page, const sm::rect& region)> cb);
    void TraversePages(const sm::rect& region, size_t start_layer,
        std::function<void(const textile::Page& page, const sm::rect& region)> cb);

    size_t CalcMipmapLevel(float scale) const;

private:
    const textile::VTexInfo& m_vtex_info;

    std::vector<Layer> m_layers;

    int m_fbo = 0;
    std::shared_ptr<ur::Shader> m_update_shader = nullptr;
    mutable std::shared_ptr<ur::Shader> m_final_shader = nullptr;

    float    m_scale = 1.0f;
    sm::vec2 m_offset;

}; // TextureStack

}