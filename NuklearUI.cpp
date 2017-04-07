//
// Copyright (c) 2016 Rokas Kupstys
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
#define NK_IMPLEMENTATION 1
#include <string.h>
#include <SDL.h>
#include <Atomic/Core/Context.h>
#include <Atomic/Core/CoreEvents.h>
#include <Atomic/Graphics/Graphics.h>
#include <Atomic/Graphics/GraphicsEvents.h>
#include <Atomic/Input/InputEvents.h>
#include <Atomic/Resource/ResourceCache.h>
#include "NuklearUI.h"
#undef NK_IMPLEMENTATION

using namespace Atomic;
using namespace std::placeholders;

struct nk_sdl_vertex
{
    float position[2];
    float uv[2];
    nk_byte col[4];
};

void NuklearUI::ClipboardCopy(nk_handle usr, const char* text, int len)
{
    String str(text, (unsigned int)len);
    SDL_SetClipboardText(str.CString());
}

void NuklearUI::ClipboardPaste(nk_handle usr, struct nk_text_edit *edit)
{
    const char *text = SDL_GetClipboardText();
    if (text) nk_textedit_paste(edit, text, nk_strlen(text));
    (void)usr;
}

NuklearUI::NuklearUI(Context* ctx)
    : Object(ctx)
{
    _graphics = GetSubsystem<Graphics>();

    _index_buffer = new IndexBuffer(context_);
    _vertex_buffer = new VertexBuffer(context_);
    _null_texture = context_->CreateObject<Texture2D>();

    nk_init_default(&_nk.ctx, 0);
    _nk.ctx.clip.copy = &ClipboardCopy;
    _nk.ctx.clip.paste = &ClipboardPaste;
    _nk.ctx.clip.userdata = nk_handle_ptr(0);

    nk_buffer_init_default(&_nk.commands);

    unsigned whiteOpaque = 0xffffffff;
    _null_texture->SetNumLevels(1);
    _null_texture->SetSize(1, 1, Graphics::GetRGBAFormat());
    _null_texture->SetData(0, 0, 0, 1, 1, &whiteOpaque);
    _nk.null_texture.texture.ptr = _null_texture.Get();

    PODVector<VertexElement> elems;
    elems.Push(VertexElement(TYPE_VECTOR2, SEM_POSITION));
    elems.Push(VertexElement(TYPE_VECTOR2, SEM_TEXCOORD));
    elems.Push(VertexElement(TYPE_UBYTE4_NORM, SEM_COLOR));
    _vertex_buffer->SetSize(MAX_VERTEX_MEMORY / sizeof(nk_sdl_vertex), elems, true);
    _index_buffer->SetSize(MAX_ELEMENT_MEMORY / sizeof(unsigned short), false, true);

    static const struct nk_draw_vertex_layout_element vertex_layout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT,    NK_OFFSETOF(struct nk_sdl_vertex, position)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT,    NK_OFFSETOF(struct nk_sdl_vertex, uv)},
        {NK_VERTEX_COLOR,    NK_FORMAT_R8G8B8A8, NK_OFFSETOF(struct nk_sdl_vertex, col)},
        {NK_VERTEX_LAYOUT_END}
    };
    NK_MEMSET(&_nk.config, 0, sizeof(_nk.config));
    _nk.config.vertex_layout = vertex_layout;
    _nk.config.vertex_size = sizeof(struct nk_sdl_vertex);
    _nk.config.vertex_alignment = NK_ALIGNOF(struct nk_sdl_vertex);
    _nk.config.null = _nk.null_texture;
    _nk.config.circle_segment_count = 22;
    _nk.config.curve_segment_count = 22;
    _nk.config.arc_segment_count = 22;
    _nk.config.global_alpha = 1.0f;
    _nk.config.shape_AA = NK_ANTI_ALIASING_ON;
    _nk.config.line_AA = NK_ANTI_ALIASING_ON;

    UpdateProjectionMatrix();

    SubscribeToEvent(E_INPUTBEGIN, std::bind(&NuklearUI::OnInputBegin, this));
    SubscribeToEvent(E_SDLRAWINPUT, std::bind(&NuklearUI::OnRawEvent, this, _2));
    SubscribeToEvent(E_INPUTEND, std::bind(&NuklearUI::OnInputEnd, this));
    SubscribeToEvent(E_ENDRENDERING, std::bind(&NuklearUI::OnEndRendering, this));
    SubscribeToEvent(E_SCREENMODE, std::bind(&NuklearUI::UpdateProjectionMatrix, this));
}

