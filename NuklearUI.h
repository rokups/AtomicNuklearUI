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
#pragma once


#include <Atomic/Core/Object.h>
#include <Atomic/Graphics/VertexBuffer.h>
#include <Atomic/Graphics/IndexBuffer.h>
#include <Atomic/Graphics/Texture2D.h>
#include "nuklear/nuklear.h"

#define NK_POINTER_HASH(p) (((int32_t)((size_t)p & 0xFFFFFFFF)) ^ (int32_t)((size_t)p >> 32))

namespace Atomic
{

class NuklearUI
    : public Atomic::Object
{
ATOMIC_OBJECT(NuklearUI, Atomic::Object);
public:
    NuklearUI(Atomic::Context* ctx);
    virtual ~NuklearUI();

    /// Get nuklear context.
    nk_context* GetNkContext() { return &_nk.ctx; }
    /// Get nuklear context.
    operator nk_context*() { return &_nk.ctx; }
    /// Get nuklear font atlas.
    struct nk_font_atlas* GetFontAtlas() { return &_nk.atlas; }
    /// Get nuklear ui scale.
    void SetScale(float scale)
    {
        _uiScale = scale;
        UpdateProjectionMatrix();
    }

    /// Set nuklear ui scale.
    float GetScale() const { return _uiScale; }
    /// Begin adding fonts.
    void BeginAddFonts(float default_font_size = 13.f);
    //! Add font to imgui subsystem.
    /*!
      \param font_path a string pointing to TTF font resource.
      \param size a font size. If 0 then size of last font is used.
      \param ranges optional ranges of font that should be used. Parameter is array of {start1, stop1, ..., startN, stopN, 0}.
      \return ImFont instance that may be used for setting current font when drawing GUI.
    */
    nk_font* AddFont(const Atomic::String& font_path, float size, const nk_rune* ranges);
    /// End adding fonts.
    void EndAddFonts();

protected:
    void OnInputBegin();
    void OnRawEvent(Atomic::VariantMap& args);
    void OnInputEnd();
    void OnEndRendering();

    static void ClipboardCopy(nk_handle usr, const char* text, int len);
    static void ClipboardPaste(nk_handle usr, struct nk_text_edit* edit);

    void UpdateProjectionMatrix();
    void ReallocateBuffers(unsigned int vertex_count, unsigned int index_count);

    struct
    {
        nk_context ctx;
        struct nk_font_atlas atlas;
        struct nk_buffer commands;
        struct nk_draw_null_texture null_texture;
        struct nk_convert_config config;
    } _nk;
    Atomic::WeakPtr<Atomic::Graphics> _graphics;
    Atomic::SharedPtr<Atomic::Texture2D> _null_texture;
    Atomic::SharedPtr<Atomic::VertexBuffer> _vertex_buffer;
    Atomic::SharedPtr<Atomic::IndexBuffer> _index_buffer;
    Atomic::SharedPtr<Atomic::Texture2D> _font_texture;
    Atomic::Matrix4 _projection;
    float _uiScale = 1.0f;
};

}
