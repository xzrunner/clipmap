#include "clipmap/TextureStack.h"
#include "clipmap/PageCache.h"

#include <SM_Calc.h>
#include <tessellation/Painter.h>
#include <unirender/Blackboard.h>
#include <unirender/RenderContext.h>
#include <unirender/RenderTarget.h>
#include <unirender/Shader.h>
#include <painting2/RenderSystem.h>
#include <textile/VTexInfo.h>
#include <textile/Page.h>

#include <assert.h>

namespace
{

const size_t TEX_SIZE = 512;

const char* update_vs = R"(

#version 330 core
layout (location = 0) in vec4 position;

out VS_OUT {
    vec2 texcoord;
} vs_out;

uniform float u_page_scale;
uniform vec2  u_page_pos;

uniform vec2 u_update_size;
uniform vec2 u_update_offset;

void main()
{
    vec2 uv = position.xy;
	vs_out.texcoord = uv * u_update_size + u_update_offset;

    vec2 pos = uv * u_update_size + u_update_offset;
    pos = pos * u_page_scale + u_page_pos;
    pos = pos * 2 - 1;
	gl_Position = vec4(pos, 0, 1);
}

)";

const char* update_fs = R"(

#version 330 core
out vec4 FragColor;

in VS_OUT {
    vec2 texcoord;
} fs_in;

uniform sampler2D page_map;

void main(void){
    FragColor = texture2D(page_map, fs_in.texcoord);
}

)";

const char* final_vs = R"(

#version 330 core
layout (location = 0) in vec4 position;

out VS_OUT {
    vec2 texcoord;
} vs_out;

uniform mat4 u_view_mat;

uniform float u_scale;
uniform vec2 u_offset;

void main()
{
    vec2 uv = position.xy;
    vec2 pos = uv * 2 - 1;

	vs_out.texcoord = uv * u_scale + u_offset;
	gl_Position = u_view_mat * vec4(pos, 0, 1);
}

)";

const char* final_fs = R"(

#version 330 core
out vec4 FragColor;

in VS_OUT {
    vec2 texcoord;
} fs_in;

uniform sampler2D finer_tex;
uniform sampler2D coarser_tex;

void main(void){
    FragColor = texture2D(finer_tex, fs_in.texcoord);
}

)";

}

