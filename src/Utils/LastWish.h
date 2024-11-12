#ifndef LASTWISH_H
#define LASTWISH_H

#pragma once

#include <functional>

/**
 * @class LastWish
 *
 * @brief The LastWish class is designed to execute two functions:
 *        one when an object is created (start function), and another when the object goes out of scope and is destroyed (last wish function).
 *
 * This pattern can be useful in scenarios where you want to ensure that certain cleanup or follow-up actions are performed
 * automatically after the execution of a function or at the end of a specific scope.
 */
class LastWish {
public:
    /**
     * @brief Constructor that executes the initial function immediately.
     *
     * This constructor takes two callback functions: `start` and `lastWish`.
     * The `start` function is executed immediately during the object’s construction,
     * and the `lastWish` function is executed when the object is destroyed.
     *
     * @param start The function to execute upon construction.
     * @param lastWish The function to execute upon destruction.
     */
    LastWish(std::function<void ()> start, std::function<void ()> lastWish) : _lastWish(lastWish)
    {
        start();
    }

    /**
     * @brief Destructor that executes the last wish function.
     *
     * When the `LastWish` object is destroyed (e.g., when it goes out of scope),
     * the `lastWish` function provided during construction is executed automatically.
     */
    ~LastWish()
    {
        _lastWish();
    }

private:
    /**
     * @brief The function to execute upon destruction.
     *
     * This function is executed when the object is destroyed (in the destructor).
     */
    std::function<void ()> _lastWish;
};

#endif // LASTWISH_H
