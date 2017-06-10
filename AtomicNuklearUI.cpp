/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Rokas Kupstys
 * Copyright (c) 2008-2016 the Urho3D project.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#define NK_IMPLEMENTATION 1
#include <SDL.h>
#include <Atomic/Core/Context.h>
#include <Atomic/Core/CoreEvents.h>
#include <Atomic/Graphics/Graphics.h>
#include <Atomic/Graphics/GraphicsEvents.h>
#include <Atomic/Input/InputEvents.h>
#include <Atomic/Resource/ResourceCache.h>
#include <Atomic/Core/Profiler.h>
#include <Atomic/IO/Log.h>
#include "AtomicNuklearUI.h"
#undef NK_IMPLEMENTATION

using namespace std::placeholders;
namespace Atomic
{

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

void NuklearUI::ClipboardPaste(nk_handle usr, struct nk_text_edit* edit)
{
    const char* text = SDL_GetClipboardText();
    if (text)
        nk_textedit_paste(edit, text, nk_strlen(text));
    (void)usr;
}

NuklearUI::NuklearUI(Context* context)
    : Object(context)
{
    _graphics = GetSubsystem<Graphics>();

    _index_buffer = new IndexBuffer(context_);
    _vertex_buffer = new VertexBuffer(context_);
    _null_texture = context_->CreateObject<Texture2D>();

    nk_init_default(&_nk, 0);
    nk_font_atlas_init_default(&_atlas);
    _nk.clip.copy = &ClipboardCopy;
    _nk.clip.paste = &ClipboardPaste;
    _nk.clip.userdata = nk_handle_ptr(0);

    nk_buffer_init_default(&_commands);
    ReallocateBuffers(1024, 1024);

    unsigned whiteOpaque = 0xffffffff;
    _null_texture->SetNumLevels(1);
    _null_texture->SetSize(1, 1, Graphics::GetRGBAFormat());
    _null_texture->SetData(0, 0, 0, 1, 1, &whiteOpaque);
    _draw_null_texture.texture.ptr = _null_texture.Get();

    static const struct nk_draw_vertex_layout_element vertex_layout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT,    NK_OFFSETOF(struct nk_sdl_vertex, position)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT,    NK_OFFSETOF(struct nk_sdl_vertex, uv)},
        {NK_VERTEX_COLOR,    NK_FORMAT_R8G8B8A8, NK_OFFSETOF(struct nk_sdl_vertex, col)},
        {NK_VERTEX_LAYOUT_END}
    };
    NK_MEMSET(&_config, 0, sizeof(_config));
    _config.vertex_layout = vertex_layout;
    _config.vertex_size = sizeof(struct nk_sdl_vertex);
    _config.vertex_alignment = NK_ALIGNOF(struct nk_sdl_vertex);
    _config.null = _draw_null_texture;
    _config.circle_segment_count = 22;
    _config.curve_segment_count = 22;
    _config.arc_segment_count = 22;
    _config.global_alpha = 1.0f;
    _config.shape_AA = NK_ANTI_ALIASING_ON;
    _config.line_AA = NK_ANTI_ALIASING_ON;

    UpdateProjectionMatrix();

    SubscribeToEvent(E_POSTUPDATE, [=](StringHash, VariantMap&) {
        ATOMIC_PROFILE(NuklearFrame);
        SendEvent(E_NUKLEARFRAME);
    });
    SubscribeToEvent(E_INPUTBEGIN, std::bind(&NuklearUI::OnInputBegin, this));
    SubscribeToEvent(E_SDLRAWINPUT, std::bind(&NuklearUI::OnRawEvent, this, _2));
    SubscribeToEvent(E_INPUTEND, std::bind(&NuklearUI::OnInputEnd, this));
    SubscribeToEvent(E_ENDRENDERING, std::bind(&NuklearUI::OnEndRendering, this));
    SubscribeToEvent(E_SCREENMODE, std::bind(&NuklearUI::UpdateProjectionMatrix, this));
}

NuklearUI::~NuklearUI()
{
    UnsubscribeFromAllEvents();
    nk_font_atlas_clear(&_atlas);
    nk_free(&_nk);
}

