#include "aura/UI/InputRouter.h"

#include <utility>

namespace aura3d::ui {

InputRouter::InputRouter(wma::IWindowManager& windowManager, Context& gui)
    : _window(&windowManager)
    , _gui(&gui)
{
    /*
     * The two built-ins, in the order their ids promise: they are indices into
     * _modes, so they have to be the first two created. Everything the
     * application adds lands after them.
     */
    static_assert(kGameplay == 0 && kMenu == 1,
                  "the built-in mode ids index _modes, so they must be the first "
                  "two createMode() hands out");

    (void)createMode({.cursorVisible = false, .uiEnabled = false}); // kGameplay
    (void)createMode({.cursorVisible = true, .uiEnabled = true});   // kMenu

    _applyMode(kGameplay);
}

InputRouter::~InputRouter()
{
    wma::KeyboardListener& keyboard = _window->getKeyboardListener();
    wma::MouseListener& pointer = _window->getMouseListener();

    /*
     * Every mode's contexts were created by this router and are used by nothing
     * else, so clearing them wholesale is safe here in a way it would not be for
     * a context shared with the application. The UI's own subscriptions live in
     * these same contexts and are withdrawn by ~Context(), which is why this
     * does not reach for the touch listener as well.
     */
    for (const Mode& mode : _modes)
    {
        keyboard.clearKeyActions(mode.keys);
        pointer.setMoveAction(wma::MouseAction{}, mode.pointer);
    }
}

InputRouter::ModeId InputRouter::createMode(const ModeDesc& desc)
{
    wma::KeyboardListener& keyboard = _window->getKeyboardListener();
    wma::MouseListener& pointer = _window->getMouseListener();
    wma::TouchListener& touch = _window->getTouchListener();

    Mode mode;
    mode.keys = keyboard.createContext();
    mode.pointer = pointer.createContext();
    mode.touch = touch.createContext();
    mode.desc = desc;

    const auto id = static_cast<ModeId>(_modes.size());
    _modes.push_back(std::move(mode));

    /*
     * A toggle has to work from wherever the player is, so every one declared
     * so far is replayed into this mode's context. That is what lets modes and
     * toggles be declared in any order -- without it, a mode created after its
     * own toggle key would be the one place that key did not work.
     */
    for (const Toggle& toggle : _toggles)
        _bindToggleIn(toggle.key, toggle.target, id);

    /*
     * Context::attachInput() registers into whichever context is resolved when
     * it is called, so a UI-enabled mode is made active just long enough to
     * attach the UI inside it. That single ordering is what the whole
     * separation rests on: with the UI's whole-keyboard subscription living in
     * this mode's context, no other mode can dispatch to it, and Tab reaches
     * the UI's focus traversal only where the application asked for a UI.
     *
     * Attaching more than once is safe and is how several modes each get their
     * own live UI: the per-context registrations land in different slots, and
     * the handful of context-independent ones (touch, the polled cursor) are
     * simply rewritten with the same values.
     */
    if (desc.uiEnabled)
    {
        keyboard.setActiveContext(_modes[id].keys);
        pointer.setActiveContext(_modes[id].pointer);
        touch.setActiveContext(_modes[id].touch);

        _gui->attachInput(*_window);

        //! Restored, since createMode() may be called long after the router has
        //! settled into a mode and must not silently change it.
        keyboard.setActiveContext(_modes[_mode].keys);
        pointer.setActiveContext(_modes[_mode].pointer);
        touch.setActiveContext(_modes[_mode].touch);
    }

    return id;
}

void InputRouter::setBaseMode(ModeId mode) noexcept
{
    if (!_valid(mode))
    {
        INK_WARN << "InputRouter::setBaseMode: no such mode (" << mode << ")";
        return;
    }
    _base = mode;
}

bool InputRouter::_menuKeyAvailable() const noexcept
{
    /*
     * A mode key yields to a focused *text field*, and only to that. The keys a
     * game reaches for here are exactly the ones a field needs -- Escape reverts
     * an edit, Tab moves between fields -- so firing both would make it
     * impossible to correct a typo without dismissing the screen underneath it.
     * Pressing Escape twice therefore does what it reads as: the first leaves
     * the field, the second leaves the screen.
     *
     * Deliberately not isCapturingKeyboard(), which is true for *any* focused
     * widget: a button keeps focus after being clicked, so gating on it would
     * leave a screen with no way out after its first button press.
     */
    return !_gui->isCapturingTextInput();
}

void InputRouter::_bindToggleIn(wma::Key key, ModeId target, ModeId in)
{
    //! A fresh callable per context: wma::KeyAction takes a move-only callback,
    //! so one cannot be handed to every mode.
    _window->getKeyboardListener().addKeyAction(
        key,
        wma::KeyAction{[this, target]() {
            if (_menuKeyAvailable())
                toggle(target);
        }},
        _modes[in].keys);
}

void InputRouter::bindToggle(wma::Key key, ModeId mode)
{
    if (!_valid(mode))
    {
        INK_WARN << "InputRouter::bindToggle: no such mode (" << mode << ")";
        return;
    }

    _toggles.push_back(Toggle{key, mode});

    /*
     * Registered into every mode that exists; createMode() replays the record
     * above into every mode declared afterwards. A toggle has to work from
     * wherever the player is, which makes it the one binding kind that cannot
     * belong to a single context.
     */
    for (ModeId in = 0; in < _modes.size(); ++in)
        _bindToggleIn(key, mode, in);
}

void InputRouter::bindClose(wma::Key key, ModeId from)
{
    if (!_valid(from))
    {
        INK_WARN << "InputRouter::bindClose: no such mode (" << from << ")";
        return;
    }

    _window->getKeyboardListener().addKeyAction(
        key,
        wma::KeyAction{[this]() {
            if (_menuKeyAvailable())
                close();
        }},
        _modes[from].keys);
}

void InputRouter::bindHeld(wma::Key key, bool& flag, ModeId mode)
{
    if (!_valid(mode))
    {
        INK_WARN << "InputRouter::bindHeld: no such mode (" << mode << ")";
        return;
    }

    _modes[mode].heldFlags.push_back(&flag);

    /*
     * The release lambda clears the flag unconditionally: it can only run while
     * this mode is resolved anyway, and a key released in the mode that bound it
     * must always be observed. The press consults the UI, for the case of a mode
     * that has both panels and movement keys.
     */
    _window->getKeyboardListener().addKeyAction(
        key,
        wma::KeyAction{[this, target = &flag]() { *target = gameHasKeyboard(); },
                       [target = &flag]() { *target = false; }},
        _modes[mode].keys);
}

void InputRouter::bindPress(wma::Key key, std::function<void()> action, ModeId mode)
{
    if (!_valid(mode))
    {
        INK_WARN << "InputRouter::bindPress: no such mode (" << mode << ")";
        return;
    }

    _window->getKeyboardListener().addKeyAction(
        key,
        wma::KeyAction{[this, action = std::move(action)]() {
            if (gameHasKeyboard())
                action();
        }},
        _modes[mode].keys);
}

void InputRouter::bindLook(std::function<void(const wma::WMAMousePosition&)> action, ModeId mode)
{
    if (!_valid(mode))
    {
        INK_WARN << "InputRouter::bindLook: no such mode (" << mode << ")";
        return;
    }

    _window->getMouseListener().setMoveAction(
        wma::MouseAction{wma::MouseAction::PositionCallback(
            [this, action = std::move(action)](const wma::WMAMousePosition& position) {
                if (gameHasPointer())
                    action(position);
            })},
        _modes[mode].pointer);
}

void InputRouter::switchTo(ModeId mode)
{
    if (!_valid(mode))
    {
        INK_WARN << "InputRouter::switchTo: no such mode (" << mode << ")";
        return;
    }

    _pending = mode;
    _switchPending = true;
}

void InputRouter::close()
{
    switchTo(_base);
}

void InputRouter::toggle(ModeId mode)
{
    /*
     * Compared against the *pending* mode rather than the live one, so two
     * presses within a frame do not both see the old state and cancel out. A
     * third mode being current switches to this one instead of closing, so one
     * screen key reaches another screen directly.
     */
    const ModeId current = _switchPending ? _pending : _mode;
    switchTo(current == mode ? _base : mode);
}

void InputRouter::newFrame()
{
    /*
     * Applied here rather than inside the binding, so a key pressed while the
     * previous frame's UI was being built never changes the mode halfway
     * through it. The frame the UI sees is always internally consistent.
     */
    if (_switchPending)
    {
        _switchPending = false;
        _applyMode(_pending);
    }

    /*
     * Opened in every mode, not only UI-enabled ones. An immediate-mode frame
     * that submits no panel costs a buffer clear, and running it unconditionally
     * is what keeps isCapturingMouse()/isCapturingKeyboard() describing the frame
     * that just happened rather than the last one that had a panel in it.
     */
    _gui->newFrame();
}

void InputRouter::_applyMode(ModeId mode)
{
    /*
     * The departing mode's movement flags are dropped before the switch: their
     * release events will be dispatched into the incoming mode's context, where
     * the bindings that would have cleared them do not exist. This is what stops
     * the player walking on behind an open menu.
     */
    for (bool* flag : _modes[_mode].heldFlags)
        *flag = false;

    _mode = mode;

    const Mode& target = _modes[mode];

    wma::KeyboardListener& keyboard = _window->getKeyboardListener();
    wma::MouseListener& pointer = _window->getMouseListener();

    keyboard.setActiveContext(target.keys);
    pointer.setActiveContext(target.pointer);
    _window->getTouchListener().setActiveContext(target.touch);

    /*
     * Skipped on Android: there is no cursor to capture, and relative mouse
     * mode would only interfere with the touch stream.
     */
#ifndef __ANDROID__
    pointer.setCursorEnabled(target.desc.cursorVisible);

    //! Forgets keys the window never saw released -- the same hazard as the
    //! held flags above, for anything polling isKeyDown().
    keyboard.releaseAllKeys();
#endif
}

bool InputRouter::gameHasKeyboard() const noexcept
{
    /*
     * A mode the UI takes no input in cannot be having its keyboard eaten by
     * it, so the capture flag is not consulted there. That is not just an
     * optimisation: the UI polls the cursor position regardless of context, so
     * in a cursor-captured mode a HUD panel sitting under the stale pointer
     * would report a capture and freeze the very bindings it never received.
     */
    const Mode& current = _modes[_mode];
    return !current.desc.uiEnabled || !_gui->isCapturingKeyboard();
}

bool InputRouter::gameHasPointer() const noexcept
{
    const Mode& current = _modes[_mode];
    return !current.desc.uiEnabled || !_gui->isCapturingMouse();
}

} // namespace aura3d::ui
