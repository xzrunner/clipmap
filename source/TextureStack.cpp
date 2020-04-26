#include "clipmap/TextureStack.h"
#include "clipmap/PageCache.h"

#include <SM_Calc.h>
#include <tessellation/Painter.h>
#include <unirender2/Device.h>
#include <unirender2/TextureDescription.h>
#include <unirender2/Framebuffer.h>
#include <unirender2/ShaderProgram.h>
#include <unirender2/RenderState.h>
#include <unirender2/Context.h>
#include <unirender2/Uniform.h>
#include <unirender2/DrawState.h>
#include <unirender2/Factory.h>
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
}

void TextureStack::Init(const ur2::Device& dev)
{
    if (m_layers[0].tex) {
        return;
    }

    // init textures
    uint8_t* filling = new uint8_t[TEX_SIZE * TEX_SIZE * 4];
    memset(filling, 0xaa, TEX_SIZE * TEX_SIZE * 4);

    auto w = m_vtex_info.vtex_width;
    auto h = m_vtex_info.vtex_height;
    for (auto& layer : m_layers)
    {
        ur2::TextureDescription desc;
        desc.target = ur2::TextureTarget::Texture2D;
        desc.width = TEX_SIZE;
        desc.height = TEX_SIZE;
        desc.format = ur2::TextureFormat::RGBA8;
        layer.tex = dev.CreateTexture(desc, filling);
    }

    delete[] filling;

    // init shader
    if (!m_update_shader)
    {
        //std::vector<std::string> textures;
        //textures.push_back("page_map");

        m_update_shader = dev.CreateShaderProgram(update_vs, update_fs);
    }
}

void TextureStack::Update(const ur2::Device& dev, ur2::Context& ctx,
                          PageCache& cache, const sm::rect& viewport,
                          float scale, const sm::vec2& offset)
{
    // need init before
    if (!m_layers[0].tex || !m_update_shader) {
        return;
    }

    if (m_scale == scale && m_offset == offset) {
        return;
    }

    m_scale = std::min(std::min(m_vtex_info.vtex_width / viewport.Width(), m_vtex_info.vtex_height / viewport.Height()), scale);
    m_offset.x = std::max(0.0f, std::min(offset.x, m_vtex_info.vtex_width - viewport.Width() * scale));
    m_offset.y = std::max(0.0f, std::min(offset.y, m_vtex_info.vtex_height - viewport.Height() * scale));

    sm::rect region = viewport;
    region.Scale(sm::vec2(m_scale, m_scale));
    region.Translate(m_offset);

    const int mipmap_level = CalcMipmapLevel(m_layers.size(), m_scale);

    std::vector<sm::rect> regions;
    regions.reserve(m_layers.size() - mipmap_level);
    auto next_r = region;
    for (size_t i = mipmap_level, n = m_layers.size(); i < n; ++i)
    {
        regions.push_back(next_r);

        auto c = next_r.Center();
        auto sz = next_r.Size();
        next_r.Scale(sm::vec2(2, 2));
        next_r.Translate(next_r.Center() - c);
    }

    // load pages
    TraverseDiffPages(regions, mipmap_level, [&](const textile::Page& page, const sm::rect& r) {
        cache.Request(dev, page);
    });

    // update levels
    TraverseDiffPages(regions, mipmap_level, [&](const textile::Page& page, const sm::rect& r) {
        auto tex = cache.QueryPageTex(page);
        assert(tex);
        AddPage(dev, ctx, page, tex, r);
    });

    assert(regions.size() == m_layers.size() - mipmap_level);
    for (size_t i = mipmap_level, n = m_layers.size(); i < n; ++i) {
        m_layers[i].region = regions[i - mipmap_level];
    }
}

void TextureStack::Draw(const ur2::Device& dev, ur2::Context& ctx,
                        float screen_width, float screen_height) const
{
    assert(!m_layers.empty());
    if (!m_layers[0].tex) {
        return;
    }

    auto rs = ur2::DefaultRenderState2D();
    DrawTexture(dev, ctx, rs, screen_width, screen_height);
    DrawDebug(dev, ctx, rs);
}

void TextureStack::DebugDraw(const ur2::Device& dev, ur2::Context& ctx) const
{
    assert(!m_layers.empty());
    if (!m_layers[0].tex) {
        return;
    }

    auto rs = ur2::DefaultRenderState2D();
    DrawDebug(dev, ctx, rs);
}

