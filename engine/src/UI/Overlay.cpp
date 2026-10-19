#include "aura/UI/Overlay.h"

#include "aura/UI/UIRoot.h"
#include <algorithm>
#include <limits>
#include <optional>

#include <ink/Inkogger.h>

namespace aura3d::ui
{

namespace
{

bool within(const Widget *ancestor, const Widget *node)
{
    for (; node; node = node->parent())
        if (node == ancestor)
            return true;
    return false;
}

Widget *pickOverlay(Widget &node, glm::vec2 point)
{
    if (!node.effectivelyVisible())
        return nullptr;
    //! Into the space the node's bounds are in: a translation moves the target with the picture.
    point -= node.translation();
    if (!node.clipsChildren() || node.contentRect().contains(point))
        for (usize i = node.childCount(); i-- > 0;)
            if (Widget *hit = pickOverlay(node.childAt(i), point))
                return hit;
    return node.hitTest(point) ? &node : nullptr;
}

Widget *firstFocusable(Widget &node)
{
    if (!node.effectivelyVisible() || !node.effectivelyEnabled())
        return nullptr;
    if (node.focusable())
        return &node;
    for (const auto &child : node.children())
        if (Widget *found = firstFocusable(*child))
            return found;
    return nullptr;
}

} // namespace

OverlayLayer::OverlayLayer()
{
    //! The layer spans the surface but owns none of it: a click in the gap
    //! between two overlays belongs to the scene underneath, not here.
    setHitTestVisible(false);
}

void OverlayLayer::_register(const OverlayDesc &desc)
{
    _entries.push_back(
        Entry{_nextId++, desc, false, WidgetRef(desc.owner), WidgetRef(root() ? root()->focused() : nullptr), true});
    if (desc.modal && root())
        root()->capturePointer(nullptr);
}

void OverlayLayer::runEnterTransitions()
{
    //! By id, and on copies: a hook may open or close overlays, which moves and erases entries under this loop.
    std::vector<Id> fresh;
    for (usize i = 0, live = std::min(childCount(), _entries.size()); i < live; ++i)
        if (!_entries[i].placed && !_entries[i].closing && childAt(i).visibility() != Visibility::Collapsed)
            fresh.push_back(_entries[i].id);

    for (const Id id : fresh)
    {
        for (usize i = 0, live = std::min(childCount(), _entries.size()); i < live; ++i)
        {
            if (_entries[i].id != id)
                continue;
            _entries[i].placed = true;
            const OverlayDesc desc = _entries[i].desc;
            const auto enter = desc.transition.enter ? desc.transition.enter : _defaultTransition.enter;
            if (enter && root())
                enter(childAt(i), desc);
            break;
        }
    }
}

void OverlayLayer::setDefaultTransition(OverlayTransition transition)
{
    _defaultTransition = std::move(transition);
}

void OverlayLayer::_finishExit(Id id)
{
    for (usize i = 0, live = std::min(childCount(), _entries.size()); i < live; ++i)
    {
        if (_entries[i].id != id || !_entries[i].exiting)
            continue;
        _entries[i].exiting = false;
        _pendingClose = true;
        //! Off screen now, not one frame later when collectClosed() destroys it.
        childAt(i).setVisibility(Visibility::Collapsed);
        return;
    }
}

Widget *OverlayLayer::focusScope() const noexcept
{
    for (usize i = std::min(childCount(), _entries.size()); i-- > 0;)
        if (!_entries[i].closing && childAt(i).effectivelyVisible() && childAt(i).hitTestVisible())
            return &childAt(i);
    return nullptr;
}

bool OverlayLayer::allowsFocus(const Widget *widget) const noexcept
{
    for (usize i = std::min(childCount(), _entries.size()); i-- > 0;)
    {
        if (_entries[i].closing)
            continue;
        if (within(&childAt(i), widget))
            return true;
        if (_entries[i].desc.modal)
            return false;
    }
    return true;
}

Widget *OverlayLayer::widgetAt(glm::vec2 point) const
{
    for (usize i = std::min(childCount(), _entries.size()); i-- > 0;)
    {
        if (_entries[i].closing)
            continue;
        Widget &child = childAt(i);
        if (child.hitTestVisible())
            if (Widget *hit = pickOverlay(child, point))
                return hit;
        if (_entries[i].desc.modal)
            return const_cast<OverlayLayer *>(this);
    }
    return nullptr;
}

void OverlayLayer::syncFocus()
{
    if (!root())
        return;
    Widget *scope = focusScope();
    if (!scope)
        return;
    for (usize i = 0; i < _entries.size(); ++i)
    {
        if (&childAt(i) != scope)
            continue;
        const bool take = _entries[i].focusPending && _entries[i].desc.takeFocus;
        _entries[i].focusPending = false;
        if (take || !allowsFocus(root()->focused()))
            root()->setFocus(firstFocusable(*scope));
        return;
    }
}

void OverlayLayer::forgetOwner(Widget &owner)
{
    for (usize i = _entries.size(); i-- > 0;)
        if (!_entries[i].closing && within(&owner, _entries[i].owner.get()))
            close(_entries[i].id);
}

bool OverlayLayer::dismissOutside(glm::vec2 point)
{
    bool closed = false;
    for (usize i = std::min(childCount(), _entries.size()); i-- > 0;)
    {
        if (_entries[i].closing || !childAt(i).hitTestVisible())
            continue;
        if (pickOverlay(childAt(i), point) || !_entries[i].desc.dismissOnOutsideClick)
            break;
        close(_entries[i].id);
        closed = true;
    }
    return closed;
}

OverlayLayer::Id OverlayLayer::lastId() const noexcept
{
    return _entries.empty() ? kNone : _entries.back().id;
}

bool OverlayLayer::isOpen(Id id) const noexcept
{
    return std::ranges::any_of(_entries,
                               [id](const Entry &entry)
                               {
                                   return entry.id == id && !entry.closing;
                               });
}

std::vector<OverlayLayer::OpenOverlay> OverlayLayer::openOverlays() const
{
    std::vector<OpenOverlay> open;
    for (usize i = 0, live = std::min(childCount(), _entries.size()); i < live; ++i)
    {
        const Widget &child = childAt(i);
        if (_entries[i].closing || !child.hitTestVisible() || child.visibility() == Visibility::Collapsed)
            continue;
        open.push_back({&child, _entries[i].desc.modal});
    }
    return open;
}

bool OverlayLayer::hasModal() const noexcept
{
    return std::ranges::any_of(_entries,
                               [](const Entry &entry)
                               {
                                   return entry.desc.modal && !entry.closing;
                               });
}

bool OverlayLayer::hasLightDismissible() const noexcept
{
    for (usize i = 0, live = std::min(childCount(), _entries.size()); i < live; ++i)
    {
        if (!_entries[i].closing && _entries[i].desc.dismissOnOutsideClick && childAt(i).hitTestVisible())
            return true;
    }
    return false;
}

Widget *OverlayLayer::topmost() const noexcept
{
    for (usize i = std::min(childCount(), _entries.size()); i-- > 0;)
    {
        if (!_entries[i].closing)
            return &childAt(i);
    }

    return nullptr;
}

bool OverlayLayer::contains(glm::vec2 point) const
{
    for (usize i = 0, live = std::min(childCount(), _entries.size()); i < live; ++i)
    {
        //! A click-through overlay -- a tooltip -- is not "inside" anything:
        //! it must not shield the menu below it from an outside click.
        if (_entries[i].closing || !childAt(i).hitTestVisible())
            continue;

        if (childAt(i).visualBounds().contains(point))
            return true;
    }

    return false;
}

void OverlayLayer::close(Id id)
{
    /*
     * Bounded by the child count, not by _entries alone. Destroying an overlay
     * unregisters its widgets, and a widget unregistering itself can land back
     * here -- a tooltip whose owner is inside the overlay being torn down does
     * exactly that -- at the one moment when the child is already erased and
     * its entry is not yet.
     */
    const usize live = std::min(childCount(), _entries.size());

    for (usize i = 0; i < live; ++i)
    {
        if (_entries[i].id != id || _entries[i].closing)
            continue;

        Entry &entry = _entries[i];
        Widget &child = childAt(i);
        entry.closing = true;
        _pendingClose = true;
        const WidgetRef restore = entry.restoreFocus;

        /*
         * Inert now, destroyed later. The widget stops taking input on this
         * very frame -- which is what the user asked for by dismissing it --
         * while its storage outlives the dispatch that is probably still
         * unwinding through it. One with an exit transition keeps drawing
         * until the transition says it is done; any other collapses at once.
         */
        child.setHitTestVisible(false);
        const auto exit = entry.desc.transition.exit ? entry.desc.transition.exit : _defaultTransition.exit;
        //! One that was never placed was never seen: there is nothing to fade out, and fading it from full opacity
        //! would show a popup nobody opened.
        entry.exiting = exit && root() != nullptr && entry.placed;
        if (entry.exiting)
        {
            const OverlayTransition &own = entry.desc.transition;
            const f32 timeout = own.exit ? own.exitTimeoutSeconds : _defaultTransition.exitTimeoutSeconds;
            entry.exitDeadline = timeout > 0.0f ? root()->time() + timeout : std::numeric_limits<f32>::infinity();
        }

        //! Read now: focus handlers and the hook itself may open or close overlays, which moves or erases the entry.
        std::optional<OverlayDesc> leaving;
        if (entry.exiting)
            leaving = entry.desc;
        else
            child.setVisibility(Visibility::Collapsed);

        //! Focus left inside a widget that is still drawing would go on taking keys.
        if (root() && (!root()->focused() || within(&child, root()->focused())))
        {
            root()->setFocus(restore.get());
            if (root()->focused() && within(&child, root()->focused()))
                root()->setFocus(nullptr);
        }

        if (leaving)
        {
            exit(child, *leaving,
                 [layer = WidgetRef(this), id]
                 {
                     if (Widget *self = layer.get())
                         static_cast<OverlayLayer *>(self)->_finishExit(id);
                 });
        }
        return;
    }
}

bool OverlayLayer::closeTopmost()
{
    for (usize i = _entries.size(); i-- > 0;)
    {
        //! Stepped over, not stopped at: an overlay Escape has no business
        //! with must not shield the one below it that does.
        if (_entries[i].closing || !_entries[i].desc.dismissOnEscape)
            continue;

        close(_entries[i].id);
        return true;
    }

    return false;
}

void OverlayLayer::closeLightDismissible()
{
    for (usize i = std::min(childCount(), _entries.size()); i-- > 0;)
    {
        if (_entries[i].closing)
            continue;

        //! An overlay that cannot be clicked cannot be what stops a
        //! click-driven dismissal: a tooltip floating over a menu is passed
        //! straight over rather than shielding it.
        if (!childAt(i).hitTestVisible())
            continue;

        //! Stops at the first overlay that holds its ground, so a click
        //! outside a submenu does not also tear down the dialog behind it.
        if (!_entries[i].desc.dismissOnOutsideClick)
            return;

        close(_entries[i].id);
    }
}

void OverlayLayer::closeFrom(const Widget &member)
{
    const usize live = std::min(childCount(), _entries.size());
    usize from = live;
    for (usize i = 0; i < live; ++i)
        if (within(&childAt(i), &member))
        {
            from = i;
            break;
        }
    for (usize i = from; i < live; ++i)
        if (!_entries[i].closing)
            close(_entries[i].id);
}

Widget *OverlayLayer::ownerOf(const Widget &member) const noexcept
{
    const usize live = std::min(childCount(), _entries.size());
    for (usize i = 0; i < live; ++i)
        if (within(&childAt(i), &member))
            return _entries[i].owner.get();
    return nullptr;
}

void OverlayLayer::closeAll()
{
    for (const Entry &entry : _entries)
    {
        if (!entry.closing)
            close(entry.id);
    }
}

void OverlayLayer::collectClosed()
{
    if (!_pendingClose)
        return;

    _pendingClose = false;

    //! Back to front: removing a child shifts everything after it, and the
    //! callbacks run once the tree is settled rather than mid-erase.
    std::vector<std::function<void()>> notify;

    for (usize i = _entries.size(); i-- > 0;)
    {
        if (!_entries[i].closing)
            continue;

        //! Still leaving; its done() asks for another collection, and a hook that never calls it is not waited for
        //! past its deadline.
        if (_entries[i].exiting)
        {
            if (root() != nullptr && root()->time() < _entries[i].exitDeadline)
            {
                _pendingClose = true;
                continue;
            }
            INK_WARN << "An overlay's exit transition did not finish in time; closing it";
        }

        if (_entries[i].desc.onClosed)
            notify.push_back(std::move(_entries[i].desc.onClosed));

        remove(childAt(i));
    }

    for (const std::function<void()> &callback : notify)
        callback();
}

void OverlayLayer::onChildRemoved(usize index)
{
    if (index < _entries.size())
        _entries.erase(_entries.begin() + static_cast<ptrdiff_t>(index));
}

glm::vec2 OverlayLayer::measureContent(const Constraints &available)
{
    //! Loose, always: an overlay is sized by what is in it, never by what it
    //! is anchored to. The layer itself reports nothing, so it never grows the
    //! surface.
    for (usize i = 0; i < childCount(); ++i)
        childAt(i).measure(Constraints::loose(available.max));

    return {0.0f, 0.0f};
}

glm::vec2 OverlayLayer::place(const OverlayDesc &desc, glm::vec2 size, const Rect &surface)
{
    const Rect &anchor = desc.anchor;

    glm::vec2 origin{anchor.min};

    switch (desc.placement)
    {
    case Placement::Below:
        origin = {anchor.min.x, anchor.max.y};
        //! Flipped rather than clamped when it would run off the bottom: a
        //! list pinned to the screen edge would cover its own trigger.
        if (origin.y + size.y > surface.max.y && anchor.min.y - size.y >= surface.min.y)
            origin.y = anchor.min.y - size.y;
        break;

    case Placement::Above:
        origin = {anchor.min.x, anchor.min.y - size.y};
        if (origin.y < surface.min.y && anchor.max.y + size.y <= surface.max.y)
            origin.y = anchor.max.y;
        break;

    case Placement::Right:
        origin = {anchor.max.x, anchor.min.y};
        if (origin.x + size.x > surface.max.x && anchor.min.x - size.x >= surface.min.x)
            origin.x = anchor.min.x - size.x;
        break;

    case Placement::Left:
        origin = {anchor.min.x - size.x, anchor.min.y};
        if (origin.x < surface.min.x && anchor.max.x + size.x <= surface.max.x)
            origin.x = anchor.max.x;
        break;

    case Placement::Over:
        origin = anchor.min;
        break;

    case Placement::Cursor:
        origin = anchor.min + glm::vec2{desc.cursorClearance};
        if (origin.x + size.x > surface.max.x)
            origin.x = anchor.min.x - size.x - desc.cursorClearance;
        if (origin.y + size.y > surface.max.y)
            origin.y = anchor.min.y - size.y - desc.cursorClearance;
        break;

    case Placement::Center:
        origin = surface.center() - size * 0.5f;
        break;
    }

    origin += desc.offset;

    //! Whatever the placement decided, the overlay ends up on screen: clamped
    //! into the surface, and only pushed past the top-left when it is simply
    //! bigger than the window.
    const glm::vec2 limit = glm::max(surface.min, surface.max - size);
    return glm::clamp(origin, surface.min, limit);
}

void OverlayLayer::arrangeContent(const Rect &content)
{
    for (usize i = 0, live = std::min(childCount(), _entries.size()); i < live; ++i)
    {
        Widget &child = childAt(i);

        const Entry &entry = _entries[i];
        if ((entry.closing && !entry.exiting) || child.visibility() == Visibility::Collapsed)
        {
            child.arrange(Rect{});
            continue;
        }

        const glm::vec2 size = child.desiredSize();
        child.arrange(Rect::fromSize(place(entry.desc, size, content), size));
    }
}

} // namespace aura3d::ui
