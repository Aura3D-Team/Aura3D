#include "VkException.h"

VkException::VkException() noexcept
    : std::exception()
{
    //Empty
}

VkException::VkException(const char* msg)
    : _msg(msg)
{
    // Empty
}

VkException::VkException(const VkResult code)
{
    if (vkResultToString.find(code) != vkResultToString.end()) {
        _msg = vkResultToString.at(code);
    } else {
        _msg = "VkResult not mapped.";
    }
}

VkException::~VkException() noexcept = default;

const char* VkException::what() const noexcept
{
    return _msg;
}