void NuklearUI::OnInputBegin()
{
    nk_input_begin(&_nk);
}

void NuklearUI::OnRawEvent(VariantMap& args)
{
    auto evt = static_cast<SDL_Event*>(args[SDLRawInput::P_SDLEVENT].Get<void*>());
    switch (evt->type)
    {
    case SDL_KEYUP:
    case SDL_KEYDOWN:
    {
        int down = evt->type == SDL_KEYDOWN;
        const Uint8* state = SDL_GetKeyboardState(0);
        SDL_Keycode sym = evt->key.keysym.sym;
        switch (sym)
        {
        case SDLK_RSHIFT:
        case SDLK_LSHIFT:
            nk_input_key(&_nk, NK_KEY_SHIFT, down);
            break;
        case SDLK_DELETE:
            nk_input_key(&_nk, NK_KEY_DEL, down);
            break;
        case SDLK_RETURN:
            nk_input_key(&_nk, NK_KEY_ENTER, down);
            break;
        case SDLK_TAB:
            nk_input_key(&_nk, NK_KEY_TAB, down);
            break;
        case SDLK_BACKSPACE:
            nk_input_key(&_nk, NK_KEY_BACKSPACE, down);
            break;
        case SDLK_HOME:
            nk_input_key(&_nk, NK_KEY_TEXT_START, down);
            nk_input_key(&_nk, NK_KEY_SCROLL_START, down);
            break;
        case SDLK_END:
            nk_input_key(&_nk, NK_KEY_TEXT_END, down);
            nk_input_key(&_nk, NK_KEY_SCROLL_END, down);
            break;
        case SDLK_PAGEDOWN:
            nk_input_key(&_nk, NK_KEY_SCROLL_DOWN, down);
            break;
        case SDLK_PAGEUP:
            nk_input_key(&_nk, NK_KEY_SCROLL_UP, down);
            break;
        case SDLK_z:
            nk_input_key(&_nk, NK_KEY_TEXT_UNDO, down && state[SDL_SCANCODE_LCTRL]);
            break;
        case SDLK_r:
            nk_input_key(&_nk, NK_KEY_TEXT_REDO, down && state[SDL_SCANCODE_LCTRL]);
            break;
        case SDLK_c:
            nk_input_key(&_nk, NK_KEY_COPY, down && state[SDL_SCANCODE_LCTRL]);
            break;
        case SDLK_v:
            nk_input_key(&_nk, NK_KEY_PASTE, down && state[SDL_SCANCODE_LCTRL]);
            break;
        case SDLK_x:
            nk_input_key(&_nk, NK_KEY_CUT, down && state[SDL_SCANCODE_LCTRL]);
            break;
        case SDLK_b:
            nk_input_key(&_nk, NK_KEY_TEXT_LINE_START, down && state[SDL_SCANCODE_LCTRL]);
            break;
        case SDLK_e:
            nk_input_key(&_nk, NK_KEY_TEXT_LINE_END, down && state[SDL_SCANCODE_LCTRL]);
            break;
        case SDLK_UP:
            nk_input_key(&_nk, NK_KEY_UP, down);
            break;
        case SDLK_DOWN:
            nk_input_key(&_nk, NK_KEY_DOWN, down);
            break;
        case SDLK_LEFT:
            if (state[SDL_SCANCODE_LCTRL])
                nk_input_key(&_nk, NK_KEY_TEXT_WORD_LEFT, down);
            else
                nk_input_key(&_nk, NK_KEY_LEFT, down);
            break;
        case SDLK_RIGHT:
            if (state[SDL_SCANCODE_LCTRL])
                nk_input_key(&_nk, NK_KEY_TEXT_WORD_RIGHT, down);
            else
                nk_input_key(&_nk, NK_KEY_RIGHT, down);
            break;
        default:
            break;
        }
        break;
    }
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        nk_input_button(&_nk,
                        (nk_buttons)(evt->button.button - 1),
                        (const int)(evt->button.x / _uiScale),
                        (const int)(evt->button.y / _uiScale),
                        evt->type == SDL_MOUSEBUTTONDOWN);
        break;
    case SDL_MOUSEWHEEL:
        nk_input_scroll(&_nk, {(float)evt->wheel.x, (float)evt->wheel.y});
        break;
    case SDL_MOUSEMOTION:
    {
        if (_nk.input.mouse.grabbed)
        {
            nk_input_motion(&_nk,
                            (int)(_nk.input.mouse.prev.x + evt->motion.xrel / _uiScale),
                            (int)(_nk.input.mouse.prev.y + evt->motion.yrel / _uiScale));
        }
        else
            nk_input_motion(&_nk, (int)(evt->motion.x / _uiScale), (int)(evt->motion.y / _uiScale));
        break;
    }
    case SDL_FINGERUP:
        nk_input_button(&_nk, NK_BUTTON_LEFT, -1, -1, 0);
        break;
    case SDL_FINGERDOWN:
        nk_input_button(&_nk, NK_BUTTON_LEFT, (int)(evt->tfinger.x / _uiScale), (int)(evt->tfinger.y / _uiScale), 1);
        break;
    case SDL_FINGERMOTION:
        if (_nk.input.mouse.grabbed)
        {
            nk_input_motion(&_nk,
                            (int)(_nk.input.mouse.prev.x + evt->tfinger.dx / _uiScale),
                            (int)(_nk.input.mouse.prev.y + evt->tfinger.dy / _uiScale));
        }
        else
            nk_input_motion(&_nk, (int)(evt->tfinger.x / _uiScale), (int)(evt->tfinger.y / _uiScale));
        break;
    case SDL_TEXTINPUT:
    {
        nk_glyph glyph = {};
        memcpy(glyph, evt->text.text, NK_UTF_SIZE);
        nk_input_glyph(&_nk, glyph);
        break;
    }
    default:
        break;
    }

    switch (evt->type)
    {
    case SDL_KEYUP:
    case SDL_KEYDOWN:
    case SDL_TEXTINPUT:
        // is any item active, but not necessarily hovered.
        args[SDLRawInput::P_CONSUMED] = (_nk.last_widget_state & NK_WIDGET_STATE_MODIFIED);
        break;
    case SDL_MOUSEWHEEL:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEMOTION:
    case SDL_FINGERUP:
    case SDL_FINGERDOWN:
    case SDL_FINGERMOTION:
        args[SDLRawInput::P_CONSUMED] = nk_window_is_any_hovered(&_nk);
        break;
    default:
        break;
    }
}

