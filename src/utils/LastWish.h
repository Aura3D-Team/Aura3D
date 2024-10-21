#ifndef LASTWISH_H
#define LASTWISH_H

#pragma once

#include <functional>

class LastWish {
public:
  LastWish(std::function<void ()> start, std::function<void ()> lastWish) : _lastWish(lastWish)
  { 
    start();
  };
  ~LastWish() 
  { 
    _lastWish();
  };
private:
  std::function<void ()> _lastWish;
};

#endif // LASTWISH_H