NuklearUI::~NuklearUI()
{
    UnsubscribeFromAllEvents();
    nk_font_atlas_clear(&_nk.atlas);
    nk_free(&_nk.ctx);
}

void NuklearUI::OnInputBegin()
{
    nk_input_begin(&_nk.ctx);
}

void NuklearUI::OnRawEvent(VariantMap& args)
{
    auto evt = static_cast<SDL_Event*>(args[SDLRawInput::P_SDLEVENT].Get<void*>());
    auto ctx = &_nk.ctx;
    if (evt->type == SDL_KEYUP || evt->type == SDL_KEYDOWN)
    {
        int down = evt->type == SDL_KEYDOWN;
        const Uint8* state = SDL_GetKeyboardState(0);
        SDL_Keycode sym = evt->key.keysym.sym;
        if (sym == SDLK_RSHIFT || sym == SDLK_LSHIFT)
            nk_input_key(ctx, NK_KEY_SHIFT, down);
        else if (sym == SDLK_DELETE)
            nk_input_key(ctx, NK_KEY_DEL, down);
        else if (sym == SDLK_RETURN)
            nk_input_key(ctx, NK_KEY_ENTER, down);
        else if (sym == SDLK_TAB)
            nk_input_key(ctx, NK_KEY_TAB, down);
        else if (sym == SDLK_BACKSPACE)
            nk_input_key(ctx, NK_KEY_BACKSPACE, down);
        else if (sym == SDLK_HOME)
        {
            nk_input_key(ctx, NK_KEY_TEXT_START, down);
            nk_input_key(ctx, NK_KEY_SCROLL_START, down);
        }
        else if (sym == SDLK_END)
        {
            nk_input_key(ctx, NK_KEY_TEXT_END, down);
            nk_input_key(ctx, NK_KEY_SCROLL_END, down);
        }
        else if (sym == SDLK_PAGEDOWN)
            nk_input_key(ctx, NK_KEY_SCROLL_DOWN, down);
        else if (sym == SDLK_PAGEUP)
            nk_input_key(ctx, NK_KEY_SCROLL_UP, down);
        else if (sym == SDLK_z)
            nk_input_key(ctx, NK_KEY_TEXT_UNDO, down && state[SDL_SCANCODE_LCTRL]);
        else if (sym == SDLK_r)
            nk_input_key(ctx, NK_KEY_TEXT_REDO, down && state[SDL_SCANCODE_LCTRL]);
        else if (sym == SDLK_c)
            nk_input_key(ctx, NK_KEY_COPY, down && state[SDL_SCANCODE_LCTRL]);
        else if (sym == SDLK_v)
            nk_input_key(ctx, NK_KEY_PASTE, down && state[SDL_SCANCODE_LCTRL]);
        else if (sym == SDLK_x)
            nk_input_key(ctx, NK_KEY_CUT, down && state[SDL_SCANCODE_LCTRL]);
        else if (sym == SDLK_b)
            nk_input_key(ctx, NK_KEY_TEXT_LINE_START, down && state[SDL_SCANCODE_LCTRL]);
        else if (sym == SDLK_e)
            nk_input_key(ctx, NK_KEY_TEXT_LINE_END, down && state[SDL_SCANCODE_LCTRL]);
        else if (sym == SDLK_UP)
            nk_input_key(ctx, NK_KEY_UP, down);
        else if (sym == SDLK_DOWN)
            nk_input_key(ctx, NK_KEY_DOWN, down);
        else if (sym == SDLK_LEFT)
        {
            if (state[SDL_SCANCODE_LCTRL])
                nk_input_key(ctx, NK_KEY_TEXT_WORD_LEFT, down);
            else
                nk_input_key(ctx, NK_KEY_LEFT, down);
        }
        else if (sym == SDLK_RIGHT)
        {
            if (state[SDL_SCANCODE_LCTRL])
                nk_input_key(ctx, NK_KEY_TEXT_WORD_RIGHT, down);
            else
                nk_input_key(ctx, NK_KEY_RIGHT, down);
        }
    }
    else if (evt->type == SDL_MOUSEBUTTONDOWN || evt->type == SDL_MOUSEBUTTONUP)
    {
        int down = evt->type == SDL_MOUSEBUTTONDOWN;
        const int x = (const int)(evt->button.x / _uiScale), y = (const int)(evt->button.y / _uiScale);
        if (evt->button.button == SDL_BUTTON_LEFT)
            nk_input_button(ctx, NK_BUTTON_LEFT, x, y, down);
        if (evt->button.button == SDL_BUTTON_MIDDLE)
            nk_input_button(ctx, NK_BUTTON_MIDDLE, x, y, down);
        if (evt->button.button == SDL_BUTTON_RIGHT)
            nk_input_button(ctx, NK_BUTTON_RIGHT, x, y, down);
    }
    else if (evt->type == SDL_MOUSEMOTION)
    {
        if (ctx->input.mouse.grabbed)
        {
            int x = (int)ctx->input.mouse.prev.x, y = (int)ctx->input.mouse.prev.y;
            nk_input_motion(ctx, (int)(x + evt->motion.xrel / _uiScale), (int)(y + evt->motion.yrel / _uiScale));
        }
        else
            nk_input_motion(ctx, (int)(evt->motion.x / _uiScale), (int)(evt->motion.y / _uiScale));
    }
    else if (evt->type == SDL_TEXTINPUT)
    {
        nk_glyph glyph = {};
        memcpy(glyph, evt->text.text, NK_UTF_SIZE);
        nk_input_glyph(ctx, glyph);
    }
    else if (evt->type == SDL_MOUSEWHEEL)
        nk_input_scroll(ctx, {(float)evt->wheel.x, (float)evt->wheel.y});
}