namespace clipmap
{

TextureStack::TextureStack(const textile::VTexInfo& info)
    : m_vtex_info(info)
{
    auto mip_count = static_cast<int>(std::log2(std::min(info.PageTableWidth(), info.PageTableHeight()))) + 1;
    m_layers.resize(mip_count);

    m_region.MakeEmpty();
}

TextureStack::~TextureStack()
{
    if (m_fbo != 0) {
        auto& rc = ur::Blackboard::Instance()->GetRenderContext();
        rc.ReleaseRenderTarget(m_fbo);
    }
}

void TextureStack::Update(PageCache& cache, const sm::rect& viewport,
                          float scale, const sm::vec2& offset)
{
    auto& rc = ur::Blackboard::Instance()->GetRenderContext();

    // init textures
    assert(!m_layers.empty());
    if (!m_layers[0].tex)
    {
        uint8_t* filling = new uint8_t[TEX_SIZE * TEX_SIZE * 4];
        memset(filling, 0xaa, TEX_SIZE * TEX_SIZE * 4);

        auto w = m_vtex_info.vtex_width;
        auto h = m_vtex_info.vtex_height;
        for (auto& layer : m_layers)
        {
            layer.tex = std::make_unique<ur::Texture>();
            layer.tex->Upload(&rc, TEX_SIZE, TEX_SIZE, ur::TEXTURE_RGBA8, filling);
        }

        delete[] filling;
    }

    // init shader
    if (!m_update_shader)
    {
        std::vector<std::string> textures;
        textures.push_back("page_map");

        std::vector<ur::VertexAttrib> layout;

        m_update_shader = std::make_shared<ur::Shader>(&rc, update_vs, update_fs, textures, layout);
    }

    sm::rect region = viewport;
    region.Translate(offset);

    // load pages
    TraverseDiffPages(m_region, region, [&cache](const textile::Page& page, const sm::rect& r) {
        cache.Request(page);
    });

    // update levels
    TraverseDiffPages(m_region, region, [&](const textile::Page& page, const sm::rect& r) {
        auto tex = cache.QueryPageTex(page);
        assert(tex);
        AddPage(page, tex, r);
    });

    m_region = region;
}

void TextureStack::Draw(float scale, const sm::vec2& offset,
                        float screen_width, float screen_height) const
{
    assert(!m_layers.empty());
    if (!m_layers[0].tex) {
        return;
    }

    auto& rc = ur::Blackboard::Instance()->GetRenderContext();
    rc.SetZTest(ur::DEPTH_DISABLE);
    rc.SetCullMode(ur::CULL_DISABLE);

    DrawTexture(scale, offset, screen_width, screen_height);
    DrawDebug(scale, offset);

    rc.SetZTest(ur::DEPTH_LESS_EQUAL);
    rc.SetCullMode(ur::CULL_BACK);
}

void TextureStack::AddPage(const textile::Page& page, const ur::TexturePtr& tex,
                           const sm::rect& region)
{
    if (!region.IsValid() || region.Width() == 0 || region.Height() == 0) {
        return;
    }

    assert(page.mip < static_cast<int>(m_layers.size()));
    auto& layer = m_layers[page.mip];
    auto tile_sz = m_vtex_info.tile_size;

    auto& rc = ur::Blackboard::Instance()->GetRenderContext();
    if (m_fbo == 0) {
        m_fbo = rc.CreateRenderTarget(0);
    }

    int vp_x, vp_y, vp_w, vp_h;
    rc.GetViewport(vp_x, vp_y, vp_w, vp_h);

    rc.BindRenderTarget(m_fbo);
    rc.BindRenderTargetTex(layer.tex->TexID(), ur::ATTACHMENT_COLOR0);
    rc.SetViewport(0, 0, TEX_SIZE, TEX_SIZE);

    rc.SetZTest(ur::DEPTH_DISABLE);
    rc.SetCullMode(ur::CULL_DISABLE);

    m_update_shader->SetUsedTextures({ tex->TexID() });
    m_update_shader->Use();

    m_update_shader->SetFloat("u_page_scale", static_cast<float>(m_vtex_info.tile_size) / TEX_SIZE);
    const int tile_n = TEX_SIZE / tile_sz;
    sm::vec2 offset(
        static_cast<float>(page.x % tile_n) * tile_sz / TEX_SIZE,
        static_cast<float>(page.y % tile_n) * tile_sz / TEX_SIZE
    );
    offset.x += tile_sz / TEX_SIZE;
    offset.y += tile_sz / TEX_SIZE;
    m_update_shader->SetVec2("u_page_pos", offset.xy);

    const float layer_tile_sz = static_cast<float>(tile_sz * std::pow(2.0f, page.mip));

    const sm::vec2 update_size(region.Width(), region.Height());
    m_update_shader->SetVec2("u_update_size", (update_size / layer_tile_sz).xy);

    sm::vec2 update_offset;
    update_offset.x = region.xmin - static_cast<int>(region.xmin / layer_tile_sz) * layer_tile_sz;
    update_offset.y = region.ymin - static_cast<int>(region.ymin / layer_tile_sz) * layer_tile_sz;
    m_update_shader->SetVec2("u_update_offset", (update_offset / layer_tile_sz).xy);

    rc.RenderQuad(ur::RenderContext::VertLayout::VL_POS, true);

    rc.BindShader(0);

    rc.UnbindRenderTarget();
    rc.SetViewport(vp_x, vp_y, vp_w, vp_h);

    rc.SetZTest(ur::DEPTH_LESS_EQUAL);
    rc.SetCullMode(ur::CULL_BACK);
}

void TextureStack::DrawTexture(float scale, const sm::vec2& offset,
                               float screen_width, float screen_height) const
{
    auto& rc = ur::Blackboard::Instance()->GetRenderContext();
    if (!m_final_shader)
    {
        std::vector<std::string> textures;
        textures.push_back("finer_tex");
        textures.push_back("coarser_tex");

        std::vector<ur::VertexAttrib> layout;

        m_final_shader = std::make_shared<ur::Shader>(&rc, final_vs, final_fs, textures, layout);
    }

    float layer = log(1.0f / scale) / log(0.5f);
    layer = std::min(static_cast<float>(m_layers.size() - 1), std::max(0.0f, layer));
    const uint32_t finer_layer = static_cast<uint32_t>(layer);
    const uint32_t coarser_layer = finer_layer < m_layers.size() - 1 ? finer_layer + 1 : finer_layer;
    m_final_shader->SetUsedTextures({
        m_layers[finer_layer].tex->TexID(),
        m_layers[coarser_layer].tex->TexID()
    });
    m_final_shader->Use();

    sm::mat4 view_mat = sm::mat4::Scaled(TEX_SIZE / screen_width, TEX_SIZE / screen_height, 1);
    m_final_shader->SetMat4("u_view_mat", view_mat.x);

    m_final_shader->SetFloat("u_scale", scale);
    m_final_shader->SetVec2("u_offset", (offset / 500).xy);

    rc.RenderQuad(ur::RenderContext::VertLayout::VL_POS, true);

    rc.BindShader(0);
}

void TextureStack::DrawDebug(float scale, const sm::vec2& offset) const
{
    tess::Painter pt;

    // region
    sm::Matrix2D mt;
    mt.Scale(scale, scale);
    mt.Translate(offset.x, offset.y);

    const float h_sz = TEX_SIZE * 0.5f;
    pt.AddRect(mt * sm::vec2(-h_sz, -h_sz), mt * sm::vec2(h_sz, h_sz), 0xff0000ff);

    // layers
    const float sx = -400;
    const float sy = -350;
    const float size  = 170;
    const float space = 4;
    for (size_t i = 0, n = m_layers.size(); i < n; ++i)
    {
        const float x = sx + (size + space) * i;
        sm::rect region(x, sy, x + size, sy + size);
        pt2::RenderSystem::DrawTexture(*m_layers[i].tex, region, mt, false);

        pt.AddRect(mt * sm::vec2(region.xmin, region.ymin), mt * sm::vec2(region.xmax, region.ymax), 0xff0000ff);
    }

    pt2::RenderSystem::DrawPainter(pt);
}

void TextureStack::TraverseDiffPages(const sm::rect& old_r, const sm::rect& new_r,
                                     std::function<void(const textile::Page& page, const sm::rect& region)> cb)
{
    if (sm::is_rect_contain_rect(old_r, new_r)) {
        return;
    }

    if (!old_r.IsValid() || !sm::is_rect_intersect_rect(old_r, new_r)) {
        return TraversePages(new_r, cb);
    }

    if (sm::is_rect_contain_rect(new_r, old_r))
    {
        TraversePages(sm::rect(new_r.xmin, new_r.ymin, new_r.xmax, old_r.ymin), cb);
        TraversePages(sm::rect(new_r.xmin, old_r.ymax, new_r.xmax, new_r.ymax), cb);
        TraversePages(sm::rect(new_r.xmin, old_r.ymin, old_r.xmin, old_r.ymax), cb);
        TraversePages(sm::rect(old_r.xmax, old_r.ymin, new_r.xmax, old_r.ymax), cb);
    }
    else if (new_r.xmin <= old_r.xmin && new_r.ymin <= old_r.ymin)
    {
        TraversePages(sm::rect(new_r.xmin, new_r.ymin, old_r.xmin, new_r.ymax), cb);
        TraversePages(sm::rect(old_r.xmin, new_r.ymin, new_r.xmax, old_r.ymin), cb);
    }
    else if (new_r.xmin <= old_r.xmin && new_r.ymax >= old_r.ymax)
    {
        TraversePages(sm::rect(new_r.xmin, new_r.ymin, old_r.xmin, new_r.ymax), cb);
        TraversePages(sm::rect(old_r.xmin, old_r.ymax, new_r.xmax, new_r.ymax), cb);
    }
    else if (new_r.xmax >= old_r.xmax && new_r.ymax >= old_r.ymax)
    {
        TraversePages(sm::rect(new_r.xmin, old_r.ymax, new_r.xmax, new_r.ymax), cb);
        TraversePages(sm::rect(old_r.xmax, new_r.ymin, new_r.xmax, old_r.ymax), cb);
    }
    else if (new_r.xmax >= old_r.xmax && new_r.ymin <= old_r.ymin)
    {
        TraversePages(sm::rect(new_r.xmin, new_r.ymin, new_r.xmax, old_r.ymin), cb);
        TraversePages(sm::rect(old_r.xmax, old_r.ymin, new_r.xmax, new_r.ymax), cb);
    }
    else
    {
        assert(0);
    }
}

void TextureStack::TraversePages(const sm::rect& region,
                                 std::function<void(const textile::Page& page, const sm::rect& region)> cb)
{
    if (!region.IsValid() || region.Width() == 0 || region.Height() == 0) {
        return;
    }

    float layer_w = static_cast<float>(m_vtex_info.vtex_width);
    float layer_h = static_cast<float>(m_vtex_info.vtex_height);
    float tile_sz = static_cast<float>(m_vtex_info.tile_size);
    for (size_t i = 0, n = m_layers.size(); i < n; ++i)
    {
        //sm::rect r;
        //r.xmin = std::max(0.0f,    region.xmin);
        //r.xmax = std::min(layer_w, region.xmax);
        //r.ymin = std::max(0.0f,    region.ymin);
        //r.ymax = std::min(layer_h, region.ymax);
        //if (r.xmax <= r.xmin || r.ymax <= r.ymin) {
        //    continue;
        //}

        sm::rect r = region;

        int x_begin = static_cast<int>(std::floor(r.xmin / tile_sz));
        int x_end   = static_cast<int>(std::floor(r.xmax / tile_sz));
        int y_begin = static_cast<int>(std::floor(r.ymin / tile_sz));
        int y_end   = static_cast<int>(std::floor(r.ymax / tile_sz));

        for (int x = x_begin; x <= x_end; ++x)
        {
            for (int y = y_begin; y <= y_end; ++y)
            {
                sm::rect r;
                r.xmin = std::max(region.xmin, x * tile_sz);
                r.xmax = std::min(region.xmax, x * tile_sz + tile_sz);
                r.ymin = std::max(region.ymin, y * tile_sz);
                r.ymax = std::min(region.ymax, y * tile_sz + tile_sz);
                cb(textile::Page(x, y, i), r);
            }
        }

        layer_w *= 2;
        layer_h *= 2;
        tile_sz *= 2;
    }
}

}