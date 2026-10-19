/*
 * Copy, cut, paste and undo in a TextField, and where the text goes: UIRoot's clipboard, which is the application's
 * own (in the process) until the window supplies the system's.
 *
 * What is protected here is what a user would lose or leak: a passphrase must not reach the clipboard, a cut the
 * clipboard did not take must not delete, a paste must not put a line break or invalid UTF-8 into a single line,
 * and one that does not fit must not cut a character in half.
 */

#include <string>

#include "aura/UI/Backend/WindowClipboard.h"
#include "aura/UI/UI.hpp"

#include "TestUtils.h"
#include "WindowTestUtils.h"

using namespace aura3d;
using namespace aura3d::ui;

namespace
{

constexpr wma::KeyModifiers kCtrl{.ctrl = true};
constexpr wma::KeyModifiers kCtrlShift{.shift = true, .ctrl = true};
constexpr wma::KeyModifiers kShift{.shift = true};

struct Harness
{
    AtlasTextShaper shaper{TextShaperDesc{}};
    UIRoot root{shaper};
    TextField *field = nullptr;

    Harness(std::string text = {}, usize maxBytes = 1024)
    {
        root.resize({400.0f, 300.0f});
        field = &root.setContent<Column>().add<TextField>(std::move(text));
        field->setMaxBytes(maxBytes);
        root.update(0.0f);
        field->requestFocus();
    }

    void key(wma::Key key, wma::KeyModifiers mods = {})
    {
        root.keyDown(key, mods);
    }
    void type(std::string_view text)
    {
        root.textInput(text);
    }
    [[nodiscard]] const std::string &text() const
    {
        return field->text.get();
    }
};

/// A clipboard a test can look into, and make refuse.
class FakeClipboard final : public IClipboard
{
  public:
    std::string held;
    bool accepts = true;
    int reads = 0;
    int writes = 0;