void NuklearUI::OnInputEnd()
{
    nk_input_end(&_nk.ctx);
}

void NuklearUI::OnEndRendering()
{
    // Engine does not render when window is closed or device is lost
    assert(_graphics && _graphics->IsInitialized() && !_graphics->IsDeviceLost());

    // Max. vertex / index count is not assumed to change later
    void* vertexData = _vertex_buffer->Lock(0, _vertex_buffer->GetVertexCount(), true);
    void* indexData = _index_buffer->Lock(0, _index_buffer->GetIndexCount(), true);
    if (vertexData && indexData)
    {
        struct nk_buffer vbuf, ebuf;
        nk_buffer_init_fixed(&vbuf, vertexData, nk_size(MAX_VERTEX_MEMORY));
        nk_buffer_init_fixed(&ebuf, indexData, nk_size(MAX_ELEMENT_MEMORY));
        nk_convert(&_nk.ctx, &_nk.commands, &vbuf, &ebuf, &_nk.config);

#if (defined(_WIN32) && !defined(ATOMIC_D3D11) && !defined(ATOMIC_OPENGL)) || defined(ATOMIC_D3D9)
        for (int i = 0; i < _vertex_buffer->GetVertexCount(); i++)
        {
            nk_sdl_vertex* v = (nk_sdl_vertex*)vertexData + i;
            v->position[0] += 0.5f;
            v->position[1] += 0.5f;
        }
#endif

        _graphics->ClearParameterSources();
        _graphics->SetColorWrite(true);
        _graphics->SetCullMode(CULL_NONE);
        _graphics->SetDepthTest(CMP_ALWAYS);
        _graphics->SetDepthWrite(false);
        _graphics->SetFillMode(FILL_SOLID);
        _graphics->SetStencilTest(false);
        _graphics->SetVertexBuffer(_vertex_buffer);
        _graphics->SetIndexBuffer(_index_buffer);
        _vertex_buffer->Unlock();
        _index_buffer->Unlock();

        unsigned index = 0;
        const struct nk_draw_command* cmd;
        nk_draw_foreach(cmd, &_nk.ctx, &_nk.commands)
        {
            if (!cmd->elem_count)
                continue;

            ShaderVariation* ps;
            ShaderVariation* vs;

            Texture2D* texture = static_cast<Texture2D*>(cmd->texture.ptr);
            if (!texture)
            {
                ps = _graphics->GetShader(PS, "Basic", "VERTEXCOLOR");
                vs = _graphics->GetShader(VS, "Basic", "VERTEXCOLOR");
            }
            else
            {
                // If texture contains only an alpha channel, use alpha shader (for fonts)
                vs = _graphics->GetShader(VS, "Basic", "DIFFMAP VERTEXCOLOR");
                if (texture->GetFormat() == Graphics::GetAlphaFormat())
                    ps = _graphics->GetShader(PS, "Basic", "ALPHAMAP VERTEXCOLOR");
                else
                    ps = _graphics->GetShader(PS, "Basic", "DIFFMAP VERTEXCOLOR");
            }

            _graphics->SetShaders(vs, ps);
            if (_graphics->NeedParameterUpdate(SP_OBJECT, this))
                _graphics->SetShaderParameter(VSP_MODEL, Matrix3x4::IDENTITY);
            if (_graphics->NeedParameterUpdate(SP_CAMERA, this))
                _graphics->SetShaderParameter(VSP_VIEWPROJ, _projection);
            if (_graphics->NeedParameterUpdate(SP_MATERIAL, this))
                _graphics->SetShaderParameter(PSP_MATDIFFCOLOR, Color(1.0f, 1.0f, 1.0f, 1.0f));

            float elapsedTime = GetSubsystem<Time>()->GetElapsedTime();
            _graphics->SetShaderParameter(VSP_ELAPSEDTIME, elapsedTime);
            _graphics->SetShaderParameter(PSP_ELAPSEDTIME, elapsedTime);

            IntRect scissor = IntRect(int(cmd->clip_rect.x), int(cmd->clip_rect.y),
                                      int(cmd->clip_rect.x + cmd->clip_rect.w),
                                      int(cmd->clip_rect.y + cmd->clip_rect.h));
            scissor.left_ = int(scissor.left_ * _uiScale);
            scissor.top_ = int(scissor.top_ * _uiScale);
            scissor.right_ = int(scissor.right_ * _uiScale);
            scissor.bottom_ = int(scissor.bottom_ * _uiScale);

            _graphics->SetBlendMode(BLEND_ALPHA);
            _graphics->SetScissorTest(true, scissor);
            _graphics->SetTexture(0, texture);
            _graphics->Draw(TRIANGLE_LIST, index, cmd->elem_count, 0, 0, _vertex_buffer->GetVertexCount());
            index += cmd->elem_count;
        }
        nk_clear(&_nk.ctx);
    }
    _graphics->SetScissorTest(false);
}

