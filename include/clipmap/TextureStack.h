#pragma once

#include <SM_Vector.h>
#include <unirender/Texture.h>

#include <vector>

namespace ur { class Shader; }
namespace textile { struct Page; struct VTexInfo; }

namespace clipmap
{

class TextureStack
{
public:
    TextureStack(const textile::VTexInfo& vtex_info);
    ~TextureStack();

    void Update(const textile::Page& page, const uint8_t* data);

    void Draw(float scale, const sm::vec2& offset,
        float screen_width, float screen_height) const;

private:
    void FillPageBuf(const uint8_t* data);

private:
    struct Layer
    {
        ur::TexturePtr tex = nullptr;
        int offx = 0, offy = 0;
    };

private:
    const textile::VTexInfo& m_vtex_info;

    std::vector<Layer> m_layers;

    uint8_t*       m_page_buf = nullptr;
    ur::TexturePtr m_page_tex = nullptr;

    int m_fbo = 0;
    std::shared_ptr<ur::Shader> m_update_shader = nullptr;
    mutable std::shared_ptr<ur::Shader> m_final_shader = nullptr;

}; // TextureStack

}