size_t TextureStack::GetTextureSize() const
{
    return TEX_SIZE;
}

sm::rect TextureStack::CalcUVRegion(int level, const Layer& layer)
{
    const auto scale = static_cast<float>(1.0 / std::pow(2, level) / TEX_SIZE);
    auto r = layer.region;
    r.Scale(sm::vec2(scale, scale));
    return r;
}

size_t TextureStack::CalcMipmapLevel(int level_num, float scale)
{
    float level = log(scale) / log(2.0f);
    level = std::min(static_cast<float>(level_num - 1), std::max(0.0f, level));
    return static_cast<size_t>(std::ceil(level));
}

void TextureStack::AddPage(const ur2::Device& dev, ur2::Context& ctx, const textile::Page& page,
                           const ur2::TexturePtr& tex, const sm::rect& region)
{
    if (!region.IsValid() || region.Width() == 0 || region.Height() == 0) {
        return;
    }

    assert(page.mip < static_cast<int>(m_layers.size()));
    auto& layer = m_layers[page.mip];
    auto tile_sz = m_vtex_info.tile_size;

    if (!m_fbo) {
        dev.CreateFramebuffer();
    }

    ctx.SetViewport(0, 0, TEX_SIZE, TEX_SIZE);

    ctx.SetFramebuffer(m_fbo);
    m_fbo->SetAttachment(ur2::AttachmentType::Color0, ur2::TextureTarget::Texture2D, layer.tex, nullptr);

    ctx.SetTexture(m_update_shader->QueryTexSlot("page_map"), tex);

    auto u_page_scale = m_update_shader->QueryUniform("u_page_scale");
    assert(u_page_scale);
    float page_scale = static_cast<float>(m_vtex_info.tile_size) / TEX_SIZE;
    u_page_scale->SetValue(&page_scale, 1);

    const int tile_n = TEX_SIZE / tile_sz;
    sm::vec2 offset(
        static_cast<float>(page.x % tile_n) * tile_sz / TEX_SIZE,
        static_cast<float>(page.y % tile_n) * tile_sz / TEX_SIZE
    );
    offset.x += tile_sz / TEX_SIZE;
    offset.y += tile_sz / TEX_SIZE;
    auto u_page_pos = m_update_shader->QueryUniform("u_page_pos");
    assert(u_page_pos);
    u_page_pos->SetValue(offset.xy, 2);

    const float layer_tile_sz = static_cast<float>(tile_sz * std::pow(2.0f, page.mip));

    const sm::vec2 update_size(region.Width(), region.Height());
    auto u_update_size = m_update_shader->QueryUniform("u_update_size");
    assert(u_update_size);
    auto tile_update_size = update_size / layer_tile_sz;
    u_update_size->SetValue(tile_update_size.xy, 2);

    sm::vec2 update_offset;
    update_offset.x = region.xmin - static_cast<int>(region.xmin / layer_tile_sz) * layer_tile_sz;
    update_offset.y = region.ymin - static_cast<int>(region.ymin / layer_tile_sz) * layer_tile_sz;
    auto u_update_offset = m_update_shader->QueryUniform("u_update_offset");
    assert(u_update_offset);
    auto tile_update_offset = update_offset / layer_tile_sz;
    u_update_offset->SetValue(tile_update_offset.xy, 2);

    ur2::DrawState ds;
    ds.render_state = ur2::DefaultRenderState2D();
    ds.program = m_update_shader;
    ctx.DrawQuad(ur2::Context::VertexLayout::Pos, ds);
}