void NuklearUI::UpdateProjectionMatrix()
{
    IntVector2 viewSize = _graphics->GetViewport().Size();
    Vector2 invScreenSize(1.0f / viewSize.x_, 1.0f / viewSize.y_);
    Vector2 scale(2.0f * invScreenSize.x_, -2.0f * invScreenSize.y_);
    Vector2 offset(-1.0f, 1.0f);

    _projection = Matrix4(Matrix4::IDENTITY);
    _projection.m00_ = scale.x_ * _uiScale;
    _projection.m03_ = offset.x_;
    _projection.m11_ = scale.y_ * _uiScale;
    _projection.m13_ = offset.y_;
    _projection.m22_ = 1.0f;
    _projection.m23_ = 0.0f;
    _projection.m33_ = 1.0f;
}

void NuklearUI::BeginAddFonts(float default_font_size)
{
    nk_font_atlas_init_default(&_nk.atlas);
    nk_font_atlas_begin(&_nk.atlas);
    if (default_font_size > 0)
        _nk.atlas.default_font = nk_font_atlas_add_default(&_nk.atlas, default_font_size, 0);
}

nk_font* NuklearUI::AddFont(const Atomic::String& font_path, float size, const nk_rune* ranges)
{
    if (size == 0)
    {
        if (_nk.ctx.style.font != 0)
            size = _nk.ctx.style.font->height;
        else if (_nk.atlas.default_font != 0)
            size = _nk.atlas.default_font->config->size;
        else
            return 0;
    }

    if (auto font_file = GetSubsystem<ResourceCache>()->GetFile(font_path))
    {
        PODVector<uint8_t> data;
        data.Resize(font_file->GetSize());
        auto bytes_len = font_file->Read(&data.Front(), data.Size());

        struct nk_font_config config = nk_font_config(size);
        config.range = ranges;
        return nk_font_atlas_add_from_memory(&_nk.atlas, &data.Front(), bytes_len, size, &config);
    }
    return 0;
}

void NuklearUI::EndAddFonts()
{
    int w, h;
    const void* image = nk_font_atlas_bake(&_nk.atlas, &w, &h, NK_FONT_ATLAS_RGBA32);

    _font_texture = context_->CreateObject<Texture2D>();
    _font_texture->SetNumLevels(1);
    _font_texture->SetSize(w, h, Graphics::GetRGBAFormat());
    _font_texture->SetData(0, 0, 0, w, h, image);

    nk_font_atlas_end(&_nk.atlas, nk_handle_ptr(_font_texture.Get()), &_nk.null_texture);
    if (_nk.atlas.default_font)
        nk_style_set_font(&_nk.ctx, &_nk.atlas.default_font->handle);
}