void NuklearUI::OnInputEnd()
{
    nk_input_end(&_nk);
}

void NuklearUI::OnEndRendering()
{
    ATOMIC_PROFILE(NuklearRenderDrawLists);
    // Engine does not render when window is closed or device is lost
    assert(_graphics && _graphics->IsInitialized() && !_graphics->IsDeviceLost());

    // Max. vertex / index count is not assumed to change later
    void* vertexData = _vertex_buffer->Lock(0, _vertex_buffer->GetVertexCount(), true);
    void* indexData = _index_buffer->Lock(0, _index_buffer->GetIndexCount(), true);
    assert(vertexData && indexData);

    struct nk_buffer vbuf, ebuf;
    nk_buffer_init_fixed(&vbuf, vertexData, _vertex_buffer->GetVertexCount() * _vertex_buffer->GetVertexSize());
    nk_buffer_init_fixed(&ebuf, indexData, _index_buffer->GetIndexCount() * _index_buffer->GetIndexSize());
    nk_flags result = nk_convert(&_nk, &_commands, &vbuf, &ebuf, &_config);
#if (defined(_WIN32) && !defined(ATOMIC_D3D11) && !defined(ATOMIC_OPENGL)) || defined(ATOMIC_D3D9)
    for (int i = 0; i < _vertex_buffer->GetVertexCount(); i++)
    {
        nk_sdl_vertex* v = (nk_sdl_vertex*)vertexData + i;
        v->position[0] += 0.5f;
        v->position[1] += 0.5f;
    }
#endif
    _vertex_buffer->Unlock();
    _index_buffer->Unlock();

    _graphics->ClearParameterSources();
    _graphics->SetColorWrite(true);
    _graphics->SetCullMode(CULL_NONE);
    _graphics->SetDepthTest(CMP_ALWAYS);
    _graphics->SetDepthWrite(false);
    _graphics->SetFillMode(FILL_SOLID);
    _graphics->SetStencilTest(false);
    _graphics->SetVertexBuffer(_vertex_buffer);
    _graphics->SetIndexBuffer(_index_buffer);

    unsigned index = 0;
    const struct nk_draw_command* cmd;
    nk_draw_foreach(cmd, &_nk, &_commands)
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

    // FIXME: Last frame was rendered incomplete or contained artifacts. We allocate more memory hoping to fit all the
    // needed data on the next frame. Reallocation and nk_convert() should be retried as much as needed, however doing
    // so commands overrun.
    if (result & NK_CONVERT_VERTEX_BUFFER_FULL)
        ReallocateBuffers((unsigned int)((vbuf.needed / _vertex_buffer->GetVertexSize()) * 2), 0);
    if (result & NK_CONVERT_ELEMENT_BUFFER_FULL)
        ReallocateBuffers(0, (unsigned int)((ebuf.needed / _index_buffer->GetIndexSize()) * 2));

    nk_clear(&_nk);
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

void NuklearUI::AddDefaultFont(float default_font_size)
{
    nk_font_atlas_begin(&_atlas);
    if (default_font_size > 0)
    {
        _atlas.default_font = nk_font_atlas_add_default(&_atlas, default_font_size, 0);
        ReallocateFontTexture();
    }
}

nk_font* NuklearUI::AddFont(const Atomic::String& font_path, float size, const nk_rune* ranges, NKUI_FontFlags flags)
{
    if (size == 0)
    {
        if (_nk.style.font != 0)
            size = _nk.style.font->height;
        else if (_atlas.default_font != 0)
            size = _atlas.default_font->config->size;
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
        config.ttf_data_owned_by_atlas = 0;
        if (flags & NKUI_FONT_MERGE)
        {
            config.merge_mode = 1;                 // always merges with last added font
            config.font = &_atlas.fonts->info;     // baked font required, otherwise this will fail.
            config.coord_type = NK_COORD_PIXEL;
        }
        auto result = nk_font_atlas_add_from_memory(&_atlas, &data.Front(), bytes_len, size, &config);
        if (flags & NKUI_FONT_SET_DEFAULT)
            _atlas.default_font = result;
        ReallocateFontTexture();
        return result;
    }
    return 0;
}

nk_font* NuklearUI::AddFont(const Atomic::String& font_path, float size, const std::initializer_list<nk_rune>& ranges,
                            NKUI_FontFlags flags)
{
    return AddFont(font_path, size, ranges.size() ? &*ranges.begin() : 0, flags);
}

void NuklearUI::ReallocateFontTexture()
{
    int w, h;
    const void* image = nk_font_atlas_bake(&_atlas, &w, &h, NK_FONT_ATLAS_RGBA32);

    _font_texture = context_->CreateObject<Texture2D>();
    _font_texture->SetNumLevels(1);
    _font_texture->SetSize(w, h, Graphics::GetRGBAFormat());
    _font_texture->SetData(0, 0, 0, w, h, image);

    nk_font_atlas_end(&_atlas, nk_handle_ptr(_font_texture.Get()), &_draw_null_texture);
    if (_atlas.default_font)
        nk_style_set_font(&_nk, &_atlas.default_font->handle);
}

void NuklearUI::ReallocateBuffers(unsigned int vertex_count, unsigned int index_count)
{
    if (vertex_count)
    {
        PODVector<VertexElement> elems;
        elems.Push(VertexElement(TYPE_VECTOR2, SEM_POSITION));
        elems.Push(VertexElement(TYPE_VECTOR2, SEM_TEXCOORD));
        elems.Push(VertexElement(TYPE_UBYTE4_NORM, SEM_COLOR));
        _vertex_buffer->SetSize(vertex_count, elems, true);
    }
    if (index_count)
        _index_buffer->SetSize(index_count, false, true);
}

void NuklearUI::SetScale(float scale)
{
    if (_uiScale == scale)
        return;
    _uiScale = scale;
    UpdateProjectionMatrix();
}

}
