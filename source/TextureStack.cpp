#include "clipmap/TextureStack.h"

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
layout (location = 1) in vec2 texcoord;

out VS_OUT {
    vec2 texcoord;
} vs_out;

uniform float u_scale;
uniform vec2 u_offset;

void main()
{
	vs_out.texcoord = texcoord;
	gl_Position = vec4(position.xy * u_scale + u_offset, 0, 1);
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
layout (location = 1) in vec2 texcoord;

out VS_OUT {
    vec2 texcoord;
} vs_out;

uniform mat4 u_view_mat;

uniform float u_scale;
uniform vec2 u_offset;

void main()
{
	vs_out.texcoord = texcoord * u_scale + u_offset;
	gl_Position = u_view_mat * position;
}

)";

const char* final_fs = R"(

#version 330 core
out vec4 FragColor;

in VS_OUT {
    vec2 texcoord;
} fs_in;

uniform sampler2D layer3_map;
uniform sampler2D layer4_map;

void main(void){
    FragColor = texture2D(layer3_map, fs_in.texcoord);
}

)";

}

namespace clipmap
{

TextureStack::TextureStack(const textile::VTexInfo& info)
    : m_vtex_info(info)
{
    auto num = static_cast<int>(std::log2(std::min(info.PageTableWidth(), info.PageTableHeight()))) + 1;
    m_layers.resize(num);

    m_page_buf = new uint8_t[info.tile_size * info.tile_size * 4];
    m_page_tex = std::make_shared<ur::Texture>();
}

TextureStack::~TextureStack()
{
    delete[] m_page_buf;

    if (m_fbo != 0) {
        auto& rc = ur::Blackboard::Instance()->GetRenderContext();
        rc.ReleaseRenderTarget(m_fbo);
    }
}

void TextureStack::Update(const textile::Page& page, const uint8_t* data)
{
    auto& rc = ur::Blackboard::Instance()->GetRenderContext();
    assert(!m_layers.empty());
    if (!m_layers[0].tex)
    {
        for (auto& layer : m_layers) {
            layer.tex = std::make_unique<ur::Texture>();
            layer.tex->Upload(&rc, TEX_SIZE, TEX_SIZE, ur::TEXTURE_RGBA8);
        }
    }

    FillPageBuf(data);

    assert(page.mip < static_cast<int>(m_layers.size()));
    auto& layer = m_layers[page.mip];
    auto tile_sz = m_vtex_info.tile_size;

    // cpu
//    rc.UpdateSubTexture(m_page_buf, page.x * tile_sz, page.y * tile_sz, tile_sz, tile_sz, layer.tex->TexID());

    // gpu
    m_page_tex->Upload(&rc, tile_sz, tile_sz, ur::TEXTURE_RGBA8, m_page_buf);

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

    if (!m_update_shader)
    {
        std::vector<std::string> textures;
        textures.push_back("page_map");

        std::vector<ur::VertexAttrib> layout;

        m_update_shader = std::make_shared<ur::Shader>(&rc, update_vs, update_fs, textures, layout);
    }
    m_update_shader->SetUsedTextures({ m_page_tex->TexID() });
    m_update_shader->Use();

    m_update_shader->SetFloat("u_scale", static_cast<float>(m_vtex_info.tile_size) / TEX_SIZE);
    sm::vec2 offset(
        (static_cast<float>(page.x) + 0.5f) * tile_sz / TEX_SIZE * 2 - 1,
        (static_cast<float>(page.y) + 0.5f) * tile_sz / TEX_SIZE * 2 - 1
    );
    m_update_shader->SetVec2("u_offset", offset.xy);

    rc.RenderQuad(ur::RenderContext::VertLayout::VL_POS_TEX);

    rc.BindShader(0);

    rc.UnbindRenderTarget();
    rc.SetViewport(vp_x, vp_y, vp_w, vp_h);

    rc.SetZTest(ur::DEPTH_LESS_EQUAL);
    rc.SetCullMode(ur::CULL_BACK);
}

void TextureStack::Draw(float scale, const sm::vec2& offset,
                        float screen_width, float screen_height) const
{
    assert(!m_layers.empty());
    if (!m_layers[0].tex) {
        return;
    }

    // texture
    auto& rc = ur::Blackboard::Instance()->GetRenderContext();
    if (!m_final_shader)
    {
        std::vector<std::string> textures;
        textures.push_back("layer3_map");
        textures.push_back("layer4_map");

        std::vector<ur::VertexAttrib> layout;

        m_final_shader = std::make_shared<ur::Shader>(&rc, final_vs, final_fs, textures, layout);
    }
    rc.SetZTest(ur::DEPTH_DISABLE);
    rc.SetCullMode(ur::CULL_DISABLE);

    m_final_shader->SetUsedTextures({ m_layers[3].tex->TexID(), m_layers[4].tex->TexID() });
    m_final_shader->Use();

    const float tex_sz = 512;

    sm::mat4 view_mat = sm::mat4::Scaled(tex_sz / screen_width, tex_sz / screen_height, 1);
    m_final_shader->SetMat4("u_view_mat", view_mat.x);

    m_final_shader->SetFloat("u_scale", scale);
    m_final_shader->SetVec2("u_offset", (offset / 500).xy);

    rc.RenderQuad(ur::RenderContext::VertLayout::VL_POS_TEX);

    rc.BindShader(0);

    //rc.SetZTest(ur::DEPTH_LESS_EQUAL);
    //rc.SetCullMode(ur::CULL_BACK);

    // region
    sm::Matrix2D mt;
    mt.Scale(scale, scale);
    mt.Translate(offset.x, offset.y);

    tess::Painter pt;
    pt.AddRect(mt * sm::vec2(-tex_sz/2, -tex_sz/2), mt * sm::vec2(tex_sz/2, tex_sz/2), 0xff0000ff);
    pt2::RenderSystem::DrawPainter(pt);

    // debug draw
    const float sx = -400;
    const float sy = -350;
    const float size = 170;
    const float space = 4;
    for (size_t i = 0, n = m_layers.size(); i < n; ++i)
    {
        const float x = sx + (size + space) * i;
        pt2::RenderSystem::DrawTexture(*m_layers[i].tex, sm::rect(x, sy, x + size, sy + size), mt, false);
    }

    rc.SetZTest(ur::DEPTH_LESS_EQUAL);
    rc.SetCullMode(ur::CULL_BACK);
}

void TextureStack::FillPageBuf(const uint8_t* data)
{
    auto dst = m_page_buf;
    memset(dst, 0xff, m_vtex_info.tile_size * m_vtex_info.tile_size * 4);
    const uint8_t* src = data;
    for (size_t y = 0; y < m_vtex_info.tile_size; ++y) {
        for (size_t x = 0; x < m_vtex_info.tile_size; ++x) {
            memset(&dst[(y * m_vtex_info.tile_size + x) * 4], 0xff, 4);
            for (size_t c = 0; c < m_vtex_info.channels; ++c) {
                auto s_idx = ((y * m_vtex_info.tile_size + x) * m_vtex_info.channels + c) * m_vtex_info.bytes;
                auto d_idx = (y * m_vtex_info.tile_size + x) * 4 + c;
                dst[d_idx] = src[s_idx];
            }
        }
    }
}

}