void TextureStack::DrawTexture(const ur2::Device& dev, ur2::Context& ctx, const ur2::RenderState& rs,
                               float screen_width, float screen_height) const
{
    if (!m_final_shader)
    {
        //std::vector<std::string> textures;
        //textures.push_back("finer_tex");
        //textures.push_back("coarser_tex");

        m_final_shader = dev.CreateShaderProgram(final_vs, final_fs);
    }

    const uint32_t finer_layer = CalcMipmapLevel(m_layers.size(), m_scale);
    const uint32_t coarser_layer = finer_layer < m_layers.size() - 1 ? finer_layer + 1 : finer_layer;
    ctx.SetTexture(m_final_shader->QueryTexSlot("finer_tex"), m_layers[finer_layer].tex);
    ctx.SetTexture(m_final_shader->QueryTexSlot("coarser_tex"), m_layers[coarser_layer].tex);

    sm::mat4 view_mat = sm::mat4::Scaled(TEX_SIZE / screen_width, TEX_SIZE / screen_height, 1);
    auto u_view_mat = m_final_shader->QueryUniform("u_view_mat");
    assert(u_view_mat);
    u_view_mat->SetValue(view_mat.x, 4 * 4);

    auto scale = m_scale / static_cast<float>(std::pow(2, finer_layer));
    auto u_scale = m_final_shader->QueryUniform("u_scale");
    assert(u_scale);
    u_scale->SetValue(&scale, 1);

    auto offset = m_offset / TEX_SIZE / static_cast<float>(std::pow(2, finer_layer));
    auto u_offset = m_final_shader->QueryUniform("u_offset");
    assert(u_offset);
    u_offset->SetValue(offset.xy, 2);

    ur2::DrawState ds;
    ds.render_state = rs;
    ds.program = m_final_shader;
    ctx.DrawQuad(ur2::Context::VertexLayout::Pos, ds);
}

void TextureStack::DrawDebug(const ur2::Device& dev, ur2::Context& ctx, const ur2::RenderState& rs) const
{
    tess::Painter pt;

    // region
    const float h_sz = TEX_SIZE * 0.5f;
    pt.AddRect(sm::vec2(-h_sz, -h_sz), sm::vec2(h_sz, h_sz), 0xff0000ff);

    // layers
    const float sx = -400;
    const float sy = -350;
    const float size  = 100;
    const float space = 4;
    auto start = CalcMipmapLevel(m_layers.size(), m_scale);
    for (size_t i = 0, n = m_layers.size(); i < n; ++i)
    {
        auto& layer = m_layers[i];

        const float x = sx + (size + space) * i;
        sm::rect region(x, sy, x + size, sy + size);
        pt2::RenderSystem::DrawTexture(dev, ctx, rs, layer.tex, region, sm::Matrix2D(), false);

        // border
        pt.AddRect(sm::vec2(region.xmin, region.ymin), sm::vec2(region.xmax, region.ymax), 0xff00ff00);

        // viewport
        auto r = CalcUVRegion(i, layer);
        if (r.xmax > 1.0f) {
            r.Translate(sm::vec2(1 - r.xmax, 0));
        }
        if (r.xmax < 0) {
            r.Translate(sm::vec2(-r.xmin, 0));
        }
        if (r.ymax > 1.0f) {
            r.Translate(sm::vec2(0, 1 - r.ymax));
        }
        if (r.ymax < 0) {
            r.Translate(sm::vec2(0, -r.ymin));
        }
        auto r_min = sm::vec2(r.xmin, r.ymin) * sm::vec2(region.Width(), region.Height()) + sm::vec2(region.xmin, region.ymin);
        auto r_max = sm::vec2(r.xmax, r.ymax) * sm::vec2(region.Width(), region.Height()) + sm::vec2(region.xmin, region.ymin);
        const auto color = i >= start ? 0xff0000ff : 0xff00ff00;
        pt.AddRect(r_min, r_max, color);
    }

    pt2::RenderSystem::DrawPainter(dev, ctx, rs, pt);
}

