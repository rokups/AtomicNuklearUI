NuklearUI
=========

[nuklear](https://github.com/vurtun/nuklear) integration for [AtomicGameEngine](https://github.com/AtomicGameEngine/AtomicGameEngine/).

# How to use

```cpp
// Create subsystem
auto nuklear = new NuklearUI(context_);
nuklear->BeginAddFonts();
auto fa = nuklear->AddFont("UI/fontawesome-webfont.ttf", 0, font_awesome_ranges);
nuklear->EndAddFonts();                                                                                 0);
// Draw GUI
SubscribeToEvent(E_UPDATE, [&](StringHash, VariantMap&) {
    nk_begin(nuklear->GetNkContext(), "Example");
    nk_end(nuklear->GetNkContext());
});
```