    std::string text() override
    {
        ++reads;
        return held;
    }
    bool setText(std::string_view text) override
    {
        ++writes;
        if (!accepts)
            return false;
        held.assign(text);
        return true;
    }
};

void testCopyCutPaste()
{
    Harness h{"hello world"};
    h.field->setCaret(0);
    h.field->setCaret(5, true);

    AURA_CHECK(h.field->selectedText() == "hello", "the selection is readable");

    h.key(wma::KEY_C, kCtrl);
    h.field->setCaret(11);
    h.key(wma::KEY_V, kCtrl);
    AURA_CHECK(h.text() == "hello worldhello", "Ctrl+C then Ctrl+V pastes at the caret");

    h.field->setCaret(0);
    h.field->setCaret(5, true);
    h.key(wma::KEY_X, kCtrl);
    AURA_CHECK(h.text() == " worldhello" && h.field->selectedText().empty(), "Ctrl+X removes the selection");
    h.field->setCaret(0);
    h.key(wma::KEY_V, kCtrl);
    AURA_CHECK(h.text() == "hello worldhello", "and what it removed is what Ctrl+V brings back");

    h.field->setCaret(0);
    h.field->setCaret(5, true);
    h.key(wma::KEY_INSERT, kCtrl);
    h.key(wma::KEY_DELETE, kShift);
    AURA_CHECK(h.text() == " worldhello", "Ctrl+Insert copies and Shift+Delete cuts");
    h.field->setCaret(0);
    h.key(wma::KEY_INSERT, kShift);
    AURA_CHECK(h.text() == "hello worldhello", "and Shift+Insert pastes");

    h.field->setCaret(6);
    h.field->setCaret(11, true);
    h.key(wma::KEY_V, kCtrl);
    AURA_CHECK(h.text() == "hello hellohello", "pasting over a selection replaces it");
}

void testClipboardIsTheRootsAndReplaceable()
{
    Harness a{"shared"};
    a.field->selectAll();
    a.key(wma::KEY_C, kCtrl);

    AURA_CHECK(a.root.clipboard().text() == "shared", "with nothing installed, the root keeps what was copied");

    auto &second = a.root.content()->add<TextField>();
    a.root.update(0.0f);
    second.requestFocus();
    a.key(wma::KEY_V, kCtrl);
    AURA_CHECK(second.text.get() == "shared", "so another field of the same application can paste it");

    FakeClipboard system;
    system.held = "from outside";
    a.root.setClipboard(&system);
    a.key(wma::KEY_V, kCtrl);
    AURA_CHECK(second.text.get() == "sharedfrom outside", "an installed clipboard is the one pasted from");

    second.selectAll();
    a.key(wma::KEY_C, kCtrl);
    AURA_CHECK(system.held == "sharedfrom outside", "and copied to");

    a.root.setClipboard(nullptr);
    AURA_CHECK(a.root.clipboard().text() == "shared", "null puts the root's own back, with what it held");
}

void testNothingToCopyOrPasteStillConsumesTheKey()
{
    Harness h{"text"};
    h.field->setCaret(2);
    FakeClipboard system;
    h.root.setClipboard(&system);

    h.key(wma::KEY_C, kCtrl);
    h.key(wma::KEY_X, kCtrl);
    AURA_CHECK(system.writes == 0 && h.text() == "text", "copy and cut with no selection do nothing");

    h.key(wma::KEY_V, kCtrl);
    AURA_CHECK(h.text() == "text", "pasting an empty clipboard does nothing");
}

void testPassphraseNeverReachesTheClipboard()
{
    Harness h{"hunter2"};
    h.field->setObscured(true);
    FakeClipboard system;
    h.root.setClipboard(&system);

    h.field->selectAll();
    h.key(wma::KEY_C, kCtrl);
    h.key(wma::KEY_X, kCtrl);
    h.key(wma::KEY_INSERT, kCtrl);
    h.key(wma::KEY_DELETE, kShift);
    AURA_CHECK(system.writes == 0 && system.held.empty(), "copy and cut give a passphrase to nobody");
    AURA_CHECK(h.text() == "hunter2", "and a cut that took nothing deletes nothing");

    system.held = "pasted";
    h.key(wma::KEY_V, kCtrl);
    AURA_CHECK(h.text() == "pasted", "pasting into one is allowed");
}

void testPassphraseLeavesNoHistory()
{
    Harness h{"hunter2"};
    h.field->setObscured(true);
    h.field->selectAll();
    h.type("swordfish");
    AURA_CHECK(!h.field->canUndo(), "an obscured field keeps no earlier state of the passphrase");

    h.field->setObscured(false);
    h.type("!");
    h.field->setObscured(true);
    AURA_CHECK(!h.field->canUndo() && !h.field->canRedo(), "and hiding a field drops what it had kept");
}

void testReadOnlyCopiesButDoesNotChange()
{
    Harness h{"fixed"};
    h.field->setReadOnly(true);
    FakeClipboard system;
    system.held = "intruder";
    h.root.setClipboard(&system);

    h.field->selectAll();
    h.key(wma::KEY_C, kCtrl);
    AURA_CHECK(system.held == "fixed", "a read-only field can be copied from");

    system.held = "intruder";
    h.key(wma::KEY_X, kCtrl);
    h.key(wma::KEY_V, kCtrl);
    h.key(wma::KEY_Z, kCtrl);
    AURA_CHECK(h.text() == "fixed" && system.held == "intruder", "but not cut, pasted into or undone");
}

void testCutThatWasNotTakenDoesNotDelete()
{
    Harness h{"keep me"};
    FakeClipboard system;
    system.accepts = false;
    h.root.setClipboard(&system);

    h.field->selectAll();
    h.key(wma::KEY_X, kCtrl);
    AURA_CHECK(h.text() == "keep me", "a clipboard that declines the text leaves it in the field");
    AURA_CHECK(system.writes == 1, "having been asked");
}

void testPasteIsMadeFitForASingleLine()
{
    Harness h;
    FakeClipboard system;
    h.root.setClipboard(&system);

    system.held = "line one\r\nline two\n\nline three\tend";
    h.key(wma::KEY_V, kCtrl);
    AURA_CHECK(h.text() == "line one line two line three end", "line breaks and tabs become one space each run");

    h.field->selectAll();
    system.held = std::string("a\x01"
                              "b\x7f"
                              "c",
                              5);
    h.key(wma::KEY_V, kCtrl);
    AURA_CHECK(h.text() == "abc", "other control characters are removed");

    h.field->selectAll();
    system.held = "ok \xFF\xFE bad \xC3\x28 \xE2\x28\xA1 end";
    h.key(wma::KEY_V, kCtrl);
    AURA_CHECK(h.text() == "ok  bad ( ( end", "invalid UTF-8 is removed, whole sequences at a time");

    h.field->selectAll();
    system.held = "caf\xC3\xA9 \xE2\x9C\x93 \xF0\x9F\x99\x82";
    h.key(wma::KEY_V, kCtrl);
    AURA_CHECK(h.text() == system.held, "valid text of every width passes through unchanged");
}

void testPasteIsCutShortAtACharacter()
{
    Harness h{"", 8};
    FakeClipboard system;
    h.root.setClipboard(&system);

    //! Four three-byte characters is twelve bytes; room for eight takes two of them, not two and a bit.
    system.held = "\xE2\x9C\x93\xE2\x9C\x93\xE2\x9C\x93\xE2\x9C\x93";
    h.key(wma::KEY_V, kCtrl);
    AURA_CHECK(h.text() == "\xE2\x9C\x93\xE2\x9C\x93", "a paste too long for the field is cut at a character");

    system.held = "xyz";
    h.field->selectAll();
    h.key(wma::KEY_V, kCtrl);
    AURA_CHECK(h.text() == "xyz", "the room counts the selection the paste replaces");

    Harness full{"12345678", 8};
    full.root.setClipboard(&system);
    full.field->setCaret(8);
    system.held = "more";
    full.key(wma::KEY_V, kCtrl);
    AURA_CHECK(full.text() == "12345678", "a full field takes nothing");
}

void testUndoAndRedo()
{
    Harness h;

    h.type("a");
    h.type("b");
    h.type("c");
    AURA_CHECK(h.text() == "abc" && h.field->canUndo() && !h.field->canRedo(), "typing makes history");

    h.key(wma::KEY_Z, kCtrl);
    AURA_CHECK(h.text().empty(), "a run of typing is undone in one step");
    AURA_CHECK(h.field->canRedo(), "and can be done again");

    h.key(wma::KEY_Y, kCtrl);
    AURA_CHECK(h.text() == "abc" && h.field->caret() == 3, "Ctrl+Y redoes it, caret where it was");

    h.key(wma::KEY_Z, kCtrl);
    h.key(wma::KEY_Z, kCtrlShift);
    AURA_CHECK(h.text() == "abc", "Ctrl+Shift+Z redoes too");

    h.key(wma::KEY_LEFT);
    h.type("X");
    AURA_CHECK(h.text() == "abXc", "moving the caret and typing makes a new step");
    h.key(wma::KEY_Z, kCtrl);
    AURA_CHECK(h.text() == "abc", "which is undone alone: the earlier run is still one");
    h.key(wma::KEY_Z, kCtrl);
    AURA_CHECK(h.text().empty(), "and then the run before it");

    h.key(wma::KEY_Z, kCtrl);
    AURA_CHECK(h.text().empty() && !h.field->canUndo(), "undo with no history does nothing");

    h.key(wma::KEY_Y, kCtrl);
    h.key(wma::KEY_Y, kCtrl);
    h.type("new");
    AURA_CHECK(!h.field->canRedo(), "an edit after an undo drops what could have been redone");
}

void testUndoRestoresTheSelection()
{
    Harness h{"hello world"};
    h.field->setCaret(0);
    h.field->setCaret(5, true);
    h.type("HOWDY");
    AURA_CHECK(h.text() == "HOWDY world", "typing over a selection replaces it");

    h.key(wma::KEY_Z, kCtrl);
    AURA_CHECK(h.text() == "hello world" && h.field->selectedText() == "hello",
               "undoing it brings back the text and the selection that was replaced");
}

void testDeletionRunsAndCut()
{
    Harness h{"abcdef"};
    h.field->setCaret(6);
    h.key(wma::KEY_BACKSPACE);
    h.key(wma::KEY_BACKSPACE);
    h.key(wma::KEY_BACKSPACE);
    AURA_CHECK(h.text() == "abc", "three backspaces");
    h.key(wma::KEY_Z, kCtrl);
    AURA_CHECK(h.text() == "abcdef" && h.field->caret() == 6, "are one step, with the caret where it was");

    h.field->selectAll();
    h.key(wma::KEY_X, kCtrl);
    AURA_CHECK(h.text().empty(), "a cut");
    h.key(wma::KEY_Z, kCtrl);
    AURA_CHECK(h.text() == "abcdef" && h.field->selectedText() == "abcdef", "is its own step, selection and all");
}

void testAssigningTheTextForgetsHistory()
{
    Harness h;
    h.type("typed");
    AURA_CHECK(h.field->canUndo(), "there is history");

    h.field->text = "set from outside";
    AURA_CHECK(!h.field->canUndo() && !h.field->canRedo(), "an assignment from outside forgets it");
    h.key(wma::KEY_Z, kCtrl);
    AURA_CHECK(h.text() == "set from outside", "so undo does not bring back text that is no longer related");
}

void testHistoryIsBounded()
{
    Harness h{"", static_cast<usize>(INK_MIB_TO_BYTES(1))};
    for (int i = 0; i < 400; ++i)
    {
        h.type("x");
        h.key(wma::KEY_LEFT);
        h.key(wma::KEY_RIGHT);
    }
    int steps = 0;
    while (h.field->canUndo() && steps < 1000)
    {
        h.key(wma::KEY_Z, kCtrl);
        ++steps;
    }
    AURA_CHECK(steps > 0 && steps <= 100, "the history keeps a bounded number of steps");
}

void testHistoryLimitIsTheFields()
{
    Harness h{"", static_cast<usize>(INK_MIB_TO_BYTES(1))};
    h.field->setHistoryLimit(5, static_cast<usize>(INK_MIB_TO_BYTES(1)));
    for (int i = 0; i < 20; ++i)
    {
        h.type("x");
        h.key(wma::KEY_LEFT);
        h.key(wma::KEY_RIGHT);
    }
    int steps = 0;
    while (h.field->canUndo() && steps < 1000)
    {
        h.key(wma::KEY_Z, kCtrl);
        ++steps;
    }
    AURA_CHECK(steps == 5, "a field keeps the number of steps it was given");

    h.field->setHistoryLimit(100, 3);
    for (int i = 0; i < 10; ++i)
    {
        h.type("yy");
        h.key(wma::KEY_LEFT);
        h.key(wma::KEY_RIGHT);
    }
    steps = 0;
    while (h.field->canUndo() && steps < 1000)
    {
        h.key(wma::KEY_Z, kCtrl);
        ++steps;
    }
    AURA_CHECK(steps >= 1 && steps < 10, "and the bytes it may hold across them, the newest step always kept");
}

void testTypedControlsAreNotInserted()
{
    Harness h;
    h.type("\t");
    h.type("a\x01"
           "b");
    AURA_CHECK(h.text() == "ab", "a control character typed (Tab arrives as one on some backends) is not inserted");
    h.type("\xFF");
    AURA_CHECK(h.text() == "ab", "and neither is invalid UTF-8");
}

void testWindowClipboardAdaptsTheWindow()
{
    test::FakeWindow window;
    WindowClipboard clipboard{window};

    AURA_CHECK(clipboard.setText("to the window") && window.clipboardText == "to the window",
               "the adapter hands text to the window");
    window.clipboardText = "from the window";
    AURA_CHECK(clipboard.text() == "from the window", "and reads the window's");

    window.clipboardAccepts = false;
    AURA_CHECK(!clipboard.setText("refused"), "a window that declines says so");
}

void testSanitize()
{
    AURA_CHECK(utf8::sanitize("plain", utf8::Controls::Space) == "plain", "plain text is untouched");
    AURA_CHECK(utf8::sanitize("a\n\n\nb", utf8::Controls::Space) == "a b", "a run of breaks is one space");
    AURA_CHECK(utf8::sanitize("a\n\x01\nb", utf8::Controls::Space) == "a b",
               "and another control between them does not make it two");
    AURA_CHECK(utf8::sanitize("a\nb", utf8::Controls::Drop) == "ab", "dropped, they are gone");
    AURA_CHECK(utf8::sanitize("a\xE2\x80\xA8"
                              "b",
                              utf8::Controls::Space) == "a b",
               "U+2028 is a break");
    AURA_CHECK(utf8::sanitize("\xED\xA0\x80", utf8::Controls::Space).empty(), "a surrogate is invalid");
    AURA_CHECK(utf8::sanitize("\xC0\xAF", utf8::Controls::Space).empty(), "an overlong form is invalid");
    AURA_CHECK(utf8::sanitize("\xF4\x90\x80\x80", utf8::Controls::Space).empty(), "above U+10FFFF is invalid");
    AURA_CHECK(utf8::sanitize("ab\xE2\x9C", utf8::Controls::Space) == "ab",
               "a truncated sequence at the end is dropped");
}

} // namespace

int main()
{
    testCopyCutPaste();
    testClipboardIsTheRootsAndReplaceable();
    testNothingToCopyOrPasteStillConsumesTheKey();
    testPassphraseNeverReachesTheClipboard();
    testPassphraseLeavesNoHistory();
    testReadOnlyCopiesButDoesNotChange();
    testCutThatWasNotTakenDoesNotDelete();
    testPasteIsMadeFitForASingleLine();
    testPasteIsCutShortAtACharacter();
    testUndoAndRedo();
    testUndoRestoresTheSelection();
    testDeletionRunsAndCut();
    testAssigningTheTextForgetsHistory();
    testHistoryIsBounded();
    testHistoryLimitIsTheFields();
    testTypedControlsAreNotInserted();
    testWindowClipboardAdaptsTheWindow();
    testSanitize();

    AURA_TEST_MAIN_RETURN();
}