void TextureStack::TraverseDiffPages(const std::vector<sm::rect>& regions, size_t start_layer,
                                     std::function<void(const textile::Page& page, const sm::rect& region)> cb)
{
    assert(regions.size() == m_layers.size() - start_layer);
    for (size_t i = start_layer, n = m_layers.size(); i < n; ++i)
    {
        auto& old_r = m_layers[i].region;
        auto& new_r = regions[i - start_layer];
        if (sm::is_rect_contain_rect(old_r, new_r)) {
            continue;
        }

        if (!old_r.IsValid() || !sm::is_rect_intersect_rect(old_r, new_r)) {
            TraversePages(new_r, i, cb);
            continue;
        }

        if (sm::is_rect_contain_rect(new_r, old_r))
        {
            TraversePages(sm::rect(new_r.xmin, new_r.ymin, new_r.xmax, old_r.ymin), i, cb);
            TraversePages(sm::rect(new_r.xmin, old_r.ymax, new_r.xmax, new_r.ymax), i, cb);
            TraversePages(sm::rect(new_r.xmin, old_r.ymin, old_r.xmin, old_r.ymax), i, cb);
            TraversePages(sm::rect(old_r.xmax, old_r.ymin, new_r.xmax, old_r.ymax), i, cb);
        }
        else if (new_r.xmin <= old_r.xmin && new_r.ymin <= old_r.ymin)
        {
            TraversePages(sm::rect(new_r.xmin, new_r.ymin, old_r.xmin, new_r.ymax), i, cb);
            TraversePages(sm::rect(old_r.xmin, new_r.ymin, new_r.xmax, old_r.ymin), i, cb);
            if (new_r.ymax > old_r.ymax) {
                TraversePages(sm::rect(old_r.xmin, old_r.xmax, new_r.xmax, new_r.ymax), i, cb);
            }
        }
        else if (new_r.xmin <= old_r.xmin && new_r.ymax >= old_r.ymax)
        {
            TraversePages(sm::rect(new_r.xmin, new_r.ymin, old_r.xmin, new_r.ymax), i, cb);
            TraversePages(sm::rect(old_r.xmin, old_r.ymax, new_r.xmax, new_r.ymax), i, cb);
            if (new_r.xmax > old_r.xmax) {
                TraversePages(sm::rect(old_r.xmax, new_r.ymin, new_r.xmax, new_r.ymax), i, cb);
            }
        }
        else if (new_r.xmax >= old_r.xmax && new_r.ymax >= old_r.ymax)
        {
            TraversePages(sm::rect(new_r.xmin, old_r.ymax, new_r.xmax, new_r.ymax), i, cb);
            TraversePages(sm::rect(old_r.xmax, new_r.ymin, new_r.xmax, old_r.ymax), i, cb);
            if (new_r.ymin < old_r.ymin) {
                TraversePages(sm::rect(new_r.xmin, new_r.ymin, old_r.xmax, old_r.ymin), i, cb);
            }
        }
        else if (new_r.xmax >= old_r.xmax && new_r.ymin <= old_r.ymin)
        {
            TraversePages(sm::rect(new_r.xmin, new_r.ymin, new_r.xmax, old_r.ymin), i, cb);
            TraversePages(sm::rect(old_r.xmax, old_r.ymin, new_r.xmax, new_r.ymax), i, cb);
            if (new_r.xmin < old_r.xmin) {
                TraversePages(sm::rect(new_r.xmin, old_r.ymin, old_r.xmin, new_r.ymax), i, cb);
            }
        }
        else if (new_r.xmin < old_r.xmin && new_r.xmax <= old_r.xmax
              && new_r.ymin >= old_r.ymin && new_r.ymax <= old_r.ymax)
        {
            TraversePages(sm::rect(new_r.xmin, new_r.ymin, old_r.xmin, new_r.ymax), i, cb);
        }
        else if (new_r.xmax > old_r.xmax && new_r.xmin >= old_r.xmin
              && new_r.ymin >= old_r.ymin && new_r.ymax <= old_r.ymax)
        {
            TraversePages(sm::rect(old_r.xmax, new_r.ymin, new_r.xmax, new_r.ymax), i, cb);
        }
        else if (new_r.ymin < old_r.ymin && new_r.ymax <= old_r.ymax
              && new_r.xmin >= old_r.xmin && new_r.xmax <= old_r.xmax)
        {
            TraversePages(sm::rect(new_r.xmin, new_r.ymin, new_r.xmax, old_r.ymin), i, cb);
        }
        else if (new_r.ymax > old_r.ymax && new_r.ymin >= old_r.ymin
              && new_r.xmin >= old_r.xmin && new_r.xmax <= old_r.xmax)
        {
            TraversePages(sm::rect(new_r.xmin, old_r.ymax, new_r.xmax, new_r.ymax), i, cb);
        }
        else
        {
            assert(0);
        }
    }
}

void TextureStack::TraversePages(const sm::rect& region, size_t layer,
                                 std::function<void(const textile::Page& page, const sm::rect& region)> cb)
{
    if (!region.IsValid() || region.Width() == 0 || region.Height() == 0) {
        return;
    }

    const float scale = static_cast<float>(std::pow(2, layer));
    const float layer_w = m_vtex_info.vtex_width * scale;
    const float layer_h = m_vtex_info.vtex_height * scale;
    const float tile_sz = m_vtex_info.tile_size * scale;

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
            cb(textile::Page(x, y, layer), r);
        }
    }
